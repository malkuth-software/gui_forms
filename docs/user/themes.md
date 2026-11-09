# Themes

A theme decides how every control looks: its faces, borders, colours, fonts
and motion. Themes are written in a subset of CSS. The program chooses its
theme. A form never does: a form names roles with `classes`, and the theme
gives each role its look. This page is the task guide. [The theme
language](theme-language) is the reference for everything a sheet may say.

There are three ways to give a program its look, and they combine:

1. Use a **built-in theme** as it is: GUI.Forms' own (`gui-forms`), the
   Windows-familiar `windows-professional`, or one of the familiar themes.
2. Write a **theme package**: a sheet compiled onto a base, for the program's
   own roles and adjustments.
3. Let the user pick a **user theme**, which the program allows and checks
   ([The theme language](theme-language.md#user-themes)).

## The built-in themes

`gui-forms` is GUI.Forms' own theme, light and dark. `windows-professional`
is the Windows-familiar theme packages compile onto by default. Both are
written in the theme language: their control families come from a floor sheet
of `:family()` blocks, read once into the theme's recipes when it is first
built.

The **familiar themes** recreate looks that Windows and macOS users
recognise. They are written in the theme language, and none of them uses a
vendor's bitmaps:

| Id | Look | Reference | Colour schemes |
|---|---|---|---|
| `classic` | Windows 98 bevels on grey | XP.css's Windows 98 sheet | light |
| `modern-flat` | flat, with unmistakable boundaries, hover and focus | none | light |
| `xp-style` | Luna-like plates with navy frames | XP.css | light |
| `windows7-style` | Aero-like; opaque until window glass arrives | 7.css | light |
| `macos-style` | translucent plates with round corners | Puppertino | light and dark |

```cpp
#include <gui_forms/gui_forms.hpp>

namespace gf = gui_forms;

void show_classic(gf::Application& application) {
    const std::shared_ptr<const gf::Theme> classic = gf::familiar_theme(gf::FamiliarTheme::classic);
    (*application.resources()).set_theme(classic);   // every window sharing the resources
}
```

- `familiar_theme_named("xp-style")` finds a theme by its id, and
  `familiar_theme_id` gives the id back.
- Each theme is built on first use and kept for the life of the program.
  `familiar_theme_build` also returns the build's diagnostics and its measured
  work.
- A light-only theme shows its light records in a dark colour scheme. High
  contrast is derived for every theme from its colour roles.
- Where a reference colour would put an enabled control below the contrast
  gate (4.5:1 for text, 3:1 for boundaries, glyphs and focus), the colour is
  changed to meet it, and the sheet's comment names each change. Disabled
  controls keep the reference's colours, as WCAG exempts inactive
  components.
- A familiar theme is a theme a window shows, not a base a package compiles
  onto. To show one in a single window, call `window.set_theme(theme)`.

## A theme package

A package is a directory with a manifest and a sheet:

```text
themes/my_app/
  theme.json
  my_app.theme.css
```

```json
{
  "format": "gui-forms-theme/1",
  "name": "my_app",
  "version": "1.0.0",
  "engine": ">=0.3",
  "sheet": {"path": "my_app.theme.css", "sha256": "<the sheet's SHA-256>"}
}
```

The sheet styles the roles your forms name and adjusts the base:

```text
/* The palette: colour roles, never literal text colours, so that forced
   high contrast can replace each one by its meaning. */
:root {
  ink-on-paper: #182d42 on #ffffff;
  muted-ink-on-paper: #5b6b79 on #ffffff;
}

.heading { color: role(ink-on-paper); font-family: content; font-size: 18px; font-weight: 700; }
.muted { color: role(muted-ink-on-paper); }

/* A part of a control, in a state. */
button.default-action::part(face):hover { scale: 1.02; transition: scale 90ms ease; }

/* Every accent button, whatever control draws it. */
:family(accent-button) { border-radius: 6px; }

/* Ancestors: the descendant and child combinators. */
#settings\.root > button::part(face) { border-radius: 4px; }
```

### Selectors

A selector names one part of one control:

- **Type, ID, class.** A control type (`button`, `track-bar`), a stable ID
  (`#settings\.ok`, with the dots escaped) or a class (`.heading`).
- **Parts.** A part (`::part(face)`) or a part role (`::role(face)`).
- **States.** A state (`:hover`, `:checked`, `:disabled`,
  `:state(drag-proximate)`).
- **Ancestors.** Up to eight ancestors, joined by whitespace or `>`. An
  ancestor names only a type, an ID and classes.
- **Grouping.** `:is()`, `:where()` and `:not()`.

`:family(<name>)` selects a **control visual family**: the parts any control
draws in that family. The families are `window`, `panel`, `card`, `button`,
`accent-button`, `command-button`, `choice`, `editor`, `menu-item`,
`selection`, `progress`, `tool-tip`, `link` and `visited-link`. A family rule
reaches every control that draws the family, including your own controls and
the parts of compound controls. That is how the built-in themes are written.
A font property in a family rule is dropped with a warning, because fonts are
resolved per control.

Specificity is CSS's: stable IDs, then classes and states (a family counts
as a state), then types and parts. A package compiled onto `gui-forms` sits
above the base as CSS's author origin sits above the user agent's, so every
package rule beats every base rule. [The theme language](theme-language.md#selectors)
gives the full table and what is refused.

### Building the package in

`gui_forms_add_theme` embeds the package in the program and checks it against
your forms:

```cmake
gui_forms_add_theme(my_app themes/my_app FUNCTION my_app_theme NAMESPACE my_app
                    FORMS forms/main.form forms/settings.form)
```

The build fails when the manifest's hash does not match the sheet, or when a
stable-ID selector names no object of the listed forms: a rule that names
nothing styles nothing. The generated header declares
`gui_forms::ThemePackage my_app::my_app_theme()`.

### Compiling it

The application compiles its theme once, when it is created: the base, then
your packages in order. A later package overrides an earlier one and may read
its colour roles.

```cpp
gf::ApplicationConfig config{};
config.theme.base = gf::ThemeBase::gui_forms;
config.theme.packages.push_back(my_app::my_app_theme());
gf::ApplicationCreateResult created = gf::Application::create(std::move(config));
if (!created) {
    // created.error is theme_refused: created.findings hold each diagnostic
    // (code, file, line, column, rule and property).
}
```

- An **error** refuses the whole theme, and nothing of it is installed.
- A **warning** drops one declaration or one rule. The rest applies, and
  `findings` lists each warning.

A package compiled onto `gui-forms` writes its colours in light terms only.
Its dark variant is derived by the same rule as the base's.

Without an application, as in a test, compile against a window's resources
and select the result:

```cpp
gf::ThemeCompileOptions options{};
options.controls = gf::stock_control_anatomies();
gf::ThemeCompileResult compiled =
    (*window.resources()).compile_theme_package(my_app::my_app_theme(), gf::ThemeLibrary{}, options);
if (compiled.succeeded()) window.set_theme(compiled.theme());
```

## Which theme a control uses

A control uses the first theme it finds in this order:

1. its own override (`Control::set_theme_override`);
2. its window's (`SurfaceRoot::set_theme`);
3. the application's resources (`ApplicationResources::set_theme`);
4. the built-in default theme.

A user theme the program allows replaces or extends the program's theme in
the resources ([The theme language](theme-language.md#user-themes)).
