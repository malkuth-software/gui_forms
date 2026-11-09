# Themes

A theme decides how every control looks. Themes are written in the theme
language, a subset of CSS addressed to controls and their named parts. The
program chooses its theme; a form only names roles with `classes`.

## Choosing a look

- **Built-in themes:** `gui-forms` (GUI.Forms' own, light and dark) and
  `windows-professional`; and the familiar themes `classic` (Windows 2000),
  `modern-flat`, `xp-style`, `windows7-style` and `macos-style`, shown with
  `gf::familiar_theme(gf::FamiliarTheme::classic)`.
- **A theme package**: a `theme.json` manifest and a sheet, compiled onto a
  base when the application is created.
- **User themes**, which the program allows and checks.

## A package

```css
:root { ink-on-paper: #182d42 on #ffffff; }                 /* colour roles */
.heading { color: role(ink-on-paper); font-size: 18px; font-weight: 700; }
button.default-action::part(face):hover { scale: 1.02; transition: scale 90ms ease; }
:family(accent-button) { border-radius: 6px; }               /* every accent button */
#settings\.root > button::part(face) { border-radius: 4px; }
window { backdrop: blur(12px) tint(role(ink-on-paper) / 0.6); }
```

```cmake
gui_forms_add_theme(my_app themes/my_app FUNCTION my_app_theme NAMESPACE my_app
                    FORMS forms/main.form)
```

```cpp
config.theme.base = gf::ThemeBase::gui_forms;
config.theme.packages.push_back(my_app::my_app_theme());   // compiled once, in Application::create
```

## Selectors

- type (`button`, `track-bar`), stable ID (`#settings\.ok`), class
  (`.heading`), any (`*`);
- part (`::part(face)`) and part role (`::role(face)`);
- states (`:hover`, `:active`, `:focus`, `:focus-visible`, `:checked`,
  `:disabled`, `:open`, ... and `:state(name)`);
- up to eight ancestors, joined by whitespace or `>`;
- `:is()`, `:where()`, `:not()`, and `:family(<name>)` for a control visual
  family (`window`, `panel`, `card`, `button`, `accent-button`,
  `command-button`, `choice`, `editor`, `menu-item`, `selection`,
  `progress`, `tool-tip`, `link`, `visited-link`);
- the surfaces `window` and `popup`, which hold only `backdrop`.

`:has()`, `:nth-*()`, attribute selectors and the sibling combinators are
errors. Specificity is CSS's; a package beats its base as CSS's author origin
beats the user agent's.

## Backdrop

A window, and only a window, may carry a backdrop chain: `blur(<length>)`
(at most one, up to 32px), `saturate()` and `tint()`, at most eight
operations. The window system supplies the blur; GUI.Forms paints only the
tint and never reads other windows' pixels. Under reduced transparency or
without a compositor the window is opaque.

Full pages: [Themes](https://gui-forms.readthedocs.io/user/themes.html),
[The theme language](https://gui-forms.readthedocs.io/user/theme-language.html),
[Transparency, glass and reflections](https://gui-forms.readthedocs.io/user/transparency.html).
