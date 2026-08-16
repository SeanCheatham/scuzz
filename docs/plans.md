# Short-term plan

## Slice: Last-use release for `Signal.setStr` inputs

`Signal.str` copies bytes and drops the owned string. `Signal.setStr` still leaves the owned `String`.

- Drop an owned string after `sz_lang_signal_str_set`.
- Proof: compiler IR for `Signal.setStr(s, "a")` shows `sz_release` of the string after the call.
- Out of scope: RC stream nodes, View lambda packs, OS threads.
