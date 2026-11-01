#include "gui_forms/window.hpp"

#include "gui_forms/application.hpp"
#include "tool_tip_presenter.hpp"
#include "../../scheduler/request/scheduled_frame_request.hpp"

#include <cmath>
#include <stdexcept>

namespace gui_forms {

void Window::set_tool_tips_shown(std::optional<bool> shown) {
    require_ui_thread("tooltip switch");
    if (tool_tips_shown_ == shown) return;
    tool_tips_shown_ = shown;
    (*tool_tip_presenter_).settings_changed();
}

bool Window::tool_tips_shown() const noexcept {
    const bool shown = tool_tips_shown_.value_or(Application::tool_tips_shown());
    return shown;
}

void Window::set_tool_tip_settings(ToolTipSettings settings) {
    require_ui_thread("tooltip settings");
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
    if (width_changed || switches_changed) (*tool_tip_presenter_).settings_changed();
}

void Window::show_tool_tip(const Control::Ptr& target, std::optional<std::chrono::milliseconds> duration) {
    require_ui_thread("tooltip show");
    if (!target || !(*target).is_alive() || (*target).attached_window() != this) {
        throw std::invalid_argument("GUI.Forms tooltip target must be a live control attached to its Window");
    }
    if ((*target).tool_tip_text().empty()) {
        throw std::invalid_argument("GUI.Forms tooltip target has no tooltip text");
    }
    if (duration && (*duration).count() < 0) {
        throw std::invalid_argument("GUI.Forms tooltip duration may not be negative");
    }
    (*tool_tip_presenter_).show_program(target, duration);
}

void Window::hide_tool_tip() {
    require_ui_thread("tooltip hide");
    (*tool_tip_presenter_).hide();
}

bool Window::tool_tip_visible() const noexcept {
    const bool visible = (*tool_tip_presenter_).visible();
    return visible;
}

Control::Ptr Window::tool_tip_target() const noexcept {
    const Control::Ptr target = (*tool_tip_presenter_).target();
    return target;
}

std::string Window::tool_tip_item_id() const {
    return (*tool_tip_presenter_).item_id();
}

std::string Window::tool_tip_text() const {
    return (*tool_tip_presenter_).text();
}

void Window::tool_tip_text_changed(Control& control) {
    (*tool_tip_presenter_).text_changed(control);
}

void Window::cover_tool_tip(RuntimeId control, std::string_view item_id, bool covered) {
    (*tool_tip_presenter_).cover(control, item_id, covered);
}

FrameRequestToken Window::request_tool_tip_wake(FrameTime deadline, std::function<void(FrameTime)> callback) {
    require_ui_thread("tooltip scheduling");
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
    update_frame_schedule_metrics();
    return FrameRequestToken(request);
}

} // namespace gui_forms
