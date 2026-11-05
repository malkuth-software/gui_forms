# Update scopes and dirtiness

Every mutation in GUI.Forms records exactly what it invalidated, and the
window decides when to act on those records. This is what keeps a retained
toolkit cheap: a text change does not re-lay-out an unrelated panel, and ten
property changes in a row do not cause ten layout passes.

## The dirty vocabulary

`Dirty` is a typed bit set declared in `gui_forms/dirty.hpp`:

| Flag | Means the control needs |
|---|---|
| `measure` | its preferred size recomputed |
| `arrange` | its children repositioned |
| `paint` | its pixels repainted |
| `hit_test` | its hit-test shape rechecked |
| `text` | its text shaped again |
| `style` | its visual recipe re-resolved |
| `resource` | an image or font resource reloaded |
| `semantics` | its semantic (accessibility) node rebuilt |
| `accessibility` | its native accessibility publication refreshed |

`Dirty::layout` is `measure | arrange`. The `invalidation` namespace names the
combinations setters actually use: `invalidation::bounds` is measure, arrange,
paint, hit test, semantics and accessibility; `invalidation::enabled` is paint,
hit test, semantics and accessibility; `invalidation::text_content` adds `text`
to the bounds set; `invalidation::paint_only` is paint alone. Stock setters
pick the narrowest correct set.

## Three ways to invalidate

`invalidate(Dirty)`
: Marks this control and the path to the root. This is what setters call.

`invalidate_subtree(Dirty)`
: Marks every descendant too. Use it when a change to this control changes
  how its children must be measured or painted, such as a font change on a
  container.

`invalidate_declared(Dirty)`
: For custom controls. Pass the effects of the mutation at the call site.
  A nonempty mask is forwarded to `invalidate`; there is no separate property
  declaration against which it is checked. Passing `Dirty::none` increments
  `MetricsSnapshot::undeclared_mutations` for an attached control and throws
  without `NDEBUG`; with `NDEBUG`, it invalidates the conservative subtree set.

Marks are coalesced per control: marking paint twice stores one entry.
`MetricsSnapshot::dirty_marks` and `mutations` both count every call that
marks dirtiness, so they show how much invalidation an interaction caused,
not how much of it was coalesced.

## Update scopes

`Window::begin_update()` returns a move-only `UpdateScope`. While any scope is
open the window records dirtiness, and reading geometry no longer forces a
layout pass: read barriers wait for the scope to close. An explicit
`perform_layout()` still runs, and paint damage is recorded as each change is
made, so the host may be woken; nothing is painted until the host's next
frame. Closing the outermost scope (or letting it go out of scope) runs one
bounded flush. Scopes nest; an inner close does nothing but decrement depth.
Always obtain a scope from `begin_update()`: a default-constructed
`UpdateScope` opens nothing but still closes a scope when it is destroyed.

```cpp
{
    gf::UpdateScope scope = window.begin_update();
    (*status).set_text("Working…");
    (*progress).set_value(0.0);
    (*cancel).enabled = true;
}   // one layout pass, one damage publication
```

Inside a scope you may call `scope.perform_layout()` to force layout early
when you need arranged geometry mid-transaction. Reading arranged or absolute
bounds outside a scope triggers the layout read barrier, which flushes pending
layout before returning; `MetricsSnapshot::read_barrier_flushes` counts those
reads so you can see when a hot loop is accidentally forcing layout.

`begin_init()` / `end_init()` on a control is the related per-control
transaction. During initialization the control's state events coalesce by
event identity and publish once, in first-event order, after the outer
`end_init()`. Entering initialization also revokes the control's focus and
capture and makes it ineligible for routed input until commit. This is the
mechanism behind designer-style staged construction and behind the
`loaded()` ordering described in [Lifecycle](lifecycle).

## Bounded flushes

A flush is not a loop until clean. It runs at most a fixed number of
measure/arrange passes; a layout that keeps dirtying itself hits the pass
limit, the window keeps the last consistent geometry, and
`MetricsSnapshot::bounded_pass_limit_hits` increments. The same bound applies
to callback arbitration: handlers that mutate state during a flush are
retried a bounded number of times (`callback_arbitration_retries`,
`callback_arbitration_limit_hits`). The toolkit never spins; it stops and
reports.

The flush order is fixed: measure dirty controls (top down, preferred sizes
flow up), arrange dirty containers, then convert paint marks into damage
rectangles per paint plane. Hit-test and semantics marks are consumed lazily
by the next hit test or snapshot. Text marks are consumed during measure,
because shaping determines size.

## What a flush produces

After a flush the window holds a `DamageRegion` per paint plane. The host
asks for it with `take_damage()`. The current paint API takes a rectangle,
so a host may paint the region's bounding box, including gaps. If no native host is
attached (tests, headless automation), the damage simply accumulates until
something calls `paint`. Painting is described in
[Painting and damage](painting-and-damage).

## Reading the counters

`Window::metrics_snapshot()` returns a `MetricsSnapshot` with, among others:

- `update_scopes_started`, `update_scope_depth`, `maximum_update_scope_depth`
- `flush_count`, `read_barrier_flushes`, `bounded_pass_limit_hits`
- `measure_passes`, `arrange_passes`, `controls_measured`, `controls_arranged`
- `dirty_marks`, `mutations`, `undeclared_mutations`

`to_json()` serializes the whole snapshot. The `print_metrics_on_close`
window option writes it to the log when the window closes, and the optional
diagnostics panel shows it live. A claim that a change "is cheaper" is made by comparing these
numbers for the same workload, not by inspection.
