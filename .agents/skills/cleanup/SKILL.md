---
name: cleanup
description: Housekeeping and cleanup on the repository.
---

# Cleanup

## When to Use
- After making significant codebase changes that may impact dependents or dependencies of the changed code
- Whenever code changes that could impact documentation
- When requested by the user for broad-scale repository cleanup or targeted cleanup

## Instructions
- Remove unused or vestigial code
- Keep code as concise and simple as possible
- Don't be verbose with implementations
- Prefer brevity in comments and documentation
- Embrace the "KISS" and "YAGNI" principles
- Re-organize and refactor code as needed, especially if it simplifies things
- Remove backwards-compatibility and legacy-code/shims unless explicitly requested by the user
