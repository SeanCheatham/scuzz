# Short-term plan

## Slice: Last-use release for mixed owned / borrowed `if` arms

An `if` phi is owned only when both arms produce owned ptrs. `if (b) xs else [1]` stays allocated when the borrowed arm is taken.

- Retain the borrowed arm at the join, then mark the phi owned so a later last-use drops it.
- Proof: compiler IR for `List.len(for { xs = [1] } yield if (false) xs else [2])` shows `sz_retain` of `xs` and `sz_release` of the phi.
- Out of scope: cycle collector, OS threads, device packaging.
