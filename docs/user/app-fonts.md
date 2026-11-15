# Application font families

GUI.Forms draws its own interface with the fonts it bundles. A program may
also load its own font bytes as a family of its own, for the text it draws
inside its composite controls and its sub-surfaces
(`gui_forms/typography/app_fonts/app_fonts.hpp`; owner decision of
2026-11-15). Stock controls, themes applied to them and GUI.Forms' own
interface (the window manager, the capsule control, dialogs, the business
card, the overlay, help and tooltips) never draw one.

## Loading

```cpp
std::shared_ptr<const std::vector<std::byte>> bytes = read_my_font();
const std::array<gui_forms::AppFontFace, 1> faces{{{bytes, 400, false}}};
gui_forms::AppFontLoad loaded = gui_forms::load_app_font_family("Brand Sans", faces);
if (loaded.validation.status != gui_forms::AppFontStatus::loaded) {
    // loaded.face_index and loaded.validation.table say what was refused.
}
gui_forms::AppFontFamily brand = std::move(loaded.family);
```

A family holds 1 to 4 faces, each a TrueType or OpenType (CFF) face of at
most 64 MiB with a weight from 1 to 1000. Every face is checked before
anything is registered (`validate_app_font` lists the checks: the sfnt
header, the table directory, the required tables and their consistency);
a collection (`.ttc`) is refused. The name is 1 to 64 bytes without
control characters, quotes, commas or semicolons, and no bundled family,
CSS generic or loaded family may already have it.

The family gets a number from 256 up (`AppFontFamily::id`), which
`FontSpec::family` carries like a bundled family's number.

## Where it draws

| Where | Application family |
|-------|--------------------|
| A composite's `<look>` and the parts and children it styles | drawn; the look names it in `font-family` |
| A control inside a sub-surface | drawn |
| A stock control, a theme, GUI.Forms' interface | dropped, with one warning line per family |

A look resolves the name when the composite's definition is compiled, so
load the family before loading the `.control` document. A theme sheet that
names an application family gets the unavailable-family warning (W113),
as for any family GUI.Forms does not have. A dropped family leaves the
font's role to draw (a stack that lists an application family is dropped
whole), and `app_font_statistics().refused_uses` counts each drop.

Glyphs a family lacks are drawn by the font's stack, its role's faces and
the bundled fallback faces.

## Quarantine

Passing the checks does not prove a face draws. A face the text engine
cannot open, shape or rasterise quarantines its family for the rest of the
process: a `gui-forms-app-font=quarantined` line is logged, the family
takes part in no text from then on, and the bundled faces draw instead.
`app_font_family_state` reports `quarantined`.

## Unloading

Destroying the `AppFontFamily` handle (or calling `unload`) stops new text
from resolving the family at once. Text engines that already opened the
family keep its bytes, so text they shaped stays drawable; the bytes are
freed when the last such engine goes (`AppFontUnload::deferred`, state
`unloading`, then `released`). A family number is never reused.

## C ABI and C#

The fonts table's `load_family` and `unload_family` (C ABI 1.5,
provisional) take `gf_app_font_face` records and copy the bytes; C# wraps
them as `GuiForms.Interop.AppFontFamily`, disposed to unload.
