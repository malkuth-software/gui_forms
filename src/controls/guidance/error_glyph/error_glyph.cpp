#include "error_glyph.hpp"

#include "../guidance_utilities.hpp"
#include "gui_forms/resources/stock_icons/stock_icons.hpp"
#include "gui_forms/surface_material/painting/surface_material_painting.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/window.hpp"

#include <algorithm>

namespace gui_forms {

ErrorGlyph::ErrorGlyph(StableId stable_id) : Control(std::move(stable_id)) {
    set_paint_plane(PaintPlane::overlay);
    focusable = false;
    cursor = CursorKind::hand;
    // The glyph shows an error, so the theme draws it in its invalid
    // recipe.
    set_visual_status(ControlVisualStatus::invalid);
}

void ErrorGlyph::set_error(std::string error) {
    if (error_ == error) return;
    error_ = std::move(error);
    invalidate(Dirty::paint | Dirty::semantics);
}

void ErrorGlyph::set_icon(stxlib::Optional<ImageId> icon) {
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
    // An icon part: the icon the program set, else the stock error icon
    // (E10: the solid exclamation circle, the variant nearest the glyph's
    // size), drawn in the theme's invalid recipe's glyph colour, so dark
    // schemes and high contrast follow.
    PartPaint part;
    part.family = ControlVisualRole::editor;
    part.bounds = bounds;
    part.role = PartRole::icon;
    part.image = icon_.value_or(stock_icon_image(stock_icon_error, StockIconStyle::solid));
    paint_part(painter, *this, part);
}

Insets ErrorGlyph::visual_outsets() const noexcept {
    const Insets result = surface_material_visual_outsets(
        effective_theme().resolve(ControlVisualRole::editor, visual_context()).material);
    return result;
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
