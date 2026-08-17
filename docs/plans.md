# Short-term plan

## Slice: Drop owned Resource after last use

Callback capture lists drop when the View, session, stream, resource, or server frees. An owned `Resource` from `Resource.make` has no last-use release.

- Drop an owned Resource after `Resource.use` when it is the last use.
- Proof: compiler IR for `Resource.use(Resource.make(IO.pure("tok"), t => IO.println(t)), t => IO.println(t))` shows `sz_release` of the resource after the call.
- Out of scope: OS threads. `sz_lang_resource_free` already releases the callback env.
