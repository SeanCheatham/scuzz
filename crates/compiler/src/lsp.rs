//! Minimal LSP server over stdio (Phase 3).
//! Supports initialize, shutdown, textDocument/didOpen|didChange, hover, publishDiagnostics.

use crate::lexer::lex;
use crate::parser::parse;
use crate::typ::typecheck;
use serde_json::{json, Value};
use std::collections::HashMap;
use std::io::{BufRead, BufReader, Write};

pub fn run_stdio() -> anyhow::Result<()> {
    let stdin = std::io::stdin();
    let mut reader = BufReader::new(stdin.lock());
    let mut stdout = std::io::stdout();
    let mut docs: HashMap<String, String> = HashMap::new();

    loop {
        let msg = match read_message(&mut reader)? {
            Some(m) => m,
            None => break,
        };
        let method = msg.get("method").and_then(|m| m.as_str()).unwrap_or("");
        let id = msg.get("id").cloned();

        match method {
            "initialize" => {
                let result = json!({
                    "capabilities": {
                        "textDocumentSync": 1,
                        "hoverProvider": true,
                        "diagnosticProvider": false
                    },
                    "serverInfo": { "name": "scalui-lsp", "version": "0.3.0" }
                });
                write_response(&mut stdout, id, result)?;
            }
            "initialized" | "textDocument/didSave" => {}
            "shutdown" => {
                write_response(&mut stdout, id, Value::Null)?;
            }
            "exit" => break,
            "textDocument/didOpen" => {
                if let Some(params) = msg.get("params") {
                    let uri = params["textDocument"]["uri"].as_str().unwrap_or("").to_string();
                    let text = params["textDocument"]["text"].as_str().unwrap_or("").to_string();
                    docs.insert(uri.clone(), text.clone());
                    publish_diagnostics(&mut stdout, &uri, &text)?;
                }
            }
            "textDocument/didChange" => {
                if let Some(params) = msg.get("params") {
                    let uri = params["textDocument"]["uri"].as_str().unwrap_or("").to_string();
                    if let Some(changes) = params.get("contentChanges").and_then(|c| c.as_array()) {
                        if let Some(last) = changes.last() {
                            if let Some(text) = last.get("text").and_then(|t| t.as_str()) {
                                docs.insert(uri.clone(), text.to_string());
                                publish_diagnostics(&mut stdout, &uri, text)?;
                            }
                        }
                    }
                }
            }
            "textDocument/hover" => {
                let uri = msg["params"]["textDocument"]["uri"]
                    .as_str()
                    .unwrap_or("")
                    .to_string();
                let text = docs.get(&uri).cloned().unwrap_or_default();
                let hover = hover_info(&text);
                write_response(
                    &mut stdout,
                    id,
                    json!({
                        "contents": { "kind": "markdown", "value": hover }
                    }),
                )?;
            }
            _ => {
                if id.is_some() {
                    write_response(&mut stdout, id, Value::Null)?;
                }
            }
        }
    }
    Ok(())
}

fn hover_info(text: &str) -> String {
    if text.contains("Effects.runKit") {
        return "**Effects.runKit** — Phase 3 blessed effects kit demo (`IO[Unit]`)".into();
    }
    if text.contains("IO.println") {
        return "**IO.println** — print a string line (`IO[Unit]`)".into();
    }
    if text.contains("enum ") {
        return "**enum** — nullary ADT (Phase 3 kernel)".into();
    }
    "ScalUI kernel dialect".into()
}

fn publish_diagnostics(out: &mut impl Write, uri: &str, text: &str) -> anyhow::Result<()> {
    let mut diags = Vec::new();
    match lex(text) {
        Ok(_) => match parse(text) {
            Ok(prog) => {
                if !prog.main.name.is_empty() {
                    if let Err(e) = typecheck(&prog) {
                        diags.push(json!({
                            "range": {
                                "start": { "line": 0, "character": 0 },
                                "end": { "line": 0, "character": 1 }
                            },
                            "severity": 1,
                            "source": "scalui",
                            "message": e.to_string()
                        }));
                    }
                }
            }
            Err(e) => {
                diags.push(json!({
                    "range": {
                        "start": { "line": 0, "character": 0 },
                        "end": { "line": 0, "character": 1 }
                    },
                    "severity": 1,
                    "source": "scalui",
                    "message": e.to_string()
                }));
            }
        },
        Err(e) => {
            diags.push(json!({
                "range": {
                    "start": { "line": 0, "character": 0 },
                    "end": { "line": 0, "character": 1 }
                },
                "severity": 1,
                "source": "scalui",
                "message": e.to_string()
            }));
        }
    }
    let note = json!({
        "jsonrpc": "2.0",
        "method": "textDocument/publishDiagnostics",
        "params": { "uri": uri, "diagnostics": diags }
    });
    write_raw(out, &note)?;
    Ok(())
}

fn read_message(reader: &mut impl BufRead) -> anyhow::Result<Option<Value>> {
    let mut content_length: Option<usize> = None;
    loop {
        let mut line = String::new();
        let n = reader.read_line(&mut line)?;
        if n == 0 {
            return Ok(None);
        }
        let trimmed = line.trim_end();
        if trimmed.is_empty() {
            break;
        }
        if let Some(rest) = trimmed.strip_prefix("Content-Length:") {
            content_length = Some(rest.trim().parse()?);
        }
    }
    let len = match content_length {
        Some(l) => l,
        None => return Ok(None),
    };
    let mut buf = vec![0u8; len];
    reader.read_exact(&mut buf)?;
    let v: Value = serde_json::from_slice(&buf)?;
    Ok(Some(v))
}

fn write_response(out: &mut impl Write, id: Option<Value>, result: Value) -> anyhow::Result<()> {
    let msg = json!({
        "jsonrpc": "2.0",
        "id": id,
        "result": result
    });
    write_raw(out, &msg)
}

fn write_raw(out: &mut impl Write, msg: &Value) -> anyhow::Result<()> {
    let body = serde_json::to_vec(msg)?;
    write!(out, "Content-Length: {}\r\n\r\n", body.len())?;
    out.write_all(&body)?;
    out.flush()?;
    Ok(())
}
