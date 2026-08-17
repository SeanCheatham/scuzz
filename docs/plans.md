# Short-term plan

## Slice: Release tap capture lists on View free

Stream nodes drop through RC. Tap constructors unpack the wrapper list and leave the capture env on the View without retain or release.

- Retain `tap_env` in View constructors that store it.
- Release `tap_env` in `sz_view_free`.
- Proof: a Headless button with a captured Signal still fires after the factory returns, and `sz_view_free` does not leak the env list under alloc accounting.
- Out of scope: OS threads.
