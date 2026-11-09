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

The sheets compile onto a built-in theme, whose recipes stand wherever they
set nothing: `ThemeCompileOptions::base`, which defaults to
`default_theme_base`, GUI.Forms' own theme `gui-forms`; or
`ThemeBase::windows_professional`, whose recipes stand alone, with no sheet
and no dark variant. Onto GUI.Forms' own theme (`gui-forms`), its
light sheet comes first in the cascade, so the package's rules override it
and may read its colour roles, and its dark variant is derived by rule: the
package's colours are written in light terms and darken by the same rule in
a dark window, so a package on `gui-forms` writes no dark values of its own.

The built-in themes are themselves sheets. `gui-forms` draws its control
families from a floor sheet (`gui_forms_floor_sheet()`) of `:family()`
blocks, read once into its recipes when the theme is first built. The
familiar themes are made the same way: `familiar_theme(FamiliarTheme)`
returns `classic` (Windows 2000 bevels), `modern-flat`, `xp-style` (Luna-like),
`windows7-style` (Aero-like, opaque until glass arrives), each light only,
or `macos-style` (macOS-like, light and dark: its dark colour scheme
resolves a second floor sheet, `familiar_theme_dark_floor_sheet()`, as an
authored dark variant), with high contrast derived as for every theme.
Each follows its visual reference wherever the engine can express it:
7.css (https://khang-nd.github.io/7.css/) for `windows7-style`, XP.css
(https://botoxparty.github.io/XP.css/) for `xp-style`, Windows 2000's
standard colour scheme and frame controls for `classic`, and Puppertino (https://puppertino.com/) for
`macos-style`. A reference value that an enabled control would
draw below the contrast gate is changed to meet it, and each sheet's
comment names it. Disabled text and disabled controls' boundaries are
exempt from the gate, as WCAG exempts inactive components, so every theme
keeps its disabled colours. They are themes a window may
show (`SurfaceRoot::set_theme`, or `ApplicationResources::set_theme` for every window), not bases a package compiles onto.

An application's theme is the base and its packages in order:
`compile_theme_packages` (and `ApplicationResources::compile_theme_packages`)
compiles `ThemeCompileOptions::base`, then each package with its imports, as
one cascade, so a later package overrides an earlier one and may read its
colour roles. The theme is named after the last package; with no packages it
is the base alone. While no application is current, windows show the process
default, `default_compiled_theme()`, the base `default_theme_base` names,
built once.

- An **error** refuses the whole theme. Nothing of it is installed; the theme
  in use stays in use, and the images the refused compilation loaded are
  released.
- A **warning** drops one declaration or one rule; the rest applies.

Every diagnostic has a code (`E409 variable_undefined`), the file, the line
and column (counted in Unicode scalar values), the rule and property where
there is one, and one detail argument.

The theme in effect for a control is its own override
(`Control::set_theme_override`), else its window's, else the program's
(`ApplicationResources::set_theme`), else the built-in default theme
(GUI.Forms' own from 0.5).

Every compiled rule keeps where it was written (`ThemeRule::provenance`):
its layer (`ThemeLayer`: built-in, library pack, project theme, user
theme, inline code), the pack that wrote it (its name and the SHA-256 of
its manifest) and the file, line and column of its first token in its own
sheet. GUI.Forms' base sheet and the packages a program's packages import
are library packs; the program's packages are the project theme. An editor
asks where each resolved field came from with
`CompiledThemeRules::resolve_provenance` (a part's primitive parameters,
material fields and recipe fields) or `Theme::provenance` (a control's
recipe): the winning rule's layer, cascade level, selector, specificity,
source and colour origin, or the built-in recipe where no rule set the
field; in forced high contrast the fields the engine's pass draws are its
own. Paint never computes provenance.

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

A selector is a compound selector naming one part of one control, after
at most eight ancestor compounds joined by the descendant combinator
(whitespace) or the child combinator (`>`): `#settings .row > button`. An
ancestor compound names only a control type, a stable ID and classes (or
`*`), so a rule changes only when an ancestor's type, ID or classes do, or
a control moves; the control's subtree is then resolved again. Ancestors
more than 256 levels up are not matched.

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
| either of, counting nothing | `button:where(.primary, #save)` |
| none of | `button:not(.primary, :hover)`: compounds of types, stable IDs, classes, states and families, not nested |
| family | `:family(accent-button)`, a control visual family: the parts a control draws for that family (`window`, `panel`, `card`, `button`, `accent-button`, `command-button`, `choice`, `editor`, `menu-item`, `selection`, `progress`, `tool-tip`, `link`, `visited-link`) |
| selector list | `button, check-box { ... }` |
| surface | `window`, `popup`, with the surface root's classes and stable ID: `window#settings`, `popup.alert` |

`:has()`, `:nth-*()`, attribute selectors, the sibling combinators `+`
and `~`, a combinator inside a group, and `&` are errors, as are a
`:not()` inside a `:not()` and a part, role or surface in one. An unknown control type, state or pseudo-class is a
warning (W106) that drops the rule. A part the selector's control type lacks
is a warning too, but, as CSS's `::part()` matches nothing for a part the
element lacks, it leaves out only its own selector: in
`user-control::part(lid), panel::part(face)` the panel rule still applies. Specificity is CSS's: stable IDs, then
classes and states (pseudo-classes) together, then control types and parts
(the part's name and role, as pseudo-elements). `:family()` counts as a
pseudo-class, `:is()` and `:not()` as their most specific argument and
`:where()` as nothing. So `button:hover` beats `.primary`, as in CSS.
A control's fonts are resolved for no family, so a font property in a
`:family()` rule is a warning that drops it.

A package compiled onto the GUI.Forms base (`ThemeBase::gui_forms`) sits
above the base sheet as CSS's author origin sits above the user-agent
origin: every rule of the package beats every rule of the base, whatever
the specificity, and the base's `!important` declarations lose to the
package's. The base alone is the program's own sheet.

`window` and `popup` select a surface, not a control: a top-level window,
an owned window or a floating panel in its own window is a `window`; a
native popup window (an alert-like owned window, such as a task dialog's or
a font dialog's panel) is a `popup`. Menus, drop-downs and tooltips are
their owner's controls, painted by the owner whether they show as overlays
or in Popup and Splash windows, so ordinary control rules style them and
they take no backdrop. Their classes and stable ID
are the surface root control's. A surface selector takes no control type,
part, role or state, and a surface rule holds `backdrop` alone (any other
property is a warning). A surface selector counts as a control type in
specificity.

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
- **System colours:** `env(system-accent, <colour>)` and
  `env(system-highlight, <colour>)` read the host's accent and
  text-selection colours as hints, in any colour position. The fallback
  colour is required, and is read where the host reports none or the
  theme is compiled without hints. A hint is read when the theme is
  compiled (`ThemeCompileOptions::system_colours`;
  `ApplicationResources::compile_theme_package` passes the hints its
  windows last reported). GUI.Forms' own theme reads neither.
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
otherwise (`shape`, `surface-finish`, `glow`, `reflection`). `primitive: <name>` selects a
drawing primitive for the part and its parameters are then properties by
their registered names (`primitive: bulge; amplitude: 0.5`); a rule declaring
only material fields selects the `material` primitive.

| Area | Properties |
|---|---|
| fills | `background-color`, `background-image` (linear, radial and conic gradients and their `repeating-` forms, images, `pattern()`), `background` (up to 8 layers), `background-blend-mode`, `background-size`, `background-repeat`, `background-position` |
| borders | `border`, `border-<edge>`, `border-width`, `border-color`, `border-style` (`solid`, `dashed`, `dotted`, `double`, `groove`, `ridge`), `border-radius` (1 to 4 values), `border-image`, `outline`, `outline-style` (`auto`, `solid`, `dotted`, `none`), `outline-offset`, `default-outline: <width> <colour>` (the default button's second perimeter, on a control), `keylines` (`none`, or one or two comma-separated rings of one to four colours in box order: lines one unit wide, the first ring one unit inside the border and the second inside it), `shape` |
| shadows and light | `box-shadow` (up to 4, `inset` admitted), `text-shadow` (on a control), `glow`, `glow-placement`, `surface-finish`, `reflection` |
| filters and blending | `filter` (`brightness()`, `contrast()`, `saturate()`, `grayscale()`, `sepia()`, `invert()`, `hue-rotate()`, `blur()`, `drop-shadow()`), `mix-blend-mode` (`normal`, `multiply`, `screen`), `isolation`, `mask-image` |
| presentation | `opacity`, `scale`, `translate` |
| effects | `effect: <name>`, a registered effect program in kebab case (such as `progress-woven-stripes`), or `none`; given the effect registry (`ThemeCompileOptions::effects`, `ApplicationThemeConfig::effects`, usually `built_in_effect_registry()`), a name it does not hold refuses the theme with E417 at the name; the control also checks the effect when it attaches it, and a refused one leaves the part to its primitive; `effect-primary`, `effect-secondary`: the effect's colours, each one colour role (`role(<name>)`, never a literal), so effects follow the palette in each colour scheme and under forced high contrast |
| text | `color`, `muted-color` and `glyph-color` (the recipe's secondary text and glyph colours, on a control or a named part, such as `group-box::part(caption)`; not on a role), `font: <role>` (`ui`, `field`, `caption`, `heading`, `title`, `monospace`) |
| fonts | `font-family` (`control`, `content`, `monospace`), `font-size` (4px to 96px, or em of the control's own size), `font-weight` (`normal`, `bold`, 100 to 900), `font-style` (`normal`, `italic`), `letter-spacing` (`normal`, px or em) |

`pattern(<name>, <ground>, <figure> [, <tile>])` is a built-in procedural
pattern in two colours: `checker` (a checkerboard; its default 2px tile is a
50% dither of logical pixels, as a Windows scroll track), `felt`,
`wood-grain`, `paper-fibre`, `pinstripe` and `brushed-metal`. The tile is a
length from 2px to 256px (by default 2px for the checker, 4px for the
pinstripe and 64px for the rest). The engine generates the pattern for each
display scale in an idle frame and draws the ground until it is ready; a
pattern layer takes no position, size or repetition. For example,
`background: pattern(checker, #d4d0c8, #ffffff)`.

`reflection` mirrors the part's finished face (its fills, borders, glows,
filters and mask; not its text), or with `backplane` the window's backplane
under the part (as glass samples it), about one edge into the space past it:
`reflection: none | <edge> [<gap> [<reach>]] [<start> [<end>]] [backplane]`. The edge is
`above`, `below`, `left` or `right` (as in `-webkit-box-reflect`); the gap
and the reach are lengths (0, and the rest of the 8 units a reflection may
reach, by default), and together at most 8 units; the start and end are the
opacity next to the edge and at the far end, numbers or percentages (0.5 and
0 by default). A unitless 0 is a length until an opacity has been read, so
`below 0 6px 1 0` is a gap of 0. The reflection is drawn beneath what the
face draws past that edge, and is omitted under high contrast and reduced
transparency. A part drawn in the backplane plane reflects no backplane.
For example, `reflection: below 1px 6px 40% 0` or `reflection: below 6px 30% backplane`.

`inherit` is admitted on `color` and `font` only; `initial` means the
built-in recipe's value.

The font properties style a control's own text, the font it holds; its
fixed secondary text keeps its fonts. `font-family` names one of the
bundled faces by its role, never a host font, so a family name, a generic
family or a list is an error. They belong to rules that match a control:
a rule naming a part, or a state such as `:hover`, may not declare them,
since a font is a layout input and states change without a layout. They
may differ by colour scheme under `@media (prefers-color-scheme: dark)`.
A font set in code (`set_font`) wins over the theme's. A change of the
resolved font measures the control again. `font-variant`, `font-stretch`,
`line-height` and the rest of CSS typography are warnings.

`backdrop` belongs to `window` and `popup` rules: the chain of operations
over what lies beneath the surface, applied in order. It is `none`, or a list
of `blur(<length>)` (a radius, 0 to 32px; at most one), `saturate(<number> |
<percentage>)` (0 to 4, 1 unchanged) and `tint(<colour> [/ <alpha>])` (the
alpha, 0 to 1 or a percentage, scales the colour's own), at most 8
operations. Windows and popups are native, partially transparent windows,
and blurring is the operating system's work: `blur()` asks the compositor for
its blurred backing (the system material), and GUI.Forms never samples other
windows' pixels. A role colour in `tint()` (`tint(role(ink-on-paper) / 0.6)`)
follows the colour scheme and forced high contrast. In any other rule
`backdrop` is warning W110 (`backdrop_not_on_window`) and is dropped; a
second blur, an amount out of range or a ninth operation is an error. For
example, `window { backdrop: blur(12px) saturate(140%) tint(role(ink-on-paper) / 0.6) }`.

The winning `window` rule's backdrop, matched against the window's media and
its root control's classes and stable ID, becomes the window's backdrop chain
(`Window::set_backdrop_chain`). It is lowered again when the window shows
another theme, when its presentation settings (scheme, contrast, motion)
change and when its scale changes; changing the root's classes later takes
effect at the next of these. An application that sets its own chain keeps
it. `popup` rules are compiled and held: they apply once native popup
windows exist.

The window outer band, a thin strip along the edges of a top-level window's
client area that the program declares with `Window::declare_band`, takes its
look from `window` rules. `band-width` is one length (0 to 32px; the
window's own width wins, and the band is snapped to whole device pixels),
`band-color` its rest colour (transparent by default, so a resting band shows
the window's base), `band-lit-color` its lit colour (the window's focus-ring
colour by default) and `band-effect` `none` or `flash` (a lit band flashes,
at most twice a second). Each property comes from the last matching rule
that sets it. A CSS-wide keyword is an error. In any other rule, including
`popup` rules, a band property is warning W111 (`band_not_on_window`) and is
dropped.

A themed TextBox paints its field as one part, `text-box::part(well)`: a
background there is the field's ground, and the control's own colour
(`text-box { color: ... }`) is its text's. Its `face` and `edge` are painted
only while the application overrides the field's look (its background or
style), and its text runs share no part identity, so a surface property on
the face or edge, and a colour or surface property on `::part(content)`, is
warning W112
(`part_not_themed`), saying what to style instead; the declaration is
kept. The containers `scrollable-control`,
`container-control`, `user-control` and `split-container` have a ground,
`::part(face)`, over their whole box, painted only when a rule names that
part (`user-control::part(face) { background: #202020 }`); a rule on the
panel family or the face role alone paints none. Under forced high contrast the band draws the high-contrast
palette's focus colour, solid and unflashing; reduced motion and turned-off
ambient motion still a flash at its lit colour. For example,
`window { band-width: 2px; band-lit-color: #c0392b; band-effect: flash }`.
In 0.5 the band is a steady colour or the built-in flash only. Effect
programs and textures on the band are planned after 0.5 (owner ruling,
2026-11-08). The band never takes a resize-edge role: a frameless window
stays frameless.

`clip-path`, `rotate`, `transform`, text
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

## Colour schemes

A theme declares the colour schemes it has variants for on `:root`:
`:root { color-scheme: light dark; }`, or `light` or `dark` alone. A theme
that declares nothing is light only. The rules for one scheme are written
under `@media (prefers-color-scheme: dark)` (or `light`); unconditioned
rules hold in both, so a dark variant writes only what differs. Any other
word (`normal`, `only`) and `color-scheme` under `@media` are errors;
`color-scheme` outside `:root` is a warning that drops it, and a sheet with
dark rules but no dark variant is warned (W402
`scheme_rules_without_variant`), since those rules then apply nowhere.

A window whose scheme its theme has no variant for shows GUI.Forms' own
theme (`gui-forms`) in that scheme instead: `Window::theme_fallback()` says
so, the warning W403 `theme_variant_missing` is written once per theme and
scheme, and the colours the application set on its controls (`back_color`,
`fore_color`, authored surfaces and parts' colours) are suspended, since
they were chosen against the other theme. Switching back shows the
application's theme and its colours again; `theme_changed` is raised each
time. High contrast uses GUI.Forms' own high-contrast palette in either
case.

The window's scheme is the application's choice
(`PresentationSettings::colour_scheme`) unless the application turns on
`ApplicationResources::set_automatic_colour_scheme(true)`, when every
window follows the host's light or dark preference (capability
`presentation_preferences`; light where the host reports none).

Colour belongs in the theme: setting `back_color` or `fore_color` on a
control still applies, for compatibility, but writes the warning W404
`inline_colour` once per control and property.

`tools/relocate_colours.py` moves the colours a form document writes
(`fore-color`, `back-color` as literal hexadecimal colours) into a theme
sheet: one rule per control, keyed by its escaped stable ID, with the light
value as written and a dark value under `@media (prefers-color-scheme:
dark)` seeded by the dark colour rule (the `gui_forms_dark_colour` filter
the build makes), a starting point to edit. It declares `:root {
color-scheme: light dark }` when the sheet declares no scheme, removes the
statements from the document, and leaves alone any other colour form and any
control the sheet already selects. It prints a diff unless given `--write`.

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
