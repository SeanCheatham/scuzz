//! Small LSP wrapping `check_project`. Same diagnostics as `--message-format=json`.
//! No second typer. Open buffers overlay disk text. Hover, completion, definition,
//! document symbols, references, rename, workspace symbols, signature help,
//! document highlights, folding ranges, format, selection ranges, inlay hints,
//! semantic tokens, code actions, and call hierarchy use that parse.

use crate::check::{
    canonicalize_source_path, check_project_with, code_actions_project, complete_project,
    definition_project, folding_ranges_project, highlights_project, hover_project,
    incoming_calls_project, inlay_hints_project, json_str, outgoing_calls_project,
    prepare_call_hierarchy_project, prepare_rename_project, references_project, rename_project,
    selection_ranges_project, semantic_tokens_project, signature_help_project, symbols_project,
    workspace_symbols_project, CallItemLsp, Diagnostic, RenameResult,
};
use crate::fold::FOLD_REGION;
use crate::overlay::collect_fmt_sources;
use crate::tokens::{TOKEN_MODIFIERS, TOKEN_TYPES};
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
            let type_list: Vec<String> = TOKEN_TYPES.iter().map(|t| json_str(t)).collect();
            let mod_list: Vec<String> = TOKEN_MODIFIERS.iter().map(|t| json_str(t)).collect();
            let caps = format!(
                r#"{{"capabilities":{{"textDocumentSync":{{"openClose":true,"change":1}},"hoverProvider":true,"completionProvider":{{"triggerCharacters":["."]}},"definitionProvider":true,"documentSymbolProvider":true,"workspaceSymbolProvider":true,"signatureHelpProvider":{{"triggerCharacters":["("]}},"referencesProvider":true,"renameProvider":{{"prepareProvider":true}},"documentHighlightProvider":true,"foldingRangeProvider":true,"documentFormattingProvider":true,"documentRangeFormattingProvider":true,"selectionRangeProvider":true,"inlayHintProvider":true,"semanticTokensProvider":{{"legend":{{"tokenTypes":[{}],"tokenModifiers":[{}]}},"full":true}},"codeActionProvider":{{"codeActionKinds":["quickfix","source.formatDocument"]}},"callHierarchyProvider":true}}}}"#,
                type_list.join(","),
                mod_list.join(",")
            );
            write_result(&mut writer, id, &caps)?;
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
        } else if method == "textDocument/documentSymbol" {
            let result = document_symbol_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "workspace/symbol" {
            let result = workspace_symbol_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/signatureHelp" {
            let result = signature_help_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/references" {
            let result = references_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/prepareRename" {
            let result = prepare_rename_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/rename" {
            match rename_result(&root, &open, &body) {
                Ok(result) => write_result(&mut writer, id, &result)?,
                Err(msg) => write_error(&mut writer, id, -32602, &msg)?,
            }
        } else if method == "textDocument/documentHighlight" {
            let result = document_highlight_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/foldingRange" {
            let result = folding_range_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/formatting" || method == "textDocument/rangeFormatting" {
            let result = formatting_result(&open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/selectionRange" {
            let result = selection_range_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/inlayHint" {
            let result = inlay_hint_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/semanticTokens/full" {
            let result = semantic_tokens_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/codeAction" {
            let result = code_action_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "textDocument/prepareCallHierarchy" {
            let result = prepare_call_hierarchy_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "callHierarchy/incomingCalls" {
            let result = incoming_calls_result(&root, &open, &body);
            write_result(&mut writer, id, &result)?;
        } else if method == "callHierarchy/outgoingCalls" {
            let result = outgoing_calls_result(&root, &open, &body);
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

fn document_symbol_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "[]".into();
    };
    match symbols_project(root, open, &path) {
        Ok(syms) if !syms.is_empty() => {
            let src = overlay_text(open, &path);
            let parts: Vec<String> = syms.iter().map(|s| encode_symbol(s, &src)).collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn workspace_symbol_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let query = json_string_field(body, "query").unwrap_or_default();
    match workspace_symbols_project(root, open, &query) {
        Ok(syms) if !syms.is_empty() => {
            let parts: Vec<String> = syms
                .iter()
                .map(|(path, s, sl, sc, el, ec)| {
                    encode_workspace_symbol(&file_uri(path), s, *sl, *sc, *el, *ec)
                })
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn encode_workspace_symbol(
    uri: &str,
    s: &crate::symbols::WorkspaceSymbol,
    sl: u32,
    sc: u32,
    el: u32,
    ec: u32,
) -> String {
    let loc = encode_location(uri, sl, sc, el, ec);
    match &s.container {
        Some(c) => format!(
            r#"{{"name":{},"kind":{},"location":{},"containerName":{}}}"#,
            json_str(&s.name),
            s.kind,
            loc,
            json_str(c)
        ),
        None => format!(
            r#"{{"name":{},"kind":{},"location":{}}}"#,
            json_str(&s.name),
            s.kind,
            loc
        ),
    }
}

fn signature_help_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "null".into();
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    match signature_help_project(root, open, &path, line, character) {
        Ok(Some(h)) => {
            let params: Vec<String> = h
                .parameters
                .iter()
                .map(|p| format!(r#"{{"label":{}}}"#, json_str(p)))
                .collect();
            format!(
                r#"{{"signatures":[{{"label":{},"parameters":[{}]}}],"activeSignature":0,"activeParameter":{}}}"#,
                json_str(&h.label),
                params.join(","),
                h.active_parameter
            )
        }
        _ => "null".into(),
    }
}

fn overlay_text(open: &BTreeMap<PathBuf, String>, path: &Path) -> String {
    let key = canonicalize_source_path(path);
    if let Some(t) = open.get(path).or_else(|| {
        open.iter()
            .find(|(p, _)| canonicalize_source_path(p) == key)
            .map(|(_, t)| t)
    }) {
        return t.clone();
    }
    fs::read_to_string(path).unwrap_or_default()
}

fn encode_symbol(s: &crate::symbols::DocSymbol, src: &str) -> String {
    let children: Vec<String> = s.children.iter().map(|c| encode_symbol(c, src)).collect();
    format!(
        r#"{{"name":{},"kind":{},"range":{},"selectionRange":{},"children":[{}]}}"#,
        json_str(&s.name),
        s.kind,
        encode_range(src, s.range_start, s.range_end),
        encode_range(src, s.sel_start, s.sel_end),
        children.join(",")
    )
}

fn encode_range(src: &str, start: usize, end: usize) -> String {
    let (sl, sc) = crate::span::offset_to_utf16_pos(src, start);
    let (el, ec) = crate::span::offset_to_utf16_pos(src, end);
    format!(
        r#"{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}}"#
    )
}

fn encode_location(uri: &str, sl: u32, sc: u32, el: u32, ec: u32) -> String {
    format!(
        r#"{{"uri":{},"range":{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}}}}"#,
        json_str(uri)
    )
}

fn references_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "[]".into();
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    let include_declaration = json_bool_field(body, "includeDeclaration").unwrap_or(true);
    match references_project(root, open, &path, line, character, include_declaration) {
        Ok(locs) => {
            let parts: Vec<String> = locs
                .iter()
                .map(|(dest, sl, sc, el, ec)| encode_location(&file_uri(dest), *sl, *sc, *el, *ec))
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn prepare_rename_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "null".into();
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    match prepare_rename_project(root, open, &path, line, character) {
        Ok(Some((sl, sc, el, ec, name))) => format!(
            r#"{{"range":{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}},"placeholder":{}}}"#,
            json_str(&name)
        ),
        _ => "null".into(),
    }
}

fn rename_result(
    root: &Path,
    open: &BTreeMap<PathBuf, String>,
    body: &str,
) -> std::result::Result<String, String> {
    let Some(path) = doc_path_from_message(body) else {
        return Ok("null".into());
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    let new_name = json_string_field(body, "newName").unwrap_or_default();
    match rename_project(root, open, &path, line, character, &new_name) {
        Ok(RenameResult::BadName) => Err(format!("invalid rename: {new_name}")),
        Ok(RenameResult::Unavailable) => Ok("null".into()),
        Ok(RenameResult::Edits(edits)) => Ok(encode_workspace_edit(&edits, &new_name)),
        Err(_) => Ok("null".into()),
    }
}

fn encode_workspace_edit(edits: &[(PathBuf, u32, u32, u32, u32)], new_text: &str) -> String {
    let mut by_uri: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for (dest, sl, sc, el, ec) in edits {
        by_uri.entry(file_uri(dest)).or_default().push(format!(
            r#"{{"range":{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}},"newText":{}}}"#,
            json_str(new_text)
        ));
    }
    let parts: Vec<String> = by_uri
        .into_iter()
        .map(|(uri, items)| format!("{}:[{}]", json_str(&uri), items.join(",")))
        .collect();
    format!(r#"{{"changes":{{{}}}}}"#, parts.join(","))
}

fn document_highlight_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "[]".into();
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    match highlights_project(root, open, &path, line, character) {
        Ok(hits) => {
            let parts: Vec<String> = hits
                .iter()
                .map(|(sl, sc, el, ec, kind)| {
                    format!(
                        r#"{{"range":{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}},"kind":{kind}}}"#
                    )
                })
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn folding_range_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "[]".into();
    };
    match folding_ranges_project(root, open, &path) {
        Ok(folds) => {
            let parts: Vec<String> = folds
                .iter()
                .map(|(sl, sc, el, ec)| {
                    format!(
                        r#"{{"startLine":{sl},"startCharacter":{sc},"endLine":{el},"endCharacter":{ec},"kind":{}}}"#,
                        json_str(FOLD_REGION)
                    )
                })
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn formatting_result(open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "null".into();
    };
    let text = overlay_text(open, &path);
    match crate::format::format_source(&text) {
        Ok(formatted) if formatted == text => "[]".into(),
        Ok(formatted) => {
            let (el, ec) = crate::span::offset_to_utf16_pos(&text, text.len());
            format!(
                r#"[{{"range":{{"start":{{"line":0,"character":0}},"end":{{"line":{el},"character":{ec}}}}},"newText":{}}}]"#,
                json_str(&formatted)
            )
        }
        Err(_) => "null".into(),
    }
}

fn selection_range_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "[]".into();
    };
    let positions = json_positions(body);
    match selection_ranges_project(root, open, &path, &positions) {
        Ok(all) => {
            let parts: Vec<String> = all
                .iter()
                .filter_map(|ranges| encode_selection_chain(ranges))
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn inlay_hint_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "[]".into();
    };
    let range = json_optional_range(body);
    match inlay_hints_project(root, open, &path, range) {
        Ok(hints) => {
            let parts: Vec<String> = hints
                .iter()
                .map(|(line, character, label, kind)| {
                    format!(
                        r#"{{"position":{{"line":{line},"character":{character}}},"label":{},"kind":{kind},"paddingRight":true}}"#,
                        json_str(label)
                    )
                })
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn semantic_tokens_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return r#"{"data":[]}"#.into();
    };
    match semantic_tokens_project(root, open, &path) {
        Ok(data) => {
            let nums: Vec<String> = data.iter().map(|n| n.to_string()).collect();
            format!(r#"{{"data":[{}]}}"#, nums.join(","))
        }
        _ => r#"{"data":[]}"#.into(),
    }
}

fn code_action_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "[]".into();
    };
    let range = json_optional_range(body);
    let only = json_only(body);
    match code_actions_project(root, open, &path, range, &only) {
        Ok(acts) => {
            let uri = file_uri(&path);
            let parts: Vec<String> = acts
                .iter()
                .map(|(title, kind, sl, sc, el, ec, new_text, preferred)| {
                    encode_code_action(&uri, title, kind, *sl, *sc, *el, *ec, new_text, *preferred)
                })
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn encode_code_action(
    uri: &str,
    title: &str,
    kind: &str,
    sl: u32,
    sc: u32,
    el: u32,
    ec: u32,
    new_text: &str,
    preferred: bool,
) -> String {
    let pref = if preferred { "true" } else { "false" };
    let text_edit = format!(
        r#"{{"range":{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}},"newText":{}}}"#,
        json_str(new_text)
    );
    let changes = format!("{}:[{}]", json_str(uri), text_edit);
    format!(
        r#"{{"title":{},"kind":{},"isPreferred":{pref},"edit":{{"changes":{{{changes}}}}}}}"#,
        json_str(title),
        json_str(kind)
    )
}

fn encode_call_item(item: &CallItemLsp) -> String {
    format!(
        r#"{{"name":{},"kind":{},"uri":{},"range":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}},"selectionRange":{{"start":{{"line":{},"character":{}}},"end":{{"line":{},"character":{}}}}}}}"#,
        json_str(&item.name),
        crate::hierarchy::KIND_FN,
        json_str(&file_uri(&item.path)),
        item.sl,
        item.sc,
        item.el,
        item.ec,
        item.ssl,
        item.ssc,
        item.sel,
        item.sec
    )
}

fn encode_from_ranges(ranges: &[(u32, u32, u32, u32)]) -> String {
    let parts: Vec<String> = ranges
        .iter()
        .map(|(sl, sc, el, ec)| {
            format!(
                r#"{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}}"#
            )
        })
        .collect();
    parts.join(",")
}

fn prepare_call_hierarchy_result(
    root: &Path,
    open: &BTreeMap<PathBuf, String>,
    body: &str,
) -> String {
    let Some(path) = doc_path_from_message(body) else {
        return "null".into();
    };
    let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
    let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
    match prepare_call_hierarchy_project(root, open, &path, line, character) {
        Ok(Some(item)) => format!("[{}]", encode_call_item(&item)),
        _ => "null".into(),
    }
}

fn item_position(body: &str) -> Option<(PathBuf, u32, u32)> {
    let item = json_object_field(body, "item")?;
    let uri = json_string_field(item, "uri")?;
    let sel = json_object_field(item, "selectionRange")?;
    let start = json_object_field(sel, "start")?;
    let line = json_i64_field(start, "line")?.max(0) as u32;
    let character = json_i64_field(start, "character")?.max(0) as u32;
    Some((
        canonicalize_source_path(&uri_to_path(&uri)),
        line,
        character,
    ))
}

fn incoming_calls_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some((path, line, character)) = item_position(body) else {
        return "[]".into();
    };
    match incoming_calls_project(root, open, &path, line, character) {
        Ok(calls) => {
            let parts: Vec<String> = calls
                .iter()
                .map(|(from, ranges)| {
                    format!(
                        r#"{{"from":{},"fromRanges":[{}]}}"#,
                        encode_call_item(from),
                        encode_from_ranges(ranges)
                    )
                })
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn outgoing_calls_result(root: &Path, open: &BTreeMap<PathBuf, String>, body: &str) -> String {
    let Some((path, line, character)) = item_position(body) else {
        return "[]".into();
    };
    match outgoing_calls_project(root, open, &path, line, character) {
        Ok(calls) => {
            let parts: Vec<String> = calls
                .iter()
                .map(|(to, ranges)| {
                    format!(
                        r#"{{"to":{},"fromRanges":[{}]}}"#,
                        encode_call_item(to),
                        encode_from_ranges(ranges)
                    )
                })
                .collect();
            format!("[{}]", parts.join(","))
        }
        _ => "[]".into(),
    }
}

fn encode_selection_chain(ranges: &[(u32, u32, u32, u32)]) -> Option<String> {
    if ranges.is_empty() {
        return None;
    }
    let mut acc: Option<String> = None;
    for (sl, sc, el, ec) in ranges.iter().rev() {
        let range = format!(
            r#"{{"start":{{"line":{sl},"character":{sc}}},"end":{{"line":{el},"character":{ec}}}}}"#
        );
        acc = Some(match acc {
            None => format!(r#"{{"range":{range}}}"#),
            Some(parent) => format!(r#"{{"range":{range},"parent":{parent}}}"#),
        });
    }
    acc
}

fn json_positions(body: &str) -> Vec<(u32, u32)> {
    let mut out = Vec::new();
    let Some(i) = body.find("\"positions\"") else {
        let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
        let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
        return vec![(line, character)];
    };
    let mut rest = &body[i..];
    while let Some(li) = rest.find("\"line\"") {
        rest = &rest[li + 6..];
        let Some(colon) = rest.find(':') else {
            break;
        };
        rest = rest[colon + 1..].trim_start();
        let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
        let Ok(line) = digits.parse::<u32>() else {
            break;
        };
        let Some(ci) = rest.find("\"character\"") else {
            break;
        };
        rest = &rest[ci + 11..];
        let Some(colon) = rest.find(':') else {
            break;
        };
        rest = rest[colon + 1..].trim_start();
        let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
        let Ok(character) = digits.parse::<u32>() else {
            break;
        };
        out.push((line, character));
    }
    if out.is_empty() {
        let line = json_i64_field(body, "line").unwrap_or(0).max(0) as u32;
        let character = json_i64_field(body, "character").unwrap_or(0).max(0) as u32;
        vec![(line, character)]
    } else {
        out
    }
}

fn json_optional_range(body: &str) -> Option<((u32, u32), (u32, u32))> {
    let i = body.find("\"range\"")?;
    let mut rest = &body[i..];
    let mut pairs = Vec::new();
    while pairs.len() < 2 {
        let Some(li) = rest.find("\"line\"") else {
            break;
        };
        rest = &rest[li + 6..];
        let Some(colon) = rest.find(':') else {
            break;
        };
        rest = rest[colon + 1..].trim_start();
        let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
        let Ok(line) = digits.parse::<u32>() else {
            break;
        };
        let Some(ci) = rest.find("\"character\"") else {
            break;
        };
        rest = &rest[ci + 11..];
        let Some(colon) = rest.find(':') else {
            break;
        };
        rest = rest[colon + 1..].trim_start();
        let digits: String = rest.chars().take_while(|c| c.is_ascii_digit()).collect();
        let Ok(character) = digits.parse::<u32>() else {
            break;
        };
        pairs.push((line, character));
    }
    if pairs.len() == 2 {
        Some((pairs[0], pairs[1]))
    } else {
        None
    }
}

fn json_only(body: &str) -> Vec<String> {
    let Some(i) = body.find("\"only\"") else {
        return Vec::new();
    };
    let rest = &body[i + 6..];
    let Some(lb) = rest.find('[') else {
        return Vec::new();
    };
    let after = &rest[lb + 1..];
    let Some(rb) = after.find(']') else {
        return Vec::new();
    };
    let mut inner = &after[..rb];
    let mut out = Vec::new();
    while let Some(q) = inner.find('"') {
        inner = &inner[q + 1..];
        let mut s = String::new();
        let mut chars = inner.chars();
        let mut closed = false;
        while let Some(c) = chars.next() {
            if c == '"' {
                closed = true;
                break;
            }
            if c == '\\' {
                if let Some(n) = chars.next() {
                    s.push(n);
                }
            } else {
                s.push(c);
            }
        }
        if closed {
            out.push(s);
        }
        inner = chars.as_str();
    }
    out
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

fn json_object_field<'a>(body: &'a str, key: &str) -> Option<&'a str> {
    let pat = format!("\"{key}\"");
    let i = body.find(&pat)?;
    let rest = body[i + pat.len()..].trim_start().strip_prefix(':')?;
    let rest = rest.trim_start();
    if !rest.starts_with('{') {
        return None;
    }
    let end = match_json_object(rest)?;
    Some(&rest[..=end])
}

fn match_json_object(s: &str) -> Option<usize> {
    let bytes = s.as_bytes();
    let mut depth = 0i32;
    let mut in_str = false;
    let mut i = 0;
    while i < bytes.len() {
        let c = bytes[i];
        if in_str {
            if c == b'\\' && i + 1 < bytes.len() {
                i += 2;
                continue;
            }
            if c == b'"' {
                in_str = false;
            }
            i += 1;
            continue;
        }
        match c {
            b'"' => in_str = true,
            b'{' => depth += 1,
            b'}' => {
                depth -= 1;
                if depth == 0 {
                    return Some(i);
                }
            }
            _ => {}
        }
        i += 1;
    }
    None
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

fn json_bool_field(body: &str, key: &str) -> Option<bool> {
    let pat = format!("\"{key}\"");
    let i = body.find(&pat)?;
    let rest = body[i + pat.len()..].trim_start().strip_prefix(':')?;
    let rest = rest.trim_start();
    if rest.starts_with("true") {
        Some(true)
    } else if rest.starts_with("false") {
        Some(false)
    } else {
        None
    }
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
    fn lsp_document_symbol_lists_add_and_main() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_sym\"\nversion = \"0.0.0\"\n",
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
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":6,"method":"textDocument/documentSymbol","params":{{"textDocument":{{"uri":{}}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("documentSymbolProvider"), "{text}");
        assert!(text.contains("\"id\":6"), "{text}");
        assert!(text.contains("\"name\":\"add\""), "{text}");
        assert!(text.contains("\"name\":\"main\""), "{text}");
    }

    #[test]
    fn lsp_references_finds_add_decl_and_call() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_refs\"\nversion = \"0.0.0\"\n",
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
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":7,"method":"textDocument/references","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}},"context":{{"includeDeclaration":true}}}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("referencesProvider"), "{text}");
        assert!(text.contains("\"id\":7"), "{text}");
        let hits = text.matches("\"line\":").count();
        assert!(hits >= 2, "expected decl and call ranges: {text}");
    }

    fn write_add_pkg(root: &Path) -> (String, String, String) {
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_rename\"\nversion = \"0.0.0\"\n",
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
        (formatted, root_uri, main_uri)
    }

    #[test]
    fn lsp_rename_rewrites_add_to_sum() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        let (formatted, root_uri, main_uri) = write_add_pkg(root);
        let call = formatted.rfind("add").unwrap();
        let (line, col) = crate::span::offset_to_utf16_pos(&formatted, call);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let prepare = format!(
            r#"{{"jsonrpc":"2.0","id":8,"method":"textDocument/prepareRename","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}}}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let rename = format!(
            r#"{{"jsonrpc":"2.0","id":9,"method":"textDocument/rename","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}},"newName":"sum"}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&prepare));
        input.extend(frame(&rename));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("renameProvider"), "{text}");
        assert!(text.contains("prepareProvider"), "{text}");
        assert!(text.contains("\"id\":8"), "{text}");
        assert!(text.contains("\"placeholder\":\"add\""), "{text}");
        assert!(text.contains("\"id\":9"), "{text}");
        assert!(text.contains("\"newText\":\"sum\""), "{text}");
        let hits = text.matches("\"newText\":\"sum\"").count();
        assert!(hits >= 2, "expected def and call edits: {text}");
    }

    #[test]
    fn lsp_rename_rejects_keyword_and_skips_kit() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        let (formatted, root_uri, main_uri) = write_add_pkg(root);
        let call = formatted.rfind("add").unwrap();
        let (line, col) = crate::span::offset_to_utf16_pos(&formatted, call);
        let kit = formatted.find("println").unwrap();
        let (kline, kcol) = crate::span::offset_to_utf16_pos(&formatted, kit);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let bad = format!(
            r#"{{"jsonrpc":"2.0","id":10,"method":"textDocument/rename","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}},"newName":"if"}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let kit_req = format!(
            r#"{{"jsonrpc":"2.0","id":11,"method":"textDocument/rename","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}},"newName":"log"}}}}"#,
            json_str(&main_uri),
            kline,
            kcol
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&bad));
        input.extend(frame(&kit_req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("\"id\":10"), "{text}");
        assert!(text.contains("-32602"), "{text}");
        assert!(text.contains("invalid rename: if"), "{text}");
        assert!(text.contains("\"id\":11"), "{text}");
        assert!(text.contains(r#""id":11,"result":null"#), "{text}");
    }

    #[test]
    fn lsp_workspace_symbol_finds_add_and_helper() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        let (_formatted, root_uri, _main_uri) = write_add_pkg(root);
        let helper = crate::format::format_source("def helper(n: Int): Int = n\n").unwrap();
        fs::write(root.join("src/Util.scuzz"), &helper).unwrap();
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let add_q =
            r#"{"jsonrpc":"2.0","id":12,"method":"workspace/symbol","params":{"query":"add"}}"#;
        let help_q =
            r#"{"jsonrpc":"2.0","id":13,"method":"workspace/symbol","params":{"query":"HELP"}}"#;
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(add_q));
        input.extend(frame(help_q));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("workspaceSymbolProvider"), "{text}");
        assert!(text.contains("\"id\":12"), "{text}");
        assert!(text.contains("\"name\":\"add\""), "{text}");
        assert!(text.contains("\"id\":13"), "{text}");
        assert!(text.contains("\"name\":\"helper\""), "{text}");
    }

    #[test]
    fn lsp_signature_help_on_add_and_println() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        let (formatted, root_uri, main_uri) = write_add_pkg(root);
        let add_at = formatted.find("add(").unwrap() + 4;
        let (line, col) = crate::span::offset_to_utf16_pos(&formatted, add_at);
        let kit_at = formatted.find("println(").unwrap() + 8;
        let (kline, kcol) = crate::span::offset_to_utf16_pos(&formatted, kit_at);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let add_help = format!(
            r#"{{"jsonrpc":"2.0","id":14,"method":"textDocument/signatureHelp","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}}}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let kit_help = format!(
            r#"{{"jsonrpc":"2.0","id":15,"method":"textDocument/signatureHelp","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}}}}}}"#,
            json_str(&main_uri),
            kline,
            kcol
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&add_help));
        input.extend(frame(&kit_help));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("signatureHelpProvider"), "{text}");
        assert!(text.contains("\"id\":14"), "{text}");
        assert!(text.contains("def add(n: Int): Int"), "{text}");
        assert!(text.contains("\"id\":15"), "{text}");
        assert!(text.contains("IO.println(s: String): IO[Unit]"), "{text}");
    }

    #[test]
    fn lsp_document_highlight_marks_add_write_and_read() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        let (formatted, root_uri, main_uri) = write_add_pkg(root);
        let call = formatted.rfind("add").unwrap();
        let (line, col) = crate::span::offset_to_utf16_pos(&formatted, call);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":16,"method":"textDocument/documentHighlight","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{},"character":{}}}}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("documentHighlightProvider"), "{text}");
        assert!(text.contains("\"id\":16"), "{text}");
        assert!(text.contains("\"kind\":3"), "{text}");
        assert!(text.contains("\"kind\":2"), "{text}");
    }

    #[test]
    fn lsp_folding_range_covers_for_block() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_fold\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = r#"@main def main: IO[Unit] =
  for {
    _ <- IO.println("x")
  } yield ()
"#;
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), &formatted).unwrap();
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":17,"method":"textDocument/foldingRange","params":{{"textDocument":{{"uri":{}}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("foldingRangeProvider"), "{text}");
        assert!(text.contains("\"id\":17"), "{text}");
        assert!(text.contains("\"kind\":\"region\""), "{text}");
        assert!(text.contains("\"startLine\":"), "{text}");
    }

    #[test]
    fn lsp_format_rewrites_dense_def() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_fmt\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "def add(n:Int):Int=n\n@main def main: IO[Unit] = IO.println(\"x\")\n";
        fs::write(root.join("src/Main.scuzz"), src).unwrap();
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":18,"method":"textDocument/formatting","params":{{"textDocument":{{"uri":{}}},"options":{{"tabSize":2,"insertSpaces":true}}}}}}"#,
            json_str(&main_uri)
        );
        let range_req = format!(
            r#"{{"jsonrpc":"2.0","id":19,"method":"textDocument/rangeFormatting","params":{{"textDocument":{{"uri":{}}},"range":{{"start":{{"line":0,"character":0}},"end":{{"line":0,"character":8}}}},"options":{{"tabSize":2,"insertSpaces":true}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(&range_req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("documentFormattingProvider"), "{text}");
        assert!(text.contains("documentRangeFormattingProvider"), "{text}");
        assert!(text.contains("\"id\":18"), "{text}");
        assert!(text.contains("n: Int"), "{text}");
        assert!(text.contains("\"id\":19"), "{text}");
        assert!(text.contains("newText"), "{text}");
    }

    #[test]
    fn lsp_format_null_on_parse_error() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        write_ok_pkg(root);
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let open = format!(
            r#"{{"jsonrpc":"2.0","method":"textDocument/didOpen","params":{{"textDocument":{{"uri":{},"languageId":"scuzz","version":1,"text":{}}}}}}}"#,
            json_str(&main_uri),
            json_str("this is not scuzz")
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":20,"method":"textDocument/formatting","params":{{"textDocument":{{"uri":{}}},"options":{{"tabSize":2,"insertSpaces":true}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&open));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains(r#""id":20,"result":null"#), "{text}");
    }

    #[test]
    fn lsp_selection_range_nests_add_call() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        let (formatted, root_uri, main_uri) = write_add_pkg(root);
        let call = formatted.rfind("add").unwrap();
        let (line, col) = crate::span::offset_to_utf16_pos(&formatted, call);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":21,"method":"textDocument/selectionRange","params":{{"textDocument":{{"uri":{}}},"positions":[{{"line":{},"character":{}}}]}}}}"#,
            json_str(&main_uri),
            line,
            col
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("selectionRangeProvider"), "{text}");
        assert!(text.contains("\"id\":21"), "{text}");
        assert!(text.contains("\"parent\""), "{text}");
    }

    #[test]
    fn lsp_inlay_hint_names_add_and_println_args() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        let (_formatted, root_uri, main_uri) = write_add_pkg(root);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":22,"method":"textDocument/inlayHint","params":{{"textDocument":{{"uri":{}}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("inlayHintProvider"), "{text}");
        assert!(text.contains("\"id\":22"), "{text}");
        assert!(text.contains("\"n:\""), "{text}");
        assert!(text.contains("\"s:\""), "{text}");
        assert!(text.contains("\"kind\":2"), "{text}");
    }

    #[test]
    fn lsp_semantic_tokens_full_marks_def() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        let (_formatted, root_uri, main_uri) = write_add_pkg(root);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":23,"method":"textDocument/semanticTokens/full","params":{{"textDocument":{{"uri":{}}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("semanticTokensProvider"), "{text}");
        assert!(text.contains("\"id\":23"), "{text}");
        assert!(text.contains("\"data\":["), "{text}");
        assert!(text.contains("tokenTypes"), "{text}");
        assert!(text.contains("\"keyword\""), "{text}");
    }

    #[test]
    fn lsp_code_action_fills_missing_match() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_action\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = r#"enum Color:
  case Red
  case Blue

@main def main: IO[Unit] =
  Color.Red match {
    case Color.Red => IO.println("r")
  }
"#;
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), &formatted).unwrap();
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let match_at = formatted.find("match").unwrap();
        let (line, col) = crate::span::offset_to_utf16_pos(&formatted, match_at);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":24,"method":"textDocument/codeAction","params":{{"textDocument":{{"uri":{}}},"range":{{"start":{{"line":{line},"character":{col}}},"end":{{"line":{line},"character":{col}}}}},"context":{{"diagnostics":[],"only":["quickfix"]}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("codeActionProvider"), "{text}");
        assert!(text.contains("\"id\":24"), "{text}");
        assert!(text.contains("Fill missing match cases"), "{text}");
        assert!(text.contains("Color.Blue"), "{text}");
        assert!(text.contains("quickfix"), "{text}");
    }

    #[test]
    fn lsp_call_hierarchy_prepare_incoming_outgoing() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_hier\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = r#"def add(n: Int): Int =
  n

def twice(n: Int): Int =
  add(n)

@main def main: IO[Unit] =
  IO.println(Str.fromInt(twice(1)))
"#;
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), &formatted).unwrap();
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let twice_call = formatted.rfind("twice").unwrap();
        let (tline, tcol) = crate::span::offset_to_utf16_pos(&formatted, twice_call);
        let add_def = formatted.find("add").unwrap();
        let (aline, acol) = crate::span::offset_to_utf16_pos(&formatted, add_def);
        let twice_def = formatted.find("twice").unwrap();
        let (dline, dcol) = crate::span::offset_to_utf16_pos(&formatted, twice_def);
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let prep = format!(
            r#"{{"jsonrpc":"2.0","id":27,"method":"textDocument/prepareCallHierarchy","params":{{"textDocument":{{"uri":{}}},"position":{{"line":{tline},"character":{tcol}}}}}}}"#,
            json_str(&main_uri)
        );
        let add_item = format!(
            r#"{{"name":"add","kind":12,"uri":{uri},"range":{{"start":{{"line":{aline},"character":{acol}}},"end":{{"line":{aline},"character":{acol}}}}},"selectionRange":{{"start":{{"line":{aline},"character":{acol}}},"end":{{"line":{aline},"character":{acol}}}}}}}"#,
            uri = json_str(&main_uri)
        );
        let twice_item = format!(
            r#"{{"name":"twice","kind":12,"uri":{uri},"range":{{"start":{{"line":{dline},"character":{dcol}}},"end":{{"line":{dline},"character":{dcol}}}}},"selectionRange":{{"start":{{"line":{dline},"character":{dcol}}},"end":{{"line":{dline},"character":{dcol}}}}}}}"#,
            uri = json_str(&main_uri)
        );
        let incoming = format!(
            r#"{{"jsonrpc":"2.0","id":28,"method":"callHierarchy/incomingCalls","params":{{"item":{add_item}}}}}"#
        );
        let outgoing = format!(
            r#"{{"jsonrpc":"2.0","id":29,"method":"callHierarchy/outgoingCalls","params":{{"item":{twice_item}}}}}"#
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&prep));
        input.extend(frame(&incoming));
        input.extend(frame(&outgoing));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("callHierarchyProvider"), "{text}");
        assert!(text.contains("\"id\":27"), "{text}");
        assert!(text.contains("\"name\":\"twice\""), "{text}");
        assert!(text.contains("\"id\":28"), "{text}");
        assert!(text.contains("\"from\""), "{text}");
        assert!(text.contains("\"id\":29"), "{text}");
        assert!(text.contains("\"to\""), "{text}");
        assert!(text.contains("\"name\":\"add\""), "{text}");
    }

    #[test]
    fn lsp_code_action_formats_document() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_fmt\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = "@main def main: IO[Unit] = IO.println(\"ok\")\n";
        fs::write(root.join("src/Main.scuzz"), src).unwrap();
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":25,"method":"textDocument/codeAction","params":{{"textDocument":{{"uri":{}}},"range":{{"start":{{"line":0,"character":0}},"end":{{"line":0,"character":8}}}},"context":{{"diagnostics":[],"only":["source"]}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("\"id\":25"), "{text}");
        assert!(text.contains("Format document"), "{text}");
        assert!(text.contains("source.formatDocument"), "{text}");
    }

    #[test]
    fn lsp_code_action_fixes_unknown_callee() {
        let dir = tempdir().unwrap();
        let root = dir.path();
        fs::write(
            root.join("scuzz.toml"),
            "[package]\nname = \"lsp_fix\"\nversion = \"0.0.0\"\n",
        )
        .unwrap();
        fs::create_dir_all(root.join("src")).unwrap();
        let src = r#"@main def main: IO[Unit] =
  IO.println(if (Str.contain("ab", "b")) "y" else "n")
"#;
        let formatted = crate::format::format_source(src).unwrap();
        fs::write(root.join("src/Main.scuzz"), &formatted).unwrap();
        let root_uri = format!("file://{}", fs::canonicalize(root).unwrap().display());
        let main = canonicalize_source_path(&root.join("src/Main.scuzz"));
        let main_uri = format!("file://{}", main.display());
        let init = format!(
            r#"{{"jsonrpc":"2.0","id":1,"method":"initialize","params":{{"rootUri":"{root_uri}","capabilities":{{}}}}}}"#
        );
        let req = format!(
            r#"{{"jsonrpc":"2.0","id":26,"method":"textDocument/codeAction","params":{{"textDocument":{{"uri":{}}},"range":{{"start":{{"line":0,"character":0}},"end":{{"line":10,"character":0}}}},"context":{{"diagnostics":[],"only":["quickfix"]}}}}}}"#,
            json_str(&main_uri)
        );
        let mut input = Vec::new();
        input.extend(frame(&init));
        input.extend(frame(&req));
        input.extend(frame(r#"{"jsonrpc":"2.0","id":2,"method":"shutdown"}"#));
        input.extend(frame(r#"{"jsonrpc":"2.0","method":"exit"}"#));
        let mut out = Vec::new();
        run_lsp_io(root, Cursor::new(input), &mut out).unwrap();
        let text = String::from_utf8(out).unwrap();
        assert!(text.contains("\"id\":26"), "{text}");
        assert!(text.contains("Str.contains"), "{text}");
        assert!(text.contains("Str.contain"), "{text}");
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
