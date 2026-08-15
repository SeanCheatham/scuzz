//! Source locations: byte offsets into a named file.

#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Span {
    pub file: String,
    pub start: usize,
    pub end: usize,
}

impl Span {
    pub fn new(file: impl Into<String>, start: usize, end: usize) -> Self {
        Self {
            file: file.into(),
            start,
            end,
        }
    }

    pub fn dummy() -> Self {
        Self::default()
    }

    pub fn cover(self, other: &Span) -> Span {
        Span {
            file: if self.file.is_empty() {
                other.file.clone()
            } else {
                self.file
            },
            start: self.start.min(other.start),
            end: self.end.max(other.end),
        }
    }
}

/// Convert a byte offset into 1-based line and column.
pub fn offset_to_line_col(source: &str, offset: usize) -> (u32, u32) {
    let offset = offset.min(source.len());
    let mut line = 1u32;
    let mut col = 1u32;
    for (i, b) in source.bytes().enumerate() {
        if i == offset {
            return (line, col);
        }
        if b == b'\n' {
            line += 1;
            col = 1;
        } else {
            col += 1;
        }
    }
    (line, col)
}

/// Convert 1-based line and column to a byte offset (column counts bytes, like [`offset_to_line_col`]).
#[cfg(test)]
pub fn line_col_to_offset(source: &str, line: u32, column: u32) -> usize {
    let mut cur_line = 1u32;
    let mut cur_col = 1u32;
    for (i, b) in source.bytes().enumerate() {
        if cur_line == line && cur_col == column {
            return i;
        }
        if b == b'\n' {
            cur_line += 1;
            cur_col = 1;
        } else {
            cur_col += 1;
        }
    }
    source.len()
}

/// Convert a byte offset into a 0-based LSP position (UTF-16 columns).
pub fn offset_to_utf16_pos(source: &str, offset: usize) -> (u32, u32) {
    let offset = offset.min(source.len());
    let mut line = 0u32;
    let mut col = 0u32;
    let mut i = 0usize;
    for ch in source.chars() {
        let len = ch.len_utf8();
        if i + len > offset {
            break;
        }
        i += len;
        if ch == '\n' {
            line += 1;
            col = 0;
        } else {
            col += ch.len_utf16() as u32;
        }
    }
    (line, col)
}

/// Convert a 0-based LSP position (UTF-16 columns) to a byte offset.
pub fn utf16_pos_to_offset(source: &str, line: u32, character: u32) -> usize {
    let mut cur_line = 0u32;
    let mut i = 0usize;
    let mut chars = source.chars();
    while cur_line < line {
        match chars.next() {
            Some('\n') => {
                i += 1;
                cur_line += 1;
            }
            Some(ch) => i += ch.len_utf8(),
            None => return source.len(),
        }
    }
    let mut col = 0u32;
    while col < character {
        match chars.next() {
            Some('\n') | None => break,
            Some(ch) => {
                i += ch.len_utf8();
                col += ch.len_utf16() as u32;
            }
        }
    }
    i.min(source.len())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn line_col_first_byte() {
        assert_eq!(offset_to_line_col("abc", 0), (1, 1));
    }

    #[test]
    fn utf16_pos_counts_multibyte() {
        let src = "a😀b";
        let grin = src.find('😀').unwrap();
        assert_eq!(offset_to_utf16_pos(src, grin), (0, 1));
        assert_eq!(utf16_pos_to_offset(src, 0, 1), grin);
        assert_eq!(utf16_pos_to_offset(src, 0, 3), grin + '😀'.len_utf8());
    }

    #[test]
    fn line_col_second_line() {
        let src = "a\nbc";
        assert_eq!(offset_to_line_col(src, 2), (2, 1));
        assert_eq!(offset_to_line_col(src, 3), (2, 2));
    }

    #[test]
    fn line_col_roundtrip() {
        let src = "ab\ncd";
        let (line, col) = offset_to_line_col(src, 3);
        assert_eq!(line_col_to_offset(src, line, col), 3);
    }
}
