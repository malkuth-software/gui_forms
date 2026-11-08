#include "gui_forms/window.hpp"

#include "gui_forms/application.hpp"
#include "gui_forms/text.hpp"
#include "tool_tip_route.hpp"
#include "../../scheduler/request/scheduled_frame_request.hpp"

#include <atomic>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace gui_forms {

namespace {

[[nodiscard]] bool finite_rect(Rect rect) noexcept {
    const bool finite = std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
                        std::isfinite(rect.height) && rect.width >= 0.0 && rect.height >= 0.0;
    return finite;
}

} // namespace

void SurfaceRoot::set_tool_tips_shown(std::optional<bool> shown) {
    require_root_thread("tooltip switch");
    if (tool_tips_shown_ == shown) return;
    tool_tips_shown_ = shown;
    (*tool_tips_).settings_changed();
}

bool SurfaceRoot::tool_tips_shown() const noexcept {
    bool program_shown = true;
    if (application_tool_tips_shown_) program_shown = (*application_tool_tips_shown_).load(std::memory_order_relaxed);
    const bool shown = tool_tips_shown_.value_or(program_shown);
    return shown;
}

void SurfaceRoot::set_tool_tip_settings(ToolTipSettings settings) {
    require_root_thread("tooltip settings");
    if (settings.initial_delay.count() < 0 || settings.reshow_delay.count() < 0 ||
        settings.auto_pop_delay.count() < 0) {
        throw std::invalid_argument("GUI.Forms tooltip delays may not be negative");
    }
    if (!std::isfinite(settings.maximum_width) || settings.maximum_width < 80.0 ||
        settings.maximum_width > 2048.0) {
        throw std::invalid_argument("GUI.Forms tooltip maximum width must be finite and between 80 and 2048");
    }
    const bool width_changed = settings.maximum_width != tool_tip_settings_.maximum_width;
    const bool switches_changed = settings.show_on_hover != tool_tip_settings_.show_on_hover ||
                                  settings.show_on_focus != tool_tip_settings_.show_on_focus ||
                                  settings.show_when_disabled != tool_tip_settings_.show_when_disabled;
    tool_tip_settings_ = settings;
    if (width_changed || switches_changed) (*tool_tips_).settings_changed();
}

void SurfaceRoot::show_tool_tip(const Control::Ptr& target, std::optional<std::chrono::milliseconds> duration) {
    require_root_thread("tooltip show");
    if (!target || !(*target).is_alive() || (*target).attached_surface_root() != this) {
        throw std::invalid_argument("GUI.Forms tooltip target must be a live control attached to its Window");
    }
    if ((*target).tool_tip_text.read().empty()) {
        throw std::invalid_argument("GUI.Forms tooltip target has no tooltip text");
    }
    if (duration && (*duration).count() < 0) {
        throw std::invalid_argument("GUI.Forms tooltip duration may not be negative");
    }
    end_tool_tip_request(true);
    (*tool_tips_).show_program(target, duration);
}

std::uint64_t SurfaceRoot::request_tool_tip(const Control::Ptr& owner, ToolTipRequest request,
                                            std::weak_ptr<SurfaceRequestAnswer> answer) {
    require_root_thread("tooltip request");
    if (!owner || !(*owner).is_alive() || (*owner).attached_surface_root() != this) {
        throw std::invalid_argument("GUI.Forms tooltip request owner must be a live control attached to its root");
    }
    if (request.text.empty() || !validate_utf8(request.text).valid()) {
        throw std::invalid_argument("GUI.Forms tooltip request text must be non-empty valid UTF-8");
    }
    if (!finite_rect(request.anchor)) {
        throw std::invalid_argument("GUI.Forms tooltip request anchor must be finite");
    }
    if (request.duration && (*request.duration).count() < 0) {
        throw std::invalid_argument("GUI.Forms tooltip duration may not be negative");
    }
    end_tool_tip_request(true);
    ToolTipRequestRecord record;
    record.id = next_tool_tip_request_;
    ++next_tool_tip_request_;
    record.owner = owner;
    record.item_id = "request." + std::to_string(record.id);
    record.answer = std::move(answer);
    const std::uint64_t id = record.id;
    const std::string item_id = record.item_id;
    tool_tip_request_ = std::move(record);
    (*tool_tips_).request(owner, id, item_id, request);
    return id;
}

void SurfaceRoot::withdraw_tool_tip_request(std::uint64_t id) {
    require_root_thread("tooltip request");
    if (!tool_tip_request_ || (*tool_tip_request_).id != id) return;
    end_tool_tip_request(false);
}

void SurfaceRoot::end_tool_tip_request(bool answer_revoked) {
    if (!tool_tip_request_) return;
    const ToolTipRequestRecord ended = std::move(*tool_tip_request_);
    tool_tip_request_.reset();
    if (answer_revoked && !ended.answer.expired()) {
        SurfaceRequestOutcome outcome;
        outcome.id = ended.id;
        outcome.result = SurfaceRequestResult::revoked;
        deliver_request_answer(ended.answer, outcome);
    }
    (*tool_tips_).withdraw(ended.item_id);
}

void SurfaceRoot::answer_tool_tip_request(std::uint64_t id, SurfaceRequestResult result) {
    if (!tool_tip_request_ || (*tool_tip_request_).id != id) return;
    const std::weak_ptr<SurfaceRequestAnswer> answer = (*tool_tip_request_).answer;
    const bool ends = result == SurfaceRequestResult::declined || result == SurfaceRequestResult::revoked ||
                      result == SurfaceRequestResult::stale;
    if (ends) tool_tip_request_.reset();
    if (answer.expired()) return;
    SurfaceRequestOutcome outcome;
    outcome.id = id;
    outcome.result = result;
    deliver_request_answer(answer, outcome);
}

void SurfaceRoot::tool_tip_request_changed(const ToolTipEvent& change) {
    if (!tool_tip_request_ || change.item_id != (*tool_tip_request_).item_id) return;
    const Control::Ptr owner = (*tool_tip_request_).owner.lock();
    if (!owner || (*owner).stable_id().value() != change.target_stable_id) return;
    answer_tool_tip_request((*tool_tip_request_).id,
                            change.shown ? SurfaceRequestResult::shown : SurfaceRequestResult::closed);
}

void SurfaceRoot::hide_tool_tip() {
    require_root_thread("tooltip hide");
    (*tool_tips_).hide();
}

bool SurfaceRoot::tool_tip_visible() const noexcept {
    const bool visible = (*tool_tips_).visible();
    return visible;
}

Control::Ptr SurfaceRoot::tool_tip_target() const noexcept {
    const Control::Ptr target = (*tool_tips_).target();
    return target;
}

std::string SurfaceRoot::tool_tip_item_id() const {
    return (*tool_tips_).item_id();
}

std::string SurfaceRoot::tool_tip_text() const {
    return (*tool_tips_).text();
}

void SurfaceRoot::tool_tip_text_changed(Control& control) {
    (*tool_tips_).text_changed(control);
}

void SurfaceRoot::tool_tip_items_changed(Control& control) {
    (*tool_tips_).items_changed(control);
}

void SurfaceRoot::show_requested_tool_tip(const Control::Ptr& control, std::string item_id, std::string text,
                                          Rect anchor, std::optional<std::chrono::milliseconds> duration) {
    require_root_thread("tooltip show");
    (*tool_tips_).show_program_item(control, std::move(item_id), std::move(text), anchor, duration);
}

void SurfaceRoot::cover_tool_tip(RuntimeId control, std::string_view item_id, bool covered) {
    (*tool_tips_).cover(control, item_id, covered);
}

FrameRequestToken SurfaceRoot::request_tool_tip_wake(FrameTime deadline, std::function<void(FrameTime)> callback) {
    require_root_thread("tooltip scheduling");
    compact_frame_requests();
    if (frame_requests_.size() >= maximum_scheduled_frame_requests) {
        throw std::length_error("GUI.Forms scheduled frame request limit reached");
    }
    // A one-shot: the presenter disconnects the request when it wakes, so
    // the interval is never reached.
    std::shared_ptr<detail::ScheduledFrameRequest> request = std::make_shared<detail::ScheduledFrameRequest>(
        deadline, std::chrono::duration_cast<FrameInterval>(std::chrono::hours(1)), std::move(callback));
    frame_requests_.push_back(request);
    metrics_.record_frame_request();
    return FrameRequestToken(request);
}

} // namespace gui_forms
