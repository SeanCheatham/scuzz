//! Small LSP wrapping `check_project`. Same diagnostics as `--message-format=json`.
//! No second typer. Unsaved buffers are ignored until they hit disk.

use crate::check::{check_project, Diagnostic};
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

pub fn run_lsp_io<R: Read, W: Write>(root: &Path, reader: R, mut writer: W) -> Result<()> {
    let mut root = fs::canonicalize(root).unwrap_or_else(|_| root.to_path_buf());
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
            let caps = r#"{"capabilities":{"textDocumentSync":1}}"#;
            write_result(&mut writer, id, caps)?;
        } else if method == "shutdown" {
            write_result(&mut writer, id, "null")?;
        } else if method == "exit" {
            break;
        } else if method == "textDocument/didOpen"
            || method == "textDocument/didSave"
            || method == "textDocument/didChange"
        {
            publish_check(&root, &mut writer)?;
        } else if method == "initialized" || method.is_empty() {
            continue;
        } else if let Some(n) = id {
            write_result(&mut writer, Some(n), "null")?;
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

fn write_result<W: Write>(writer: &mut W, id: Option<i64>, result: &str) -> Result<()> {
    let n = id.unwrap_or(0);
    write_msg(
        writer,
        &format!(r#"{{"jsonrpc":"2.0","id":{n},"result":{result}}}"#),
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

fn publish_check<W: Write>(root: &Path, writer: &mut W) -> Result<()> {
    let diags = match check_project(root) {
        Ok(d) => d,
        Err(e) => vec![Diagnostic::error(e.to_string())],
    };
    let mut by_uri: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for path in src_files(root)? {
        by_uri.entry(file_uri(&path)).or_default();
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

fn src_files(root: &Path) -> Result<Vec<PathBuf>> {
    Ok(collect_fmt_sources(&root.join("src"))?)
}

fn lsp_diagnostic_json(d: &Diagnostic) -> String {
    let line = d.line.unwrap_or(1).saturating_sub(1);
    let col = d.column.unwrap_or(1).saturating_sub(1);
    format!(
        r#"{{"range":{{"start":{{"line":{line},"character":{col}}},"end":{{"line":{line},"character":{col}}}}},"severity":1,"message":{}}}"#,
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
    format!("file://{}", abs.display())
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
    PathBuf::from(rest)
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

fn json_id(body: &str) -> Option<i64> {
    let i = body.find("\"id\"")?;
    let rest = body[i + 4..].trim_start().strip_prefix(':')?;
    let rest = rest.trim_start();
    let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
    digits.parse().ok()
}

fn json_str(s: &str) -> String {
    let mut out = String::from("\"");
    for ch in s.chars() {
        match ch {
            '"' => out.push_str("\\\""),
            '\\' => out.push_str("\\\\"),
            '\n' => out.push_str("\\n"),
            '\r' => out.push_str("\\r"),
            '\t' => out.push_str("\\t"),
            c if c.is_control() => out.push_str(&format!("\\u{:04x}", c as u32)),
            c => out.push(c),
        }
    }
    out.push('"');
    out
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
}
