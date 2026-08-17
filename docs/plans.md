# Short-term plan

## Slice: Drop owned acquire IO after Resource.make

`Resource.use` drops an owned resource after the call. `Resource.make` stores the acquire IO without retain, so the acquire graph can alias the use flatMap.

- Retain the acquire IO in `Resource.make`.
- Drop an owned acquire IO after the call.
- Release acquire when the resource drops.
- Proof: compiler IR for `Resource.make(IO.pure("tok"), t => IO.println(t))` shows `sz_release` of the acquire IO after the call.
- Out of scope: OS threads.
