#pragma once
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace gui_forms {

class Control;
class Painter;

// The by-role paint path (owner ruling, 2026-10-14: the atom vocabulary is the
// canonical source). A control describes each part it paints with words from
// the vocabulary (its PartRole, the AnatomyStates it is in) plus the content
// it shows, and paint_part draws it. A control never draws a part itself:
// paint_part hands the part to its theme's engine (ThemeEngine), which draws
// it with a primitive where a theme rule selects one (from T3) and with the
// built-in recipe otherwise.
//
// The built-in recipe draws from today's theme records: `family` names the
// ControlVisualRole whose recipe the part uses, and the part's states select
// the recipe's surface state, focus and default cues.

// The drawn mark of a glyph or indicator part. Marks are engine vocabulary
// (design language §8.5): a control names the mark, never strokes it.
// dialog_launcher, more, overflow and customize were admitted with the
// ribbon plan (owner decision RB-20, §12.8): the dialog launcher of a ribbon
// group, the "more" button of a gallery, the overflow (double chevron) of a
// strip, and the customize button of a Quick Access Toolbar. pin and
// dock_shape were admitted with the docking plan (owner decision DK-35,
// §12.10): a panel's pin, drawn pinned when the part is `checked` and
// unpinned otherwise, and a docking target's shape, a small window with the
// region a drop gives the panel filled (PartPaint::dock_region). underline
// was admitted for links (owner decision, 2026-10-28): a one-unit rule along
// the bottom edge of the mark's box, in the recipe's glyph colour, which the
// link families set to their text colour.
enum class PartMark : std::uint8_t {
    none,
    check,
    indeterminate,
    radio_dot,
    toggle_knob,
    arrow_up,
    arrow_down,
    arrow_left,
    arrow_right,
    chevron_down,
    chevron_right,
    close,
    grip_dots,
    dialog_launcher,
    more,
    overflow,
    customize,
    pin,
    dock_shape,
    underline,
    // DataGridView's row header and error glyphs (owner decision B1-3): the
    // dirty row's pencil, the new row's star and the error mark. Provisional
    // drawings in glyph ink; their look is T4's.
    pencil,
    asterisk,
    error,
    count,
};

inline constexpr std::size_t part_mark_count = static_cast<std::size_t>(PartMark::count);

// Registered (snake_case) name of a mark, as the schema's vocabulary lists it.
[[nodiscard]] std::string_view part_mark_name(PartMark mark) noexcept;

// The values admitted to the vocabulary beside states and marks (design
// language 12.7), as the schema lists them: they have no enum, so this is
// the order they were admitted in.
inline constexpr std::array<std::string_view, 5> part_value_names{
    {"accent_index", "text_orientation", "dock_region", "content_color", "authored_color"}};

enum class PartTextAlignment : std::uint8_t { start, center, end };

// The region the dock_shape mark fills (design language §8.5; owner
// decision DK-35, question J2): a drop on that side of a pane splits it and
// puts the panel there; tab_into puts the panel into the pane's stack as a
// tab.
enum class DockShapeRegion : std::uint8_t { top, bottom, left, right, tab_into };

// One part to paint, in the control's local coordinates. `text`, `image` and
// `mark` are read only by roles that show them (label, caption and content;
// icon and image; glyph and indicator). `text` is borrowed for the call. `font` is an
// effective font (Control::effective_font). `corner_radius`, when set, gives
// a surface part its shape; the theme (T4) will own shapes, so a control sets
// it only where today's recipe has no shape for the part. `orientation` and
// `alignment` are read with `text`; `dock_region` only by the dock_shape
// mark.
struct PartPaint final {
    PartRole role{PartRole::region};
    Rect bounds{};
    AnatomyStateSet states{};
    ControlVisualRole family{ControlVisualRole::panel};
    PartMark mark{PartMark::none};
    std::string_view text{};
    FontSpec font{};
    PartTextAlignment alignment{PartTextAlignment::start};
    // The text's reading direction (§8.5; owner decision DK-35, J4). A
    // turned label lies along the part's height, its line centred across the
    // width; `alignment` then runs from the end reading starts at (the
    // bottom for bottom_to_top, the top for top_to_bottom).
    TextOrientation orientation{TextOrientation::horizontal};
    // The region the dock_shape mark fills.
    DockShapeRegion dock_region{DockShapeRegion::tab_into};
    ImageId image{};
    std::optional<double> corner_radius{};
    // The part's position in the theme's accent ramp (§8.5), or none. A
    // surface part with an accent is tinted from the ramp over its
    // material; text and mark roles ignore it.
    std::optional<AccentIndex> accent{};
    // The colour the part shows as its content (design language 8.5; owner
    // decision W-7), such as a colour picker's sample, or none. Only the
    // fill role honours it: a fill with a content colour is painted that
    // colour, flat, in the part's shape (corner_radius, else square), in
    // place of the recipe's material; every other role ignores it. A swatch
    // is therefore a fill inside a well part that frames it. The colour is
    // data, as an image is: it is drawn as given in ordinary and
    // high-contrast presentation alike (the framing well follows the
    // high-contrast recipe, so the sample stays outlined), and when the
    // part is disabled (T2 GIVEN E2-1: a theme styles only what surrounds
    // it, so a disabled swatch shows through its well, edge and label).
    std::optional<Color> content_color{};
    // The colours an application authored for this part (design language
    // 8.5; owner decision T4-1), or none, such as a DataGridView cell
    // style's BackColor and ForeColor. They stand where an inline style's
    // colours would stand for this one part: over the theme's ordinary
    // declarations, under its important ones, and never in high contrast,
    // where the role's colours stand (§5.8). Unlike the content colour they
    // are a look, not data.
    // - The background: a surface role (and an indicator or glyph without a
    //   mark, which paints its ground) paints it flat in place of the
    //   material's fills, keeping the material's border, keylines and
    //   shadows and the part's shape. A fill with a content colour shows
    //   the content colour. A disabled part keeps it.
    // - The foreground: text roles and the caret draw in it, marks are
    //   inked with it and rules (keyline, divider, seam) are drawn in it. A
    //   disabled part keeps the theme's disabled colours, so disabled stays
    //   greyed; a theme text colour declared important stands over it for
    //   text.
    // A rule that chooses a primitive for the part at an important level
    // stands over both; a primitive that draws a theme material takes the
    // background in its material's fills, and a primitive's own colour
    // parameters do not read them.
    std::optional<Color> authored_background{};
    std::optional<Color> authored_foreground{};
    // The part's identity across frames (T2 GIVEN H3-1), or none. A part
    // with a key has a record in its window's engine part table; one without
    // is drawn statically, with no transitions and no cached raster. It is
    // identity, not vocabulary: it changes nothing a part looks like today.
    // Its views are borrowed for the call.
    std::optional<PartKey> key{};
    // How far past `bounds` the control lets the part be drawn when a theme
    // scales it (T2 GIVEN D5-1, E8), in logical units. The presentational
    // scale grows the part about its centre only as far as this allows:
    // with none granted, the default, a part never grows. Layout and the
    // part's hit region ignore it.
    Insets granted_overflow{};
};

// The theme context a part's states select: hot, pressed or captured,
// selection (selected, checked, current, open, active_tool), the visible focus
// cue and preselect. The control supplies what the part does not carry: its
// enabled, invalid and pending status, window activity and high contrast. A
// part that is itself disabled or unavailable is drawn disabled.
[[nodiscard]] ControlVisualContext part_visual_context(
    const Control& control, AnatomyStateSet states) noexcept;

// Paints one part of `control` by its role (see PartPaint), through the
// engine of the control's effective theme. Roles that draw nothing of their
// own (region, edge, perimeter, sheen) are no-ops: the material of the
// surface they belong to draws them. An empty or non-finite `bounds` paints
// nothing.
//
// What disabled means (owner, 2026-10-20: "a change in font color or control
// highlighting. greyed out"): a disabled part greys the control's chrome,
// through the recipes' disabled states: the text colour of labels and
// captions, and the highlighting (edge, well, hot and selected plates).
// Content never changes:
// - the image role (T2 GIVEN H1-1) is a content picture, such as the docking
//   stamp's snapshot, drawn exactly as given into `bounds`, never tinted,
//   dimmed or resized by a theme, disabled or not;
// - a content colour (GIVEN E2-1) is drawn exactly as given, disabled or
//   not.
// An icon is chrome, the theme's or the command's symbol, and keeps its
// disabled treatment: 45 % opacity.
//
// The shadow role paints the theme's overlay shadow (owner decision RB-37):
// `bounds` is the rect of the surface that casts it (a tooltip's face, a
// menu's panel), with the corner radius of `corner_radius` or else of the
// `family` recipe, and the shadow falls outside it, down and right. Paint it
// before the surface. Only surfaces that really overlap content cast it; a
// control's own contact shadow stays in its material. Nothing is painted
// where the theme casts no overlay shadow (high contrast, by default). A
// control that paints a shadow part reports overlay_shadow_outsets from its
// visual_outsets, so that damage and replay cover the blur.
void paint_part(Painter& painter, const Control& control, const PartPaint& part);

// How far `control`'s theme's overlay shadow reaches outside the casting
// surface's rect, in the control's presentation (ordinary or high
// contrast). Zero on every side when the theme casts none.
[[nodiscard]] Insets overlay_shadow_outsets(const Control& control) noexcept;

// paint_part's drawing, with the recipe and context already resolved. For
// helpers that resolve a recipe themselves (the existing controls' focus and
// default cues), so that every part is drawn by one implementation. The
// shadow role paints nothing here: the overlay shadow is the theme's value,
// not the recipe's, so it is drawn only through paint_part.
void paint_part_from_recipe(Painter& painter, const PartPaint& part,
                            const ControlVisualRecipe& recipe, ControlVisualContext context);

} // namespace gui_forms
