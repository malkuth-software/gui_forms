# The theme language

A GUI.Forms theme is written in a subset of CSS. The same language styles a
whole program (a theme package) and one control (an inline style). This page
describes what the language admits, how a sheet is compiled and what
happens when it is wrong. The conformance corpus,
`tests/theme_language/conformance/`, holds one compiled example of every
form below, with its expected result; its coverage table,
`COVERAGE.tsv`, lists each form and its example.

## How a theme is compiled

A theme package is a manifest (`theme.json`) naming its sheet, its image
resources and the themes it imports, each with its SHA-256 hash.
`compile_theme_package` verifies the package, compiles its sheet with the
sheets it imports (imported sheets first, as in CSS) and returns either a
`Theme` with any warnings or a refusal with its diagnostics.

- An **error** refuses the whole theme. Nothing of it is installed; the theme
  in use stays in use, and the images the refused compilation loaded are
  released.
- A **warning** drops one declaration or one rule; the rest applies.

Every diagnostic has a code (`E409 variable_undefined`), the file, the line
and column (counted in Unicode scalar values), the rule and property where
there is one, and one detail argument.

The theme in effect for a control is its own override
(`Control::set_theme_override`), else its window's, else the program's
(`ApplicationResources::set_theme`), else the built-in default theme.

## Sheets

The source is UTF-8 (a leading byte order mark is kept and ignored), at most
1 MiB, with LF, CRLF or CR line breaks. A sheet holds style rules,
`@import`, `@media` and `@keyframes`:

```css
@import "palette";
@media (prefers-color-scheme: dark) { button { opacity: 0.9 } }
@keyframes pulse { from { opacity: 0.5 } to { opacity: 1 } }
button::part(face):hover { scale: 1.04; transition: scale 90ms ease }
```

CSS's error recovery is not used: a structural defect (a missing `}`, a
declaration outside a rule, a stray token) is an error. Nesting style rules
inside style rules is not admitted. At most 10,000 rules (counting the rules
selector lists, `:is()` and media query lists expand into), 64 declarations
per rule, 32 levels of nesting and 4 KiB strings.

## Selectors

A selector is one compound selector naming one part of one control. There
are no combinators.

| Form | Example |
|---|---|
| control type | `button`, `track-bar` (the registered id, in kebab case) |
| any control | `*`, or no type |
| stable ID | `#player-play` |
| class | `.primary` |
| part | `::part(scroll-thumb)` |
| part role | `::role(face)` |
| state | `:hover` `:active` `:focus` `:focus-visible` `:checked` `:indeterminate` `:disabled` `:enabled` `:read-only` `:invalid` `:open` `:default`, and every other published state as `:state(drag-proximate)` |
| either of | `button:is(:hover, :focus-visible)::part(face)` (at most 32 arguments) |
| selector list | `button, check-box { ... }` |

`:not()`, `:where()`, `:has()`, attribute selectors, combinators and `&`
are errors. An unknown control type, part, state or pseudo-class is a
warning that drops the rule. Specificity counts stable IDs, then classes,
control types, parts and states.

## Values

- **Units:** plain numbers; `px` (logical units); `%` where CSS defines
  percentages; `em`, relative to the part's text size; `ms` and `s`;
  `deg`, `rad`, `turn`; `dppx` and `x` in `@media (resolution)`. Every other
  unit is an error.
- **Colours:** colour roles first: `role(ink-on-paper)` gives the pair's
  foreground in `color` and its background in `background`, and
  `role(<pair> foreground)` or `role(<pair> background)` picks one side. A
  theme's palette is a `:root` block of role declarations
  (`:root { ink-on-paper: #1b2733 on #ffffff }`); a role the palette does
  not define is an error. Literals: `#rgb`, `#rgba`, `#rrggbb`,
  `#rrggbbaa`, `rgb()`, `rgba()`, `hsl()`, `hsla()` (comma and space
  forms), `transparent`, `color-mix(in srgb, ...)` and every CSS named
  colour. A literal colour in a text or edge position (`color`, borders,
  outlines) is a warning: forced high contrast can only replace it by its
  position.
- **Expressions:** `calc()`, `clamp()`, `min()`, `max()`, and the engine's
  `mix()`, `smoothstep()`, `choose()`, `less()`, `less-equal()`,
  `greater()`, `greater-equal()`, `equal()`, `not-equal()`, `state()` and
  `param()`. A value reading an engine input (`--pointer-x`, `--pointer-y`,
  `--time-in-state`, `--display-scale`, `--text-scale`) or an application's
  published value is evaluated by the engine per frame; any other is folded
  when the theme is compiled. At most 64 instructions and a stack of 16.
- **Images:** `url("resource:<name>")`, a resource the manifest lists.
- **Keywords:** a keyword the running engine does not have is an error,
  unless the sheet declares a newer engine, when it is a warning.

## Custom properties

`--name: <tokens>` defines a custom property and `var(--name)` or
`var(--name, fallback)` reads it. Definitions cascade per part like any
property, `!important` included: `button.loud { --lift: 0.8 }` holds for
loud buttons only. A `var()` of a name no rule defines, without a fallback,
is an error, and so is a cycle. Engine inputs are reserved names; defining
one is an error. At most 1,024 custom properties per sheet, a substitution
of at most 4,096 tokens, and at most 128 copies of one rule for the
combinations of definitions it reads.

## Properties

Property names are CSS's where CSS has the property, and engine-named
otherwise (`shape`, `surface-finish`, `glow`). `primitive: <name>` selects a
drawing primitive for the part and its parameters are then properties by
their registered names (`primitive: bulge; amplitude: 0.5`); a rule declaring
only material fields selects the `material` primitive.

| Area | Properties |
|---|---|
| fills | `background-color`, `background-image` (linear, radial and conic gradients and their `repeating-` forms, images), `background` (up to 8 layers), `background-blend-mode`, `background-size`, `background-repeat`, `background-position` |
| borders | `border`, `border-<edge>`, `border-width`, `border-color`, `border-style` (`solid`, `dashed`, `dotted`, `double`, `groove`, `ridge`), `border-radius` (1 to 4 values), `border-image`, `outline`, `outline-style` (`auto`, `solid`, `dotted`, `none`), `outline-offset`, `shape` |
| shadows and light | `box-shadow` (up to 4, `inset` admitted), `text-shadow` (on a control), `glow`, `glow-placement`, `surface-finish` |
| filters and blending | `filter` (`brightness()`, `contrast()`, `saturate()`, `grayscale()`, `sepia()`, `invert()`, `hue-rotate()`, `blur()`, `drop-shadow()`), `mix-blend-mode` (`normal`, `multiply`, `screen`), `isolation`, `mask-image` |
| presentation | `opacity`, `scale`, `translate` |
| text | `color`, `font: <role>` (`ui`, `field`, `caption`, `heading`, `title`, `monospace`) |

`inherit` is admitted on `color` and `font` only; `initial` means the
built-in recipe's value. `clip-path`, `rotate`, `transform`, text
decoration and spacing, and layout properties (`width`, `margin`, ...) are
warnings: geometry and layout belong to the engine and to forms.

## Motion

`transition` (shorthand and longhands) animates `opacity`, `scale`,
`translate`, `face` and a primitive's animated parameters between the
states its rules name. Timing functions: `linear`, `ease`, `ease-in`,
`ease-out`, `ease-in-out`, `cubic-bezier()`, `steps(n, jump-start |
jump-end)`, and the engine's `follow(<time>)`, `spring(<time>, <damping>)`
and `decay(<time>)`. Every duration is clamped to the engine's 400 ms
ceiling, and reduced motion is the engine's: a theme cannot opt out of it.

`@keyframes` with `animation` on a state rule play once on entering the
state (at most 4 stops between `from` and `to`). With
`animation-iteration-count: infinite` (or `infinite` in the shorthand) they
loop as an ambient effect, at most 30 frames per second
(`animation-frame-rate`), resting at `animation-rest-pose` when the
animation stops.

## `@media`

`prefers-color-scheme: light | dark`, `forced-colors: active | none`,
`prefers-reduced-motion: reduce | no-preference`, and `resolution` with
`min-` and `max-` in `dppx` or `x`. Conditions join with `and`; a comma list
applies the block for each query. Media types other than `all` and `screen`,
`not`, `only` and `or` are errors; an unknown feature is a warning that
skips the block. The conditions are evaluated per window.

## Inline styles

`Control::set_inline_style` takes the same language for one control: its
own declarations, then blocks for its parts and states with the control's
selector implied.

```cpp
const InlineStyleResult result = button.set_inline_style(
    "--lift: 0.6; opacity: 0.9 !important; ::part(face):hover { scale: 1.04 }");
```

Inline declarations win over the theme's, except where only the theme's is
`!important`. A refused style changes nothing: the control keeps its
previous style, and the result holds the diagnostics, which name the file
`inline:<stable ID>`. An inline style's custom properties take part in the
theme's cascade: where the style defines a name the theme's rules read, those
rules read the style's value for that control, and the style's own `var()`
of a name only the theme defines reads the theme's. Such a style is compiled
against the theme in effect, and again when that theme changes; a theme
against which it no longer compiles is refused (`std::invalid_argument`,
naming the first error), the old theme staying in use.

## User themes

A user theme is a sheet a program's user writes: a main file
`themes/<name>.css` and an optional folder `themes/<name>/` beside it. The
main file opens with a header, which makes it a theme:

```css
@theme { version: "1.0.0"; author: "A. User"; engine: ">=0.3"; }
button::part(face) { background-color: #2b3a4a !important }
```

The header is the sheet's first item (`E207` elsewhere); its descriptors
are strings, and `engine` is a bound as a manifest's (`">=0.3"`). The
folder's top-level `.css` files are further sheets of the theme, loaded
before the main file in filename order, or where an `@import "editor.css";`
names one; its top-level `.png` files are images
(`url("resource:wood.png")`); `icons/<identifier>.png` overrides one of the
program's icons. `@import "<package>"` imports one of the program's own
theme packages. `compile_user_theme` compiles the files a host read.

Under a program's own theme (the layered policy,
`ApplicationResources::set_user_theme_policy`), a user theme's ordinary
declarations apply only where the program declares nothing for that
property of that part, and its `!important` ones over the program's
ordinary declarations, never over the program's own `!important` ones.
Forced high contrast stands over every user declaration, and a user theme's
opacity is at least 0.5, so that no user theme hides a control. A program
may instead let a user theme replace its own, or refuse user themes.

A program's icons are registered with an identifier, segments of lowercase
letters, digits and dashes joined by `.` (`toolbar.save`,
`ApplicationResources::register_icon`), and each may be marked fixed, for
such as a security indicator. A user theme's `icons/toolbar.save.png`
replaces that icon (`ApplicationResources::icon`) when it decodes, is at
most 1024 by 1024 pixels and 1 MiB, and is not fully transparent. Each
other icon file is a warning and is ignored: one naming no program icon
(`W501`), one replacing a fixed icon (`W502`), and one breaking those
rules (`W503`).

A program finds its users' themes at launch with `scan_user_themes`, which
reads the `themes/` folder beside the program and the per-user one
(`per_user_theme_folder`, under the platform's configuration folder; the
per-user theme wins a name). It reads at most 64 themes a folder, and
compiles each into the program's resources
(`ApplicationResources::compile_user_theme`), so a list can show the
themes that compile and the refused ones with their diagnostics. A `.css`
file without the header is no theme, and the scan's log says so.
`read_theme_command_line` reads `--theme <name>`, `--theme built-in` (no
user theme; the way back that needs no reading) and
`--export-themes [folder]`; `select_user_theme` applies the choice before
the first window. `export_user_themes` writes the program's own packages
as user themes, each a verbatim copy that changes nothing until edited,
with the overridable icons and a `README.txt`, and never overwrites a file.

## Not yet built

- `backdrop-filter` is refused (`E999 not_yet_built`).
- A gradient positioned at the pointer, which the plan lowers to the `sheen`
  primitive, is refused (`E405`).
- Procedural backgrounds (`felt()` and the rest) are warnings until the
  engine registers them.
