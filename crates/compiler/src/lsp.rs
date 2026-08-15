//! Small LSP wrapping `check_project`. Same diagnostics as `--message-format=json`.
//! No second typer. Open buffers overlay disk text. Hover, completion, and definition use that parse.

use crate::check::{
    canonicalize_source_path, check_project_with, complete_project, definition_project,
    hover_project, json_str, Diagnostic,
};
use crate::overlay::collect_fmt_sources;
use anyhow::Result;
use std::collections::BTreeMap;
use std::fs;
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};

pub fn run_lsp(root: &Path) -> Result<()> {
    let stdin = std::io::stdin();
    let stdout = std::io::stdout();
    run_lsp_io(root, stdin.lock(), stdout.lock())
}

fn run_lsp_io<R: Read, W: Write>(root: &Path, reader: R, mut writer: W) -> Result<()> {
    let mut root = fs::canonicalize(root).unwrap_or_else(|_| root.to_path_buf());
    let mut open: BTreeMap<PathBuf, String> = BTreeMap::new();
    let mut buf = BufReader::new(reader);
    loop {
        let Some(body) = read_message(&mut buf)? else {
            break;
        };
        let method = json_string_field(&body, "method").unwrap_or_default();
        let id = json_id(&body);
        if method == "initialize" {
            if let Some(p) = root_from_init(&body) {
                root = p;
            }
            let caps = r#"{"capabilities":{"textDocumentSync":{"openClose":true,"change":1},"hoverProvider":true,"completionProvider":{"triggerCharacters":["."]},"definitionProvider":true}}"#;
            write_result(&mut writer, id, caps)?;
        } else if method == "shutdown" {
            write_result(&mut writer, id, "null")?;
        } else if method == "exit" {
            break;
        } else if method == "textDocument/didOpen" || method == "textDocument/didChange" {
            if let Some((path, text)) = doc_text_from_message(&body) {
                open.insert(path, text);
            }
            publish_check(&root, &open, &mut writer)?;
        } else if method == "textDocument/didClose" {
            if let Some(path) = doc_path_from_message(&body) {
                let key = canonicalize_source_path(&path);
                open.retain(|p, _| canonicalize_source_path(p) != key);
            }
            publish_check(&root, &open, &mut writer)?;
        } else if method == "textDocument/hover" {
            let result = hover_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/completion" {
            let result = completion_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/definition" {
            let result = definition_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/didSave" {
            publish_check(&root, &open, &mut writer)?;
        } else if method == "initialized" || method.is_empty() {
            continue;
        } else if let Some(n) = id {
            write_error(
                &mut writer,
                Some(n),
                -32601,
                &format!("method not found: {method}"),
            )?;
        }
    }
    Ok(())
}

fn read_message<R: BufRead>(reader: &mut R) -> Result<Option<String>> {
    let mut content_len: Option<usize> = None;
    let mut saw = false;
    loop {
        let mut line = String::new();
        let n = reader.read_line(&mut line)?;
        if n == 0 {
            return Ok(None);
        }
        let line = line.trim_end_matches(['\n', '\r']);
        if line.is_empty() {
            if !saw {
                return Ok(None);
            }
            break;
        }
        saw = true;
        let lower = line.to_ascii_lowercase();
        if let Some(rest) = lower.strip_prefix("content-length:") {
            content_len = rest.trim().parse().ok();
        }
    }
    let n = content_len.unwrap_or(0);
    let mut buf = vec![0u8; n];
    if n > 0 {
        reader.read_exact(&mut buf)?;
    }
    Ok(Some(String::from_utf8_lossy(&buf).into_owned()))
}

fn write_msg<W: Write>(writer: &mut W, body: &str) -> Result<()> {
    write!(writer, "Content-Length: {}\r\n\r\n{}", body.len(), body)?;
    writer.flush()?;
    Ok(())
}

fn write_result<W: Write>(writer: &mut W, id: Option<String>, result: &str) -> Result<()> {
    let n = id.unwrap_or_else(|| "null".into());
    write_msg(
        writer,
        &format!(r#"{{"jsonrpc":"2.0","id":{n},"result":{result}}}"#),
    )
}

fn write_error<W: Write>(
    writer: &mut W,
    id: Option<String>,
    code: i64,
    message: &str,
) -> Result<()> {
    let n = id.unwrap_or_else(|| "null".into());
    write_msg(
        writer,
        &format!(
            r#"{{"jsonrpc":"2.0","id":{n},"error":{{"code":{code},"message":{}}}}}"#,
            json_str(message)
        ),
    )
}

fn write_notify<W: Write>(writer: &mut W, method: &str, params: &str) -> Result<()> {
    write_msg(
        writer,
        &format!(
            r#"{{"jsonrpc":"2.0","method":{},"params":{}}}"#,
            json_str(method),
            params
        ),
    )
}

fn publish_check<W: Write>(
    root: &Path,
    open: &BTreeMap<PathBuf, String>,
    writer: &mut W,
) -> Result<()> {
    let diags = match check_project_with(root, open) {
        Ok(d) => d,
        Err(e) => vec![Diagnostic::error(e.to_string())],
    };
    let mut by_uri: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for path in src_files(root)? {
        by_uri.entry(file_uri(&path)).or_default();
    }
    for path in open.keys() {
        by_uri.entry(file_uri(path)).or_default();
    }
    for d in &diags {
        let uri = diag_uri(root, d);
        by_uri.entry(uri).or_default().push(lsp_diagnostic_json(d));
    }
    for (uri, items) in by_uri {
        let params = format!(
            r#"{{"uri":{},"diagnostics":[{}]}}"#,
            json_str(&uri),
            items.join(",")
        );
        write_notify(writer, "textDocument/publishDiagnostics", &params)?;
    }
    Ok(())
}

fn hover_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "null".into();
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    match hover_project(root, open, &path, line, character) {
        Ok(Some(text)) => format!(
            r#"{{"contents":{{"kind":"plaintext","value":{}}}}}"#,
            json_str(&text)
        ),
        _ => "null".into(),
    }
}

fn completion_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "null".into();
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    match complete_project(root, open, &path, line, character) {
        Ok(items) if !items.is_empty() => {
            let parts: Vec<String> = items
                .iter()
                .map(|c| {
                    format!(
                        r#"{{"label":{},"kind":{},"detail":{},"insertText":{}}}"#,
                        json_str(&c.label),
                        c.kind,
                        json_str(&c.detail),
                        json_str(&c.insert_text)
                    )
                })
                .collect();
            format!(r#"{{"isIncomplete":false,"items":[{}]}}"#, parts.join(","))
        }
        _ => "null".into(),
    }
}

fn definition_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "null".into();
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    match definition_project(root, open, &path, line, character) {
        Ok(Some((dest, sl, sc, el, ec))) => format!(
            r#"{{"uri":{},"range":{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}}}}"#,
            json_str(&file_uri(&dest))
        ),
        _ => "null".into(),
    }
}

fn src_files(root: &Path) -> Result<Vec<PathBuf>> {
    Ok(collect_fmt_sources(&root.join("src"))?)
}

fn lsp_diagnostic_json(d: &Diagnostic) -> String {
    let line = d.line.unwrap_or(1).saturating_sub(1);
    let col = d.column.unwrap_or(1).saturating_sub(1);
    let mut end_line = d.end_line.unwrap_or(d.line.unwrap_or(1)).saturating_sub(1);
    let mut end_col = d
        .end_column
        .unwrap_or(d.column.unwrap_or(1))
        .saturating_sub(1);
    if end_line < line || (end_line == line && end_col <= col) {
        end_line = line;
        end_col = col.saturating_add(1);
    }
    format!(
        r#"{{"range":{{"start":{{"line":{line},"character":{col}}},"end":{{"line":{end_line},"character":{end_col}}}}},"severity":1,"message":{}}}"#,
        json_str(&d.message)
    )
}

fn diag_uri(root: &Path, d: &Diagnostic) -> String {
    match &d.file {
        Some(f) => file_uri(&diag_path(root, f)),
        None => file_uri(root),
    }
}

fn diag_path(root: &Path, file: &str) -> PathBuf {
    let p = Path::new(file);
    if p.is_absolute() {
        return p.to_path_buf();
    }
    let joined = root.join(file);
    if joined.exists() {
        return joined;
    }
    if let Some(idx) = file.find("/src/") {
        return root.join(&file[idx + 1..]);
    }
    if let Some(stripped) = file.strip_prefix("src/") {
        return root.join("src").join(stripped);
    }
    root.join("src").join(file)
}

fn file_uri(path: &Path) -> String {
    let abs = fs::canonicalize(path).unwrap_or_else(|_| path.to_path_buf());
    let mut out = String::from("file://");
    for b in abs.to_string_lossy().as_bytes() {
        match b {
            b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'/' | b'-' | b'_' | b'.' | b'~' => {
                out.push(*b as char)
            }
            _ => out.push_str(&format!("%{:02X}", b)),
        }
    }
    out
}

fn root_from_init(body: &str) -> Option<PathBuf> {
    if let Some(uri) = json_string_field(body, "rootUri") {
        if !uri.is_empty() && uri != "null" {
            return Some(uri_to_path(&uri));
        }
    }
    json_string_field(body, "rootPath")
        .filter(|s| !s.is_empty())
        .map(PathBuf::from)
}

fn uri_to_path(uri: &str) -> PathBuf {
    let rest = uri.strip_prefix("file://").unwrap_or(uri);
    let rest = rest.strip_prefix("localhost").unwrap_or(rest);
    PathBuf::from(percent_decode(rest))
}

fn percent_decode(s: &str) -> String {
    let bytes = s.as_bytes();
    let mut out = Vec::new();
    let mut i = 0;
    while i < bytes.len() {
        if bytes[i] == b'%' && i + 2 < bytes.len() {
            if let Ok(v) =
                u8::from_str_radix(std::str::from_utf8(&bytes[i + 1..i + 3]).unwrap_or(""), 16)
            {
                out.push(v);
                i += 3;
                continue;
            }
        }
        out.push(bytes[i]);
        i += 1;
    }
    String::from_utf8_lossy(&out).into_owned()
}

fn doc_path_from_message(body: &str) -> Option<PathBuf> {
    let uri = json_string_field(body, "uri")?;
    Some(canonicalize_source_path(&uri_to_path(&uri)))
}

fn doc_text_from_message(body: &str) -> Option<(PathBuf, String)> {
    let path = doc_path_from_message(body)?;
    let text = json_string_field(body, "text")?;
    Some((path, text))
}

fn json_string_field(body: &str, key: &str) -> Option<String> {
    let pat = format!("\"{key}\"");
    let i = body.find(&pat)?;
    let rest = body[i + pat.len()..].trim_start().strip_prefix(':')?;
    let rest = rest.trim_start().strip_prefix('"')?;
    let mut out = String::new();
    let mut chars = rest.chars();
    while let Some(c) = chars.next() {
        if c == '"' {
            return Some(out);
        }
        if c == '\\' {
            match chars.next() {
                Some('"') => out.push('"'),
                Some('\\') => out.push('\\'),
                Some('n') => out.push('\n'),
                Some('r') => out.push('\r'),
                Some('t') => out.push('\t'),
                Some(o) => out.push(o),
                None => break,
            }
        } else {
            out.push(c);
        }
    }
    Some(out)
}

fn json_id(body: &str) -> Option<String> {
    let pat = "\"id\"";
    let i = body.find(pat)?;
    let rest = body[i + pat.len()..].trim_start().strip_prefix(':')?;
    let rest = rest.trim_start();
    if rest.starts_with('"') {
        let s = json_string_field(body, "id")?;
        return Some(json_str(&s));
    }
    if rest.starts_with("null") {
        return Some("null".into());
    }
    let mut n = String::new();
    let mut chars = rest.chars();
    if rest.starts_with('-') {
        n.push('-');
        chars.next();
    }
    for c in chars {
        if c.is_ascii_digit() {
            n.push(c);
        } else {
            break;
        }
    }
    if n.is_empty() || n == "-" {
        None
    } else {
        Some(n)
    }
}

fn json_i64_field(body: &str, key: &str) -> Option<i64> {
    let pat = format!("\"{key}\"");
    let i = body.find(&pat)?;
    let rest = body[i + pat.len()..].trim_start().strip_prefix(':')?;
    let rest = rest.trim_start();
    let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
    digits.parse().ok()
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;
    use tempfile::tempdir;

    fn frame(body: &str) -> Vec<u8> {
        format!("Content-Length: {}\r\n\r\n{}", body.len(), body).into_bytes()
    }

    #[test]
    fn lsp_publishes_check_diagnostics_then_exits() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_test\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        fs::write(
            root.join("src/Main.scuzz"),
            "@main def main: IO[Unit] =\n  IO.println(1)\n",
        )
        .unwrap();
        let uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{uri}","capabilities":{{}}}}}}"#
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(
            r#"{"jsonrpc":"2.0","method":"initialized","params":{}}"#,
        ));
        input.extend(frame(
            r#"{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{"textDocument":{"uri":"file:///x"}}}"#,
        ));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("textDocument/publishDiagnostics"), "{text}");
        assert!(text.contains("\"severity\":1"), "{text}");
        assert!(
            text.contains("Main.scuzz") || text.contains("println"),
            "{text}"
        );
        assert!(text.contains("\"id\":1"), "{text}");
        assert!(text.contains("textDocumentSync"), "{text}");
    }

    fn write_ok_pkg(root: &Path) {
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_unsaved\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        fs::write(
            root.join("src/Main.scuzz"),
            "@main def main: IO[Unit] =\n  IO.println(\"ok\")\n",
        )
        .unwrap();
    }

    #[test]
    fn lsp_unsaved_buffer_overlays_disk_until_close() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let bad = json_str("@main def main: IO[Unit] =\n  IO.println(1 + \"x\")\n");
        let open = format!(
            r#"{{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":{},"languageId":"scuzz","version":1,"text":{}}}}}}}"#,
            json_str(&main_uri),
            bad
        );
        let close = format!(
            r#"{{"jsonrpc":"2.0","method":"textDocument/didClose","params":{{"textDocument":{{"uri":{}}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(
            r#"{"jsonrpc":"2.0","method":"initialized","params":{}}"#,
        ));
        input.extend(frame(&open));
        input.extend(frame(&close));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(
            text.contains("Int") || text.contains("String"),
            "expected unsaved type error: {text}"
        );
        let last = text.rfind("publishDiagnostics").expect(&text);
        let after_close = &text[last..];
        assert!(
            !after_close.contains("\"severity\":1"),
            "close must publish disk-clean diagnostics: {after_close}"
        );
    }

    #[test]
    fn lsp_hover_returns_println_signature() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let src = fs::read_to_string(&main).unwrap();
        let off = src.find("println").unwrap();
        let (line, col) = crate::span::offset_to_utf16_pos(&src, off);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let hover = format!(
            r#"{{"jsonrpc":"2.0","id":3,"method":"textDocument/hover","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}}}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&hover));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("hoverProvider"), "{text}");
        assert!(text.contains("IO.println"), "{text}");
        assert!(text.contains("\"id\":3"), "{text}");
    }

    #[test]
    fn lsp_completion_offers_println_after_dot() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_complete\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "@main def main: IO[Unit] =\n  IO.\n";
        fs::write(root.join("src/Main.scuzz"), src).unwrap();
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let off = src.find("IO.").unwrap() + 3;
        let (line, col) = crate::span::offset_to_utf16_pos(src, off);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let comp = format!(
            r#"{{"jsonrpc":"2.0","id":4,"method":"textDocument/completion","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}}}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&comp));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("completionProvider"), "{text}");
        assert!(text.contains("IO.println"), "{text}");
        assert!(text.contains("\"id\":4"), "{text}");
    }

    #[test]
    fn lsp_definition_jumps_to_add() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_def\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src =
            "def add(n: Int): Int = n\n@main def main: IO[Unit] =\n  IO.println(Str.fromInt(add(1)))\n";
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), &formatted).unwrap();
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let call = formatted.rfind("add").unwrap();
        let (line, col) = crate::span::offset_to_utf16_pos(&formatted, call);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let defn = format!(
            r#"{{"jsonrpc":"2.0","id":5,"method":"textDocument/definition","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}}}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&defn));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("definitionProvider"), "{text}");
        assert!(text.contains("\"id\":5"), "{text}");
        assert!(text.contains("\"line\":0"), "{text}");
    }

    #[test]
    fn lsp_unknown_method_returns_protocol_error() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let mut input = Vec::new();
        input.extend(frame(
            r#"{"jsonrpc":"2.0","id":"abc","method":"textDocument/foo","params":{}}"#,
        ));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("\"id\":\"abc\""), "{text}");
        assert!(text.contains("-32601"), "{text}");
        assert!(text.contains("method not found"), "{text}");
    }

    #[test]
    fn lsp_encodes_file_uri() {
        assert_eq!(percent_decode("file:///tmp/a%20b"), "file:///tmp/a b");
        let p = PathBuf::from("/tmp/hello world");
        let uri = {
            let mut out = String::from("file://");
            for b in p.to_string_lossy().as_bytes() {
                match b {
                    b'A'..=b'Z' | b'a'..=b'z' | b'0'..=b'9' | b'/' | b'-' | b'_' | b'.' | b'~' => {
                        out.push(*b as char)
                    }
                    _ => out.push_str(&format!("%{:02X}", b)),
                }
            }
            out
        };
        assert!(uri.contains("%20"), "{uri}");
        assert_eq!(uri_to_path(&uri), p);
    }
}
