# Next slice

Bind one unbound type parameter to one concrete type. Rank: [`gaps.md`](gaps.md). Locks: [`philosophy.md`](philosophy.md). Do not add a kit-call pin. Pins that already exist stay. `eqPinned` stays the rule inside a generic def. Parse `Param` and `Fun` stay strings.

## One value, one concrete type

`Type.eq` returns true when either side is `Ty.TVar`. This program checks today. It must fail. `a` is one value. `Str.len` needs `String`. `+` needs `Int`.

```text
def id[A](x: A): A = x

def bad(): Int =
  for {
    xs = List.empty()
    a = id(List.at(xs, 0))
    _ = Str.len(a)
    n = a + 1
  } yield n
```

This program checks today. It must stay green. `a` is `Int` in both uses.

```text
def id[A](x: A): A = x

def ok(): Int =
  for {
    xs = List.empty()
    a = id(List.at(xs, 0))
    n = a + 1
    m = a + 2
  } yield n + m
```

The same rule covers a bare parameter. `def bad(x: A): Int = Str.len(x) + x` must fail. `def ok(x: A): Int = x + x` must stay green. `def id[A](x: A): Int = x` stays an error.

Proof: add the programs to `examples/tyck`. `scuzz fuzz --iterations 0 examples/tyck` stays green. `scuzz check examples/compiler` stays green.
