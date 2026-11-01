#include "error_glyph.hpp"

#include "../guidance_utilities.hpp"
#include "gui_forms/window.hpp"

namespace gui_forms {

ErrorGlyph::ErrorGlyph(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    set_focusable(false);
    set_cursor(CursorKind::hand);
}

void ErrorGlyph::set_error(std::string error) {
    if (error_ == error) return;
    error_ = std::move(error);
    invalidate(Dirty::paint | Dirty::semantics);
}

void ErrorGlyph::set_icon(std::optional<ImageId> icon) {
    if (icon_ == icon) return;
    icon_ = icon;
    invalidate(Dirty::paint);
}

void ErrorGlyph::configure_blink(ErrorBlinkStyle style,
                                 std::chrono::milliseconds rate,
                                 bool restart) {
    style_ = style;
    rate_ = rate;
    if (restart) {
        phase_visible_ = true;
        transitions_remaining_ =
            style == ErrorBlinkStyle::blink_if_different_error
                ? guidance_detail::changed_error_blink_transitions
                : 0U;
    }
    refresh_schedule();
    invalidate(Dirty::paint);
}

void ErrorGlyph::refresh_motion_policy() {
    refresh_schedule();
    invalidate(Dirty::paint);
}

bool ErrorGlyph::blink_active() const noexcept {
    return blink_timer_.started();
}

bool ErrorGlyph::phase_visible() const noexcept {
    return phase_visible_;
}

void ErrorGlyph::on_paint(Painter& painter, Rect) {
    blink_timer_.note_painted();
    if (!phase_visible_) return;
    const Rect bounds{0.0, 0.0, committed_arranged_bounds().width,
                      committed_arranged_bounds().height};
    if (icon_) {
        painter.draw_image(*icon_, bounds);
        return;
    }
    painter.draw_box_shadow({1.5, 1.5, bounds.width - 3.0,
                             bounds.height - 3.0},
                            7.0, {0.0, 1.0}, 2.0, 0.0,
                            Color::rgba(35, 28, 24, 72));
    painter.fill_rounded_rect({1.0, 1.0, bounds.width - 2.0,
                               bounds.height - 2.0},
                              7.0, Color::rgba(205, 45, 39));
    painter.stroke_rounded_rect({1.5, 1.5, bounds.width - 3.0,
                                 bounds.height - 3.0},
                                6.5, Color::rgba(128, 22, 19), 1.0);
    painter.draw_text_utf8(
        {5.2, 13.0}, "!",
        effective_font({FontRole::control, 11.0, 700, false}),
        Color::rgba(255, 255, 255));
}

bool ErrorGlyph::hit_test_local(Point point) const {
    return phase_visible_ && Control::hit_test_local(point);
}

SemanticDescriptor ErrorGlyph::semantic_descriptor() const {
    SemanticDescriptor descriptor;
    descriptor.role = SemanticRole::static_text;
    descriptor.name = error_;
    descriptor.states = SemanticState::invalid;
    descriptor.exposed = false;
    descriptor.include_descendants = false;
    return descriptor;
}

void ErrorGlyph::advance_blink() {
    Window* owner = attached_window();
    if (!owner || (*owner).presentation_settings().reduced_motion ||
        style_ == ErrorBlinkStyle::never_blink) {
        phase_visible_ = true;
        blink_timer_.stop();
        invalidate(Dirty::paint);
        return;
    }
    phase_visible_ = !phase_visible_;
    if (style_ == ErrorBlinkStyle::blink_if_different_error) {
        if (transitions_remaining_ > 0U) --transitions_remaining_;
        if (transitions_remaining_ == 0U) {
            phase_visible_ = true;
            blink_timer_.stop();
        }
    }
    invalidate(Dirty::paint);
}

void ErrorGlyph::on_attachment_committed() noexcept {
    try {
        refresh_schedule();
    } catch (...) {
        blink_timer_.stop();
        phase_visible_ = true;
    }
}

void ErrorGlyph::on_detached_from_window() noexcept {
    blink_timer_.stop();
    phase_visible_ = true;
}

void ErrorGlyph::refresh_schedule() {
    blink_timer_.stop();
    Window* owner = attached_window();
    if (!owner || (*owner).presentation_settings().reduced_motion ||
        style_ == ErrorBlinkStyle::never_blink ||
        (style_ == ErrorBlinkStyle::blink_if_different_error &&
         transitions_remaining_ == 0U)) {
        phase_visible_ = true;
        return;
    }
    std::shared_ptr<gui_forms::ErrorGlyph> self = std::dynamic_pointer_cast<ErrorGlyph>(shared_from_this());
    if (!self) return;
    blink_timer_.start(*this, rate_, BlinkTick{self});
}

AnimationTickResult ErrorGlyph::BlinkTick::operator()(FrameTime now) const {
    const std::shared_ptr<ErrorGlyph> live = glyph.lock();
    if (!live) return AnimationTickResult::unchanged;
    static_cast<void>(now);
    (*live).advance_blink();
    return AnimationTickResult::damaged;
}

} // namespace gui_forms
