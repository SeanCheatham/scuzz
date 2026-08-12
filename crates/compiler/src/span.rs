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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn line_col_first_byte() {
        assert_eq!(offset_to_line_col("abc", 0), (1, 1));
    }

    #[test]
    fn line_col_second_line() {
        let src = "a\nbc";
        assert_eq!(offset_to_line_col(src, 2), (2, 1));
        assert_eq!(offset_to_line_col(src, 3), (2, 2));
    }
}
