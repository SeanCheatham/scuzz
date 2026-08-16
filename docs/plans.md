# Short-term plan

## Slice: Last-use release for mixed owned / borrowed match arms

An `if` phi retains a borrowed arm when the other arm is owned. Match still marks the phi owned only when every arm produces an owned ptr. `xs match { case A => xs; case B => [1] }` stays allocated when the borrowed arm is taken.

- Retain each borrowed match arm at its join when any arm is owned, then mark the phi owned.
- Proof: compiler IR for a match that yields a bound list or a fresh list shows `sz_retain` of the bound list and `sz_release` of the phi after `List.len`.
- Out of scope: cycle collector, OS threads, device packaging.
