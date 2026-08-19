//! Document links from the same parse as `check`. No second typer.

use crate::ast::Program;
use crate::definition::{decl_span, source_for_module, DeclKind};
use crate::resolve::module_id_from_label;

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct DocumentLink {
    pub start: usize,
    pub end: usize,
    pub dest_file: String,
    pub dest_start: usize,
    pub dest_end: usize,
    pub tooltip: String,
}

/// One link per `import Module.name` in `current_file`. Target is the imported def or enum.
pub fn document_links_in_source(
    program: &Program,
    sources: &[(String, String)],
    current_file: &str,
) -> Vec<DocumentLink> {
    let module = module_id_from_label(current_file);
    let mut out = Vec::new();
    for im in &program.imports {
        if im.in_module != module {
            continue;
        }
        if im.span.end <= im.span.start {
            continue;
        }
        let target_name = if im.is_wildcard() {
            program
                .defs
                .iter()
                .find(|d| d.module == im.from_module && !d.is_private)
                .map(|d| d.name.as_str())
                .or_else(|| {
                    program
                        .enums
                        .iter()
                        .find(|e| e.module == im.from_module)
                        .map(|e| e.name.as_str())
                })
        } else {
            Some(im.name.as_str())
        };
        let Some(target_name) = target_name else {
            continue;
        };
        let Some((dest_file, dest_start, dest_end)) =
            import_target(program, sources, &im.from_module, target_name)
        else {
            continue;
        };
        out.push(DocumentLink {
            start: im.span.start,
            end: im.span.end,
            dest_file,
            dest_start,
            dest_end,
            tooltip: format!("{}.{}", im.from_module, im.name),
        });
    }
    out
}

fn import_target(
    program: &Program,
    sources: &[(String, String)],
    from_module: &str,
    name: &str,
) -> Option<(String, usize, usize)> {
    let kind = if program
        .defs
        .iter()
        .any(|d| d.module == from_module && d.name == name)
    {
        DeclKind::Def
    } else if program
        .enums
        .iter()
        .any(|e| e.module == from_module && e.name == name)
    {
        DeclKind::Enum
    } else {
        return None;
    };
    let (file, text) = source_for_module(sources, from_module)?;
    let (start, end) = decl_span(text, kind, name)?;
    Some((file.to_string(), start, end))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::parser::{parse_file, parse_sources};

    #[test]
    fn links_import_to_def() {
        let main = "import A.tag\n@main def main: IO[Unit] =\n  IO.println(tag())\n";
        let a = "def tag(): String =\n  \"a\"\n";
        let sources = vec![
            ("Main.scuzz".into(), main.to_string()),
            ("A.scuzz".into(), a.to_string()),
        ];
        let program = parse_sources(&sources).unwrap();
        let links = document_links_in_source(&program, &sources, "Main.scuzz");
        assert_eq!(links.len(), 1, "{links:?}");
        let l = &links[0];
        assert_eq!(&main[l.start..l.end], "A.tag");
        assert_eq!(l.dest_file, "A.scuzz");
        assert_eq!(&a[l.dest_start..l.dest_end], "tag");
        assert_eq!(l.tooltip, "A.tag");
    }

    #[test]
    fn links_wildcard_to_first_public_def() {
        let main = "import A.*\n@main def main: IO[Unit] =\n  IO.println(tag())\n";
        let a = "def tag(): String =\n  \"a\"\n";
        let sources = vec![
            ("Main.scuzz".into(), main.to_string()),
            ("A.scuzz".into(), a.to_string()),
        ];
        let program = parse_sources(&sources).unwrap();
        let links = document_links_in_source(&program, &sources, "Main.scuzz");
        assert_eq!(links.len(), 1, "{links:?}");
        assert_eq!(&main[links[0].start..links[0].end], "A.*");
        assert_eq!(&a[links[0].dest_start..links[0].dest_end], "tag");
    }

    #[test]
    fn skips_unknown_import() {
        let src = "import Missing.gone\n@main def main: IO[Unit] =\n  IO.println(\"x\")\n";
        let p = parse_file(src, "Main.scuzz").unwrap();
        let sources = vec![("Main.scuzz".into(), src.to_string())];
        let links = document_links_in_source(&p, &sources, "Main.scuzz");
        assert!(links.is_empty(), "{links:?}");
    }
}
