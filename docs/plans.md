# Short-term plan

## Slice: Last-use release for owned `if` / match arm temps

`if` / match join through an unowned phi. An owned temp that is the arm result (`if (true) [1] else [2]`) stays allocated.

- Mark the phi owned when both arms produce owned ptrs, so a later last-use (`List.len`) drops the taken arm.
- Proof: compiler IR for `List.len(if (true) [1] else [2])` shows `sz_release` of the list.
- Out of scope: mixed owned/borrowed arms, cycle collector, OS threads, device packaging.
