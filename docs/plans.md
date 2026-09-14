# Current slice

Store Param/Fun as `Ty` in the checker env. Parse each Param/Fun string once at bind. Lookup uses the stored `Ty`. Do not re-parse on lookup.

Locks: do not tighten `Type.eq`. Do not add Fun-string special cases. Parse `Param.ty` and `Fun.ret` stay strings. No kit payloads, prepend/Builder, GUI, or table-stakes.

Proof: `scuzz check` plus `examples/tyck`. `def id[A](x: A): Int = x` still checks.
