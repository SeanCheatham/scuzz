# Reactive select options

Use `Signal[List[String]]` for select options.
Replace the copied list when its Signal changes.
Keep the selected index under application control.
Cancel pending popup input when options change.
Keep the control and unrelated fields mounted.
An empty list has no input or Tab stop.
Use new option identities after replacement. Reject stale pointer input.

Extend the select application with list replacement and empty-list recovery.
Check shared input, browser semantics, and exact native and evaluator replay.
Run runtime, sanitizer, code generation, browser, package, and clean UI checks.

Status: Reactive options and retained-state checks pass in the normal, ASan, and UBSan runtime suites. All eight native and evaluator replay cases pass. Five browser modes pass. The CLI rebuild and source campaign pass with the generated-clock fix. Code generation and all 39 companion oracle workloads pass. Complete full web, package, and clean UI checks.
