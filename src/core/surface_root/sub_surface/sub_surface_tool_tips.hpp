#pragma once

#include "../../window/tool_tips/tool_tip_route.hpp"
#include "../../window/tool_tips/tool_tip_subjects.hpp"
#include "gui_forms/surface_root/sub_surface/sub_surface.hpp"

#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms::detail {

// A sub-surface's tooltip route (design note section 15.2): the sub-surface
// shows no tooltip itself; it asks its parent. It is fed the hooks a window
// feeds its presenter and does no timing: when the thing under the pointer,
// under keyboard focus, or shown by the program changes, it revokes its
// request for the previous thing and posts one for the new thing (a thing
// without text posts nothing). The parent's presenter applies every policy:
// delays, reshow, auto-pop, switches and placement.
//
// At most one request is open per origin (pointer, keyboard, program). An
// open request stays until the thing changes, the sub-surface revokes it, or
// the parent answers declined, revoked or stale; closed (shown, then closed
// by the parent) keeps it, as the parent keeps it.
//
// Owned by its SubSurface (SurfaceRoot::tool_tips_); every member runs on
// the sub-surface's thread.
class SubSurfaceToolTipRequester final : public ToolTipRoute {
public:
    explicit SubSurfaceToolTipRequester(SubSurface& root) noexcept;
    ~SubSurfaceToolTipRequester() override;
    SubSurfaceToolTipRequester(const SubSurfaceToolTipRequester&) = delete;
    SubSurfaceToolTipRequester& operator=(const SubSurfaceToolTipRequester&) = delete;

    void pointer_moved(Point position, const Control::Ptr& hovered, FrameTime now) override;
    // The press goes to the parent too, whose presenter handles it.
    void pointer_pressed() override;
    void pointer_left() override;
    void focus_changed(const Control::Ptr& focused) override;
    void key_handled() override;
    // A keyboard or program request whose thing moved is asked again.
    void root_arranged() override;
    void text_changed(const Control& control) override;
    void items_changed(const Control& control) override;
    // The parent's settings decide; nothing here.
    void settings_changed() override;
    void cover(RuntimeId control, std::string_view item_id, bool covered) override;
    // Revokes the program request, then asks for `target`'s tooltip.
    void show_program(const Control::Ptr& target, std::optional<std::chrono::milliseconds> duration) override;
    // A host nested in a sub-surface shows nothing (not part of stage S3).
    void show_program_item(const Control::Ptr& control, std::string item_id, std::string text, Rect anchor,
                           std::optional<std::chrono::milliseconds> duration) override;
    // Revokes the program request and forwards request `id` as the program
    // request, with its own content; the parent's answers go to the root
    // (answer_tool_tip_request).
    void request(const Control::Ptr& owner, std::uint64_t id, std::string item_id,
                 const ToolTipRequest& content) override;
    void withdraw(std::string_view item_id) override;
    // Revokes the program request (a request_tool_tip one is answered
    // closed); pointer and keyboard tooltips are the parent's to close.
    void hide() override;
    // Forgets every request without posting: the thread is stopping, and the
    // host drops its requests when it stops the thread.
    void shutdown() noexcept override;

    // The request the parent last answered shown, until it is closed or
    // revoked.
    [[nodiscard]] bool visible() const noexcept override;
    [[nodiscard]] Control::Ptr target() const noexcept override;
    [[nodiscard]] std::string item_id() const override;
    [[nodiscard]] std::string text() const override;

    // Focus left through the host: the keyboard request is revoked.
    void focus_left();
    // A new configuration was applied: every open request is asked again,
    // measured in it (the parent drops the old ones as stale).
    void content_epoch_changed();
    // The parent's answer. False when no open request has its id (revoked
    // here first): the answer is dropped.
    bool receive_outcome(const SurfaceRequestOutcome& outcome);

private:
    // One open request.
    struct OpenRequest final {
        std::uint64_t id{};
        ToolTipSubject subject{};
        std::string description{};
        std::uint64_t content_epoch{};
    };

    // Revokes `slot`'s request, if any, and empties it.
    void revoke(std::optional<OpenRequest>& slot);
    // Posts a request for `subject` (which has text) with `description` and
    // records it in `slot`; leaves `slot` empty when there is nowhere to post.
    void post(SubSurfaceToolTipOrigin origin, ToolTipSubject subject, std::string description,
              std::optional<std::chrono::milliseconds> duration, std::optional<OpenRequest>& slot);
    // Finds the thing under the pointer again and asks for it when it
    // differs from the open pointer request (always, with `force`).
    void request_pointer(bool force);
    // The same for the thing under keyboard focus; nothing while the
    // sub-surface is inactive.
    void request_focus(bool force);
    // The same for the program's target, kept for its duration.
    void request_program(bool force);
    [[nodiscard]] const OpenRequest* shown() const noexcept;

    // Observes the root that owns this route.
    SubSurface& root_;
    std::optional<OpenRequest> pointer_{};
    std::optional<OpenRequest> keyboard_{};
    std::optional<OpenRequest> program_{};
    std::optional<std::chrono::milliseconds> program_duration_{};
    // The root's request_tool_tip ID the program request carries; 0 for a
    // show_tool_tip one (its content is the target's own tooltip).
    std::uint64_t explicit_request_{};
    std::string explicit_item_{};
    // Where the pointer was and what it was over, and the region within
    // which the thing under it stays the same (the found thing's anchor, or
    // the region with nothing to ask for).
    Point pointer_position_{};
    std::weak_ptr<Control> hovered_{};
    std::optional<Rect> pointer_region_{};
    std::uint64_t shown_id_{};
    ToolTipCovers covers_{};
};

} // namespace gui_forms::detail
