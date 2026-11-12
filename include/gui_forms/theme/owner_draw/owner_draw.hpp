#pragma once

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/event/event/event.hpp"
#include "gui_forms/theme/types/theme_types.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace gui_forms {

class Control;
class Painter;

// How a control with items draws each item's content (owner decision W-Q6:
// content-only owner drawing). `normal` draws it by role. `owner_draw`
// raises the control's item-content event for each painted item instead:
// the control still paints the item's plate, selection and focus cue by
// role, and the application paints only what lies inside the item's
// content rectangle. Windows Forms' OwnerDrawFixed and ListView.OwnerDraw
// map here; OwnerDrawVariable's per-item measuring is not part of the seam.
enum class ItemDrawMode : std::uint8_t { normal, owner_draw };

// One item's content to paint (owner decision W-Q6), raised while the
// control paints, after the item's plate and selection and before its focus
// cue. `painter` is the control's, borrowed for the call, in the control's
// local coordinates and clipped to `content`; the handler draws with it and
// does not keep it. `content` is the item's content rectangle in local
// coordinates. `index` is the item's position in the control's model and
// `stable_id` its identity, borrowed for the call; `column` is the column
// of a cell in a view with columns (0 for the item's own content, n for its
// sub-item in column n, as Windows Forms' DrawSubItem), else 0. `states`
// are the item's anatomy states, and `text` and `font` are the colour and
// font the control's default content uses for them, so that owner-drawn
// text can match. A handler sets `draw_default` to have the control draw
// its default content over what the handler drew.
//
// A handler paints only: it must not change, dispose or re-parent the
// control, or anything else in its window, while the event is raised.
struct GUI_FORMS_API ItemContentPaint final {
    Painter& painter;
    std::size_t index{};
    std::size_t column{};
    std::string_view stable_id{};
    Rect content{};
    AnatomyStateSet states{};
    FontSpec font{};
    Color text{};
    bool draw_default{};
};

// The text colour `family`'s recipe gives `control` for an item in
// `states`: the colour a control's default item content is drawn in, which
// it reports as ItemContentPaint::text.
[[nodiscard]] GUI_FORMS_API Color item_content_text(const Control& control, ControlVisualRole family,
                                      AnatomyStateSet states) noexcept;

// Raises `event` once with `item`, with `item.painter` saved and clipped to
// `item.content` and restored afterwards (also when a handler throws), so
// that nothing a handler draws reaches the plate, the focus cue or another
// item. Returns whether the control then draws its default content: true
// only when a handler set `draw_default`. An empty or non-finite content
// rectangle raises nothing and returns false.
[[nodiscard]] GUI_FORMS_API bool raise_item_content_paint(Event<ItemContentPaint&>& event, ItemContentPaint& item);

} // namespace gui_forms
