// The window capsule in the manager (surface_manager_internal.hpp): its
// drawing from theme parts (CapsuleView), its pointer and key routing, its
// commands carried out on the desk, its animation clock and its help call
// point. The gesture rules are capsule/surface_capsule.cpp's.
#include "surface_manager_internal.hpp"
#include "gui_forms/theme/part_painting/part_painting.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

namespace gui_forms::host::surface_detail {
namespace {

std::uint8_t channel(std::uint32_t rgb, std::uint32_t shift) noexcept {
    const std::uint8_t value = static_cast<std::uint8_t>((rgb >> shift) & 0xFFU);
    return value;
}

// `from` toward `to` by `amount` (0 to 1), at `alpha` (0 to 1).
Color mixed_color(std::uint32_t from, std::uint32_t to, double amount, double alpha) noexcept {
    const double t = std::clamp(amount, 0.0, 1.0);
    const double a = std::clamp(alpha, 0.0, 1.0);
    const double red = static_cast<double>(channel(from, 16U)) * (1.0 - t) + static_cast<double>(channel(to, 16U)) * t;
    const double green = static_cast<double>(channel(from, 8U)) * (1.0 - t) + static_cast<double>(channel(to, 8U)) * t;
    const double blue = static_cast<double>(channel(from, 0U)) * (1.0 - t) + static_cast<double>(channel(to, 0U)) * t;
    const Color color{static_cast<std::uint8_t>(std::lround(red)), static_cast<std::uint8_t>(std::lround(green)),
                      static_cast<std::uint8_t>(std::lround(blue)), static_cast<std::uint8_t>(std::lround(a * 255.0))};
    return color;
}

// A centred square of `fraction` of `area`'s height.
Rect mark_box(Rect area, double fraction) noexcept {
    const double side = std::max(0.0, std::min(area.width, area.height) * fraction);
    const Rect box{area.x + (area.width - side) * 0.5, area.y + (area.height - side) * 0.5, side, side};
    return box;
}

SurfaceCapsuleMeasures capsule_measures(const SurfaceFrameTheme& theme) noexcept {
    SurfaceCapsuleMeasures measures{};
    measures.slop = theme.capsule_slop;
    measures.minimize_distance = theme.capsule_minimize_distance;
    measures.snap_edge = theme.snap_edge;
    measures.snap_release = theme.snap_release;
    measures.animation_milliseconds = theme.capsule_animation_milliseconds;
    measures.nudge = theme.capsule_nudge;
    return measures;
}

} // namespace

SurfaceCapsuleSection capsule_section(SurfaceDeskPart part) noexcept {
    switch (part) {
    case SurfaceDeskPart::capsule_left:
        return SurfaceCapsuleSection::left;
    case SurfaceDeskPart::capsule_help:
        return SurfaceCapsuleSection::help;
    case SurfaceDeskPart::capsule_right:
        return SurfaceCapsuleSection::right;
    default:
        return SurfaceCapsuleSection::none;
    }
}

CapsuleLook capsule_look(const SurfaceCapsuleGesture& gesture, HostWindowId window) noexcept {
    CapsuleLook look{};
    if (!window.valid() || gesture.window() != window) return look;
    look.arm = gesture.arm();
    look.intent = gesture.intent();
    look.minimize_offset = gesture.minimize_offset();
    look.hovered = gesture.hovered();
    look.pressed = gesture.pressed();
    look.focused = gesture.keyboard() ? gesture.focused_section() : SurfaceCapsuleSection::none;
    return look;
}

// ---------------------------------------------------------------------------
// CapsuleView

CapsuleView::CapsuleView(StableId stable_id) : Control(std::move(stable_id)) {}

void CapsuleView::set_look(const CapsuleLook& look, const SurfaceFrameTheme& theme) {
    const bool same = look == look_ && help_width_ == theme.capsule_help_width &&
                      minimize_distance_ == theme.capsule_minimize_distance && face_ == theme.capsule_face &&
                      glyph_ == theme.capsule_glyph && ready_ == theme.capsule_ready && close_ == theme.capsule_close;
    if (same) return;
    look_ = look;
    help_width_ = theme.capsule_help_width;
    minimize_distance_ = theme.capsule_minimize_distance;
    face_ = theme.capsule_face;
    glyph_ = theme.capsule_glyph;
    ready_ = theme.capsule_ready;
    close_ = theme.capsule_close;
    invalidate(Dirty::paint);
}

// Back to front: the capsule's face; the halves' fills, each reaching
// under the question mark by the capsule's radius so its rounded inner end
// is covered; the question mark's own face and text; the halves' marks;
// the focus ring. At the X the whole capsule is one red fill and one mark.
void CapsuleView::on_paint(Painter& painter, Rect local_damage) {
    static_cast<void>(local_damage);
    const Rect whole = client_rectangle();
    if (whole.empty()) return;
    const double radius = whole.height * 0.5;
    const double help = std::min(help_width_, whole.width);
    const double half = (whole.width - help) * 0.5;
    const Rect left{0.0, 0.0, half, whole.height};
    const Rect middle{half, 0.0, help, whole.height};
    const Rect right{half + help, 0.0, whole.width - half - help, whole.height};
    // invite: 0 idle to 1 with one half armed; morph: 0 there to 1 at the X.
    const double invite = std::clamp(look_.intent * 2.0, 0.0, 1.0);
    const double morph = std::clamp(look_.intent * 2.0 - 1.0, 0.0, 1.0);
    const bool left_armed = look_.arm == SurfaceCapsuleArm::left || look_.arm == SurfaceCapsuleArm::close;
    const bool right_armed = look_.arm == SurfaceCapsuleArm::right || look_.arm == SurfaceCapsuleArm::close;
    const bool whole_x = look_.arm == SurfaceCapsuleArm::close;

    PartPaint face;
    face.role = PartRole::face;
    face.family = ControlVisualRole::button;
    face.bounds = whole;
    face.corner_radius = radius;
    face.authored_background = mixed_color(face_, face_, 0.0, 1.0);
    if (look_.hovered != SurfaceCapsuleSection::none) face.states.insert(AnatomyState::hot);
    if (look_.pressed != SurfaceCapsuleSection::none) face.states.insert(AnatomyState::pressed);
    paint_part(painter, *this, face);

    PartPaint fill;
    fill.role = PartRole::fill;
    fill.family = ControlVisualRole::button;
    fill.corner_radius = radius;
    if (whole_x) {
        // Green turning red as the X forms.
        fill.bounds = whole;
        fill.content_color = mixed_color(ready_, close_, morph, 1.0);
        paint_part(painter, *this, fill);
    } else {
        const Rect left_fill{0.0, 0.0, half + radius, whole.height};
        const Rect right_fill{half + help - radius, 0.0, right.width + radius, whole.height};
        // The armed half turns toward the X's red; the other half lights
        // green, inviting the second click. The minimise drag lights the
        // left half as it nears its distance.
        if (left_armed || right_armed) {
            fill.bounds = left_armed ? left_fill : right_fill;
            fill.content_color = mixed_color(face_, close_, 0.6, invite);
            paint_part(painter, *this, fill);
            fill.bounds = left_armed ? right_fill : left_fill;
            fill.content_color = mixed_color(ready_, ready_, 0.0, invite);
            paint_part(painter, *this, fill);
        } else if (look_.minimize_offset > 0.0 && minimize_distance_ > 0.0) {
            fill.bounds = left_fill;
            fill.content_color = mixed_color(ready_, ready_, 0.0, look_.minimize_offset / minimize_distance_);
            paint_part(painter, *this, fill);
        }
        PartPaint question;
        question.role = PartRole::face;
        question.family = ControlVisualRole::button;
        question.bounds = middle;
        question.corner_radius = 0.0;
        question.frame = PartFrame::none;
        question.authored_background = mixed_color(face_, face_, 0.0, 1.0);
        if (look_.hovered == SurfaceCapsuleSection::help) question.states.insert(AnatomyState::hot);
        paint_part(painter, *this, question);
        PartPaint mark_text;
        mark_text.role = PartRole::label;
        mark_text.family = ControlVisualRole::button;
        mark_text.bounds = middle;
        mark_text.text = lend_paint_text("?");
        mark_text.font = FontSpec{FontRole::control, 13, 700, false};
        mark_text.alignment = PartTextAlignment::center;
        mark_text.authored_foreground = mixed_color(glyph_, glyph_, 0.0, 1.0);
        paint_part(painter, *this, mark_text);
    }

    PartPaint glyph;
    glyph.role = PartRole::glyph;
    glyph.family = ControlVisualRole::button;
    glyph.authored_foreground = mixed_color(glyph_, glyph_, 0.0, 1.0);
    if (whole_x) {
        glyph.bounds = mark_box(whole, 0.6);
        glyph.mark = PartMark::close;
        paint_part(painter, *this, glyph);
    } else {
        // The left mark follows the minimise drag down, at most a third of
        // the capsule's height.
        Rect left_mark = mark_box(left, 0.6);
        left_mark.y += std::min(look_.minimize_offset * 0.25, whole.height / 3.0);
        glyph.bounds = left_mark;
        glyph.mark = left_armed ? PartMark::close : PartMark::chevron_down;
        paint_part(painter, *this, glyph);
        glyph.bounds = mark_box(right, 0.6);
        glyph.mark = right_armed ? PartMark::close : PartMark::grip_dots;
        paint_part(painter, *this, glyph);
    }

    if (look_.focused != SurfaceCapsuleSection::none) {
        PartPaint ring;
        ring.role = PartRole::focus;
        ring.family = ControlVisualRole::button;
        ring.states.insert(AnatomyState::focus_visible);
        ring.bounds = middle;
        ring.corner_radius = 0.0;
        if (look_.focused != SurfaceCapsuleSection::help) ring.corner_radius = radius;
        if (look_.focused == SurfaceCapsuleSection::left) ring.bounds = left;
        if (look_.focused == SurfaceCapsuleSection::right) ring.bounds = right;
        if (whole_x) {
            ring.bounds = whole;
            ring.corner_radius = radius;
        }
        paint_part(painter, *this, ring);
    }
}

// ---------------------------------------------------------------------------
// The runtime's capsule

bool Runtime::tile_window(HostWindowId target, SurfaceTileSide side) {
    NativeWindow* found = window(target);
    if (found == nullptr || !desk.tile(target, side)) return false;
    const std::optional<Rect> placed = desk.content(target);
    if (placed.has_value()) (*found).apply_content(*placed);
    (*found).refresh_chrome();
    scene_dirty_ = true;
    return true;
}

void Runtime::show_capsule_look() {
    const HostWindowId current = capsule.window();
    if (capsule_shown_window_.valid() && capsule_shown_window_ != current) {
        NativeWindow* before = window(capsule_shown_window_);
        if (before != nullptr) (*before).refresh_chrome();
    }
    capsule_shown_window_ = current;
    NativeWindow* now = window(current);
    if (now != nullptr) (*now).refresh_chrome();
    // The snap preview may have changed.
    scene_dirty_ = true;
}

void Runtime::forget_capsule(HostWindowId target) {
    if (capsule.window() != target) return;
    if (desk.frame_dragging() && desk.frame_drag_window() == target && capsule.moving()) desk.end_frame_drag();
    if (capsule.forget(target)) show_capsule_look();
}

bool Runtime::capsule_reduced_motion() noexcept {
    const NativeWindow* found = window(capsule.window());
    if (found == nullptr) found = keyboard_window();
    const bool reduced = found != nullptr && (*(*found).entry.model).presentation_settings().reduced_motion;
    return reduced;
}

void Runtime::advance_capsule() {
    capsule.set_measures(capsule_measures(desk.theme()));
    if (!capsule.animating()) {
        capsule_ticking_ = false;
        return;
    }
    const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    double elapsed = 0.0;
    if (capsule_ticking_) elapsed = std::chrono::duration<double, std::milli>(now - capsule_clock_).count();
    capsule_ticking_ = true;
    capsule_clock_ = now;
    if (capsule.advance(elapsed, capsule_reduced_motion())) show_capsule_look();
    if (!capsule.animating()) capsule_ticking_ = false;
}

Rect Runtime::snap_preview_rect() const noexcept {
    if (!capsule.moving()) return Rect{};
    switch (capsule.snap()) {
    case SurfaceCapsuleSnap::left_half:
        return desk.tiled_outer(SurfaceTileSide::left);
    case SurfaceCapsuleSnap::right_half:
        return desk.tiled_outer(SurfaceTileSide::right);
    case SurfaceCapsuleSnap::none:
    default:
        return Rect{};
    }
}

void Runtime::open_capsule_help(HostWindowId target) {
    if (capsule_help.open != nullptr) {
        capsule_help.open(capsule_help.context, target);
        return;
    }
    NativeWindow* found = window(target);
    if (found == nullptr) return;
    HostMessageDialogRequest message;
    message.title = "Window capsule";
    message.message = std::string(surface_capsule_help_text());
    message.buttons = HostMessageButtons::ok;
    message.default_choice = HostDialogChoice::ok;
    HostDialogRequest request;
    request.request_id = next_dialog_ticket();
    request.owner_id = (*found).entry.stable_id;
    request.payload = message;
    static_cast<void>(message_dialog(*found, request, message));
}

void Runtime::capsule_outcome(const SurfaceCapsuleOutcome& outcome) {
    const HostWindowId target = outcome.window;
    switch (outcome.command) {
    case SurfaceCapsuleCommand::toggle_maximize:
        static_cast<void>(toggle_maximized(target));
        break;
    case SurfaceCapsuleCommand::minimize:
        if (minimize_window(target)) forget_capsule(target);
        break;
    case SurfaceCapsuleCommand::begin_move: {
        NativeWindow* found = window(target);
        if (found == nullptr || !desk.begin_capsule_move(target, outcome.origin)) break;
        const std::optional<Rect> placed = desk.content(target);
        if (placed.has_value()) (*found).apply_content(*placed);
        (*found).refresh_chrome();
        const std::optional<Rect> moved = desk.frame_drag_to(outcome.point);
        if (moved.has_value()) (*found).apply_content(*moved);
        break;
    }
    case SurfaceCapsuleCommand::move: {
        if (!desk.frame_dragging() || desk.frame_drag_window() != target) break;
        const std::optional<Rect> moved = desk.frame_drag_to(outcome.point);
        NativeWindow* found = window(target);
        if (moved.has_value() && found != nullptr) (*found).apply_content(*moved);
        break;
    }
    case SurfaceCapsuleCommand::finish_move:
        desk.end_frame_drag();
        if (outcome.snap == SurfaceCapsuleSnap::left_half)
            static_cast<void>(tile_window(target, SurfaceTileSide::left));
        if (outcome.snap == SurfaceCapsuleSnap::right_half)
            static_cast<void>(tile_window(target, SurfaceTileSide::right));
        break;
    case SurfaceCapsuleCommand::cancel_move:
        desk.end_frame_drag();
        break;
    case SurfaceCapsuleCommand::tile: {
        const SurfaceWindowState state = desk.state(target);
        if (outcome.snap == SurfaceCapsuleSnap::left_half) {
            static_cast<void>(tile_window(target, SurfaceTileSide::left));
        } else if (outcome.snap == SurfaceCapsuleSnap::right_half) {
            static_cast<void>(tile_window(target, SurfaceTileSide::right));
        } else if (state != SurfaceWindowState::normal && state != SurfaceWindowState::minimized) {
            static_cast<void>(restore_window(target));
        }
        break;
    }
    case SurfaceCapsuleCommand::nudge: {
        NativeWindow* found = window(target);
        const std::optional<Rect> content = desk.content(target);
        if (found == nullptr || !content.has_value() || !desk.framed(target) || desk.blocked(target)) break;
        const Rect next{(*content).x + outcome.offset.x, (*content).y + outcome.offset.y, (*content).width,
                        (*content).height};
        const std::optional<Rect> placed = desk.set_content(target, next);
        if (placed.has_value()) (*found).apply_content(*placed);
        (*found).refresh_chrome();
        break;
    }
    case SurfaceCapsuleCommand::open_help:
        show_capsule_look();
        open_capsule_help(target);
        break;
    case SurfaceCapsuleCommand::close: {
        show_capsule_look();
        NativeWindow* found = window(target);
        if (found != nullptr) (*found).close(HostCloseReason::user);
        break;
    }
    case SurfaceCapsuleCommand::none:
    default:
        break;
    }
    if (outcome.redraw || outcome.command != SurfaceCapsuleCommand::none) show_capsule_look();
    // With reduced motion the look takes its targets at once.
    advance_capsule();
}

bool Runtime::handle_capsule_pointer(const SurfaceInputRecord& record) {
    if (!capsule.pressing()) return false;
    const SurfacePointerAction action = record.action;
    if (action == SurfacePointerAction::move || action == SurfacePointerAction::enter) {
        capsule_outcome(capsule.drag(record.position, extent));
        return true;
    }
    if (action == SurfacePointerAction::up) {
        const SurfaceDeskHit hit = desk.hit(record.position);
        const SurfaceCapsuleSection section =
            hit.window == capsule.window() ? capsule_section(hit.part) : SurfaceCapsuleSection::none;
        host.pointer_capture(false);
        capsule_outcome(capsule.release(section));
        return true;
    }
    if (action == SurfacePointerAction::cancel) {
        host.pointer_capture(false);
        capsule_outcome(capsule.cancel());
        return true;
    }
    // A leave or a second press while the capsule holds the pointer.
    return true;
}

// ---------------------------------------------------------------------------
// The strip's capsule

void FrameChrome::set_capsule(bool shown, const CapsuleLook& look, bool title_shown) {
    const bool layout_changed = shown != capsule_shown_;
    capsule_shown_ = shown;
    (*capsule_).set_look(look, runtime_.desk.theme());
    const bool title = !shown || title_shown;
    if (title != title_shown_) {
        title_shown_ = title;
        (*title_).text = title ? title_text_ : std::string();
    }
    if (layout_changed) layout();
}

} // namespace gui_forms::host::surface_detail
