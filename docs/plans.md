# Short-term plan

## Next

**Hot reload — `scuzz watch` still only rebuilds** — `impl Get[T] for Opt` typechecks (`examples/trait`). `[ui]` `run --watch` stamp-reloads the View tree. Next: an IO-only source change picked up without restarting the process, or leave watch as rebuild-only.
