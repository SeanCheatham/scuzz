# Next slice

`Signal[T]` first step: `Signal.list` over records and `View.each` over the element type.

Today `Signal.list` holds `List[String]` and `View.each` binds String. Studio keeps tasks as encoded strings (`Tasks.parseItem` / `encodeItem` on every read). Direction: one generic cell and `View.each` over the element type. Gap 1: [`gaps.md`](gaps.md). Lock: [`vision.md`](vision.md#signal-string-and-errors).

## Done when

- `Signal.list` holds `List[T]` for a record or enum `T`, not only String. The element type comes from a non-empty literal, a typed def return, or an `e: Signal[List[T]]` pin on an empty literal.
- `View.each(sig, elem => view)` binds the element type. Record field access (`item.label`) and constructor patterns work in the body.
- `Signal.getList` / `Signal.setList` stay typed over the element type.
- A list signal of records dumps `list[N] <name> = <count>` (count only). String lists keep `["a", "b"]`. `Timeline.signalListLen` and `Property.signalListLen` read both shapes. `Property.signalListAt` stays String-only and returns `""` for a record list.
- `examples/studio` keeps tasks as `List[Item]` in the signal. `Tasks.parseItem` / `encodeItem` run only at the Fs load/save boundary. Claims and goldens keep passing on the host baseline (names from the previous slice).

## Out of slice

Generic `Signal.map` over `A => B`, non-list `Signal[T]` record cells, `Signal.set` on lists inside `View.each` lambdas, UTF-8 `String`, typed `E`, LSP spans.
