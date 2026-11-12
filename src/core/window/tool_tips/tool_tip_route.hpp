#pragma once

#include "gui_forms/control.hpp"
#include "gui_forms/surface_root/requests/surface_requests.hpp"

#include <cstdint>

#include <chrono>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms::detail {

// Where a root's tooltips go (threaded sub-surfaces design note section 15).
// A window shows them itself (ToolTipPresenter); a sub-surface, which never
// shows anything beyond the region it borrows from its parent, asks its
// parent to show them (SubSurfaceToolTipRequester). The root owns its one
// route, made with the root, and calls it only on the root's thread: input
// after the root's own routing, layout, and the root's tooltip API.
class ToolTipRoute {
public:
    virtual ~ToolTipRoute() = default;

    // Root input, after the root's own routing. Positions are in the root's
    // coordinates.
    virtual void pointer_moved(Point position, const Control::Ptr& hovered, FrameTime now) = 0;
    virtual void pointer_pressed() = 0;
    virtual void pointer_left() = 0;
    virtual void focus_changed(const Control::Ptr& focused) = 0;
    virtual void key_handled() = 0;
    // Escape went down (after the root's routing, in place of key_handled):
    // the bubble showing or waiting closes, and does not show again for the
    // same thing until the pointer leaves it or focus moves (WCAG 1.4.13,
    // dismissible without moving the pointer or focus).
    virtual void escape_pressed() = 0;
    // After the root arranged its tree, before its overlays.
    virtual void root_arranged() = 0;
    // `control`'s own tooltip text changed.
    virtual void text_changed(const Control& control) = 0;
    // `control`'s part tooltips (Control::tool_tip_items) changed: a part
    // that shows, waits or is under the pointer or focus is found again.
    virtual void items_changed(const Control& control) = 0;
    virtual void settings_changed() = 0;
    // A focus scope opened (a menu, a drop-down): what the pointer showed
    // beneath it closes.
    virtual void scope_changed() = 0;
    virtual void cover(RuntimeId control, std::string_view item_id, bool covered) = 0;

    // SurfaceRoot::show_tool_tip and hide_tool_tip; the root validated the
    // target and the duration.
    virtual void show_program(const Control::Ptr& target, std::optional<std::chrono::milliseconds> duration) = 0;
    // Shows `control`'s part `item_id` with `text` below `anchor` (root
    // coordinates) for `duration`: a request a sub-surface made through its
    // host. A root that cannot show one ignores it.
    virtual void show_program_item(const Control::Ptr& control, std::string item_id, std::string text,
                                   Rect anchor, std::optional<std::chrono::milliseconds> duration) = 0;
    // SurfaceRoot::request_tool_tip: request `id`, current from now on, as
    // `owner`'s part `item_id`. A window shows it at once; a sub-surface
    // forwards it to its parent. The route answers through the root's
    // answer_tool_tip_request (shown, closed and declined from a window).
    virtual void request(const Control::Ptr& owner, std::uint64_t id, std::string item_id,
                         const ToolTipRequest& content) = 0;
    // The current request, `item_id`, was replaced or withdrawn: closed
    // where it shows.
    virtual void withdraw(std::string_view item_id) = 0;
    virtual void hide() = 0;
    // Drops everything without raising events; the root's destructor calls
    // it first.
    virtual void shutdown() noexcept = 0;

    // What shows, as this root knows it.
    [[nodiscard]] virtual bool visible() const noexcept = 0;
    [[nodiscard]] virtual Control::Ptr target() const noexcept = 0;
    [[nodiscard]] virtual std::string item_id() const = 0;
    [[nodiscard]] virtual std::string text() const = 0;
};

} // namespace gui_forms::detail
