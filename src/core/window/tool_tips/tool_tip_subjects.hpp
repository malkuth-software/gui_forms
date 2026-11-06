#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/window/tool_tips/tool_tips.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace gui_forms::detail {

// What showed a tooltip: the pointer resting over its thing, keyboard focus
// reaching it, or the program (Window::show_tool_tip).
enum class ToolTipOrigin : std::uint8_t { pointer, keyboard, program };

// The thing a tooltip belongs to: a control, or one authored part of it
// (`item_id`, its semantic virtual child's stable ID). `anchor` is the
// thing's bounds in window coordinates. An empty `text` means the thing
// shows no tooltip; `anchor` is then the region over which nothing shows.
struct ToolTipSubject final {
    std::weak_ptr<Control> control{};
    std::string item_id{};
    std::string text{};
    Rect anchor{};
};

// Covers by (control runtime ID, item ID), counted (SurfaceRoot::cover_tool_tip).
using ToolTipCovers = std::map<std::pair<std::uint64_t, std::string>, std::size_t>;

// How a root finds the thing a tooltip belongs to (ToolTipPresenter for a
// window, SubSurfaceToolTipRequester for a sub-surface). Positions and
// anchors are in the root's coordinates. UI thread of that root.
[[nodiscard]] bool contains_point(Rect bounds, Point point) noexcept;
[[nodiscard]] ToolTipSubject tool_tip_subject_of(const Control::Ptr& control, std::string item_id,
                                                 std::string text, Rect anchor);
// Whether a cover hides `control`'s part `item_id` (empty: the control).
[[nodiscard]] bool covered(const ToolTipCovers& covers, const Control& control, std::string_view item_id);
// Whether `control` may show a tooltip now: visible, and enabled unless
// disabled tooltips show.
[[nodiscard]] bool available(const ToolTipSettings& settings, const Control& control) noexcept;
[[nodiscard]] ToolTipSubject resolve_pointer_subject(const Control::Ptr& hovered, Point position,
                                                     const ToolTipSettings& settings,
                                                     const ToolTipCovers& covers);
[[nodiscard]] ToolTipSubject resolve_focus_subject(const Control::Ptr& focused, const ToolTipSettings& settings,
                                                   const ToolTipCovers& covers);
// The same control and part, both with or both without text.
[[nodiscard]] bool same_tool_tip_thing(const ToolTipSubject& left, const ToolTipSubject& right) noexcept;

} // namespace gui_forms::detail
