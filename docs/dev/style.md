# Style guide

GUI.Forms sources follow one house style: an orthodox, explicitly spelled
subset of C++20 in which types, ownership, mutation and execution order are
visible at the point of use. The canonical text of the complete style is
[`HOUSE_STYLE.md`](https://github.com/malkuth-software/notes/blob/main/HOUSE_STYLE.md)
on the `main` branch of the project's notes repository. Its sections are
ordered by importance, most important first. Read it in full before writing
or changing code.

Part of the style is checked mechanically. The `house-style` job in
`.github/workflows/native.yml` runs `tools/check_house_style.py` over every
first-party directory and fails on any finding. The scanner ignores
comments and literals and reports `auto`, `->` (pointer member access and
trailing return types), coroutine keywords, `std::ranges` and `std::views`
pipelines, defaulted comparison operators, and bracket sequences that may be
lambdas. `tools/house_policy_check` is a Clang LibTooling checker that applies
the same subset semantically over a build's compilation database; maintainers
run it locally, CI does not (see [Repository tools](tools.md)). Everything
else here, including ownership, lifetimes, failure states and kernel
structure, is checked in review.

## Retained layout

GUI.Forms is retained mode, and so is every control's layout. Layout is
computed when its inputs change (content, the size given to `arrange`, the
wrap width, the font, the scale) and kept. Paint, hover, tooltips and
pointer input read the kept layout and never flow content again. `arrange`
flows content again only when the bounds it was given, or the content,
changed. A control never invalidates its own layout from `measure`,
`arrange`, `on_layout` or `on_paint`: an imperative control that lays out
everything on every call, on top of a retained toolkit, is how an
application hangs. The window catches the violations and reports them as
layout diagnostics; [Layout](../user/layout.md#retained-layout) has the
rules and what the window does.
