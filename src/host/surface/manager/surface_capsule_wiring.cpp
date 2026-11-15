// The window capsule in the manager (surface_manager_internal.hpp): its
// control in the title strip (WindowCapsule), its pointer and key routing, its
// commands carried out on the desk, its animation clock and its help call
// point. The gesture rules are WindowCapsuleGesture's
// (src/controls/window_capsule/window_capsule_gesture.cpp).
#include "surface_manager_internal.hpp"
#include "gui_forms/components/business_card/business_card.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>
#include <utility>

namespace gui_forms::host::surface_detail {
namespace {

Color rgb_color(std::uint32_t rgb) noexcept {
    const Color color{static_cast<std::uint8_t>((rgb >> 16U) & 0xFFU), static_cast<std::uint8_t>((rgb >> 8U) & 0xFFU),
                      static_cast<std::uint8_t>(rgb & 0xFFU), 255U};
    return color;
}

WindowCapsuleMeasures capsule_measures(const SurfaceFrameTheme& theme) noexcept {
    WindowCapsuleMeasures measures{};
    measures.slop = theme.capsule_slop;
    measures.minimize_distance = theme.capsule_minimize_distance;
    measures.snap_edge = theme.snap_edge;
    measures.snap_release = theme.snap_release;
    measures.animation_milliseconds = theme.capsule_animation_milliseconds;
    measures.nudge = theme.capsule_nudge;
    return measures;
}

} // namespace

WindowCapsuleSection capsule_section(SurfaceDeskPart part) noexcept {
    switch (part) {
    case SurfaceDeskPart::capsule_left:
        return WindowCapsuleSection::left;
    case SurfaceDeskPart::capsule_help:
        return WindowCapsuleSection::help;
    case SurfaceDeskPart::capsule_right:
        return WindowCapsuleSection::right;
    default:
        return WindowCapsuleSection::none;
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
    case WindowCapsuleSnap::left_half:
        return desk.tiled_outer(SurfaceTileSide::left);
    case WindowCapsuleSnap::right_half:
        return desk.tiled_outer(SurfaceTileSide::right);
    case WindowCapsuleSnap::none:
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
    // The default: the program's business card in the hand over the
    // window, the one card every GUI.Forms program shows
    // (business_card.hpp). A window without a model shows the capsule's own
    // help instead.
    if ((*found).entry.model) {
        static_cast<void>(show_business_card(*(*found).entry.model));
        return;
    }
    HostMessageDialogRequest message;
    message.title = "Window capsule";
    message.message = std::string(window_capsule_help_text());
    message.buttons = HostMessageButtons::ok;
    message.default_choice = HostDialogChoice::ok;
    HostDialogRequest request;
    request.request_id = next_dialog_ticket();
    request.owner_id = (*found).entry.stable_id;
    request.payload = message;
    static_cast<void>(message_dialog(*found, request, message));
}

void Runtime::capsule_outcome(const WindowCapsuleOutcome& outcome) {
    const HostWindowId target = outcome.window;
    switch (outcome.command) {
    case WindowCapsuleCommand::toggle_maximize:
        static_cast<void>(toggle_maximized(target));
        break;
    case WindowCapsuleCommand::minimize:
        if (minimize_window(target)) forget_capsule(target);
        break;
    case WindowCapsuleCommand::begin_move: {
        NativeWindow* found = window(target);
        if (found == nullptr || !desk.begin_capsule_move(target, outcome.origin)) break;
        const std::optional<Rect> placed = desk.content(target);
        if (placed.has_value()) (*found).apply_content(*placed);
        (*found).refresh_chrome();
        const std::optional<Rect> moved = desk.frame_drag_to(outcome.point);
        if (moved.has_value()) (*found).apply_content(*moved);
        break;
    }
    case WindowCapsuleCommand::move: {
        if (!desk.frame_dragging() || desk.frame_drag_window() != target) break;
        const std::optional<Rect> moved = desk.frame_drag_to(outcome.point);
        NativeWindow* found = window(target);
        if (moved.has_value() && found != nullptr) (*found).apply_content(*moved);
        break;
    }
    case WindowCapsuleCommand::finish_move:
        desk.end_frame_drag();
        if (outcome.snap == WindowCapsuleSnap::left_half)
            static_cast<void>(tile_window(target, SurfaceTileSide::left));
        if (outcome.snap == WindowCapsuleSnap::right_half)
            static_cast<void>(tile_window(target, SurfaceTileSide::right));
        break;
    case WindowCapsuleCommand::cancel_move:
        desk.end_frame_drag();
        break;
    case WindowCapsuleCommand::tile: {
        const SurfaceWindowState state = desk.state(target);
        if (outcome.snap == WindowCapsuleSnap::left_half) {
            static_cast<void>(tile_window(target, SurfaceTileSide::left));
        } else if (outcome.snap == WindowCapsuleSnap::right_half) {
            static_cast<void>(tile_window(target, SurfaceTileSide::right));
        } else if (state != SurfaceWindowState::normal && state != SurfaceWindowState::minimized) {
            static_cast<void>(restore_window(target));
        }
        break;
    }
    case WindowCapsuleCommand::nudge: {
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
    case WindowCapsuleCommand::open_help:
        show_capsule_look();
        open_capsule_help(target);
        break;
    case WindowCapsuleCommand::close: {
        show_capsule_look();
        NativeWindow* found = window(target);
        if (found != nullptr) (*found).close(HostCloseReason::user);
        break;
    }
    case WindowCapsuleCommand::none:
    default:
        break;
    }
    if (outcome.redraw || outcome.command != WindowCapsuleCommand::none) show_capsule_look();
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
        const WindowCapsuleSection section =
            hit.window == capsule.window() ? capsule_section(hit.part) : WindowCapsuleSection::none;
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

void FrameChrome::set_capsule(bool shown, const WindowCapsuleLook& look, bool title_shown) {
    const bool layout_changed = shown != capsule_shown_;
    capsule_shown_ = shown;
    const SurfaceFrameTheme& theme = runtime_.desk.theme();
    WindowCapsuleColors colors{};
    colors.face = rgb_color(theme.capsule_face);
    colors.glyph = rgb_color(theme.capsule_glyph);
    colors.ready = rgb_color(theme.capsule_ready);
    colors.close = rgb_color(theme.capsule_close);
    (*capsule_).set_colors(colors);
    (*capsule_).set_help_width(theme.capsule_help_width);
    (*capsule_).set_measures(capsule_measures(theme));
    (*capsule_).set_look(look);
    const bool title = !shown || title_shown;
    if (title != title_shown_) {
        title_shown_ = title;
        (*title_).text = title ? title_text_ : std::string();
    }
    if (layout_changed) layout();
}

} // namespace gui_forms::host::surface_detail
