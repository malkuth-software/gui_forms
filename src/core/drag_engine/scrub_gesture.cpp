#include "scrub_gesture.hpp"

#include "gui_forms/surface_root/surface_root.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <string_view>
#include <system_error>

// The scrub's arithmetic is computed without fused multiply-add, so the
// same pointer script gives the same values on every host.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

namespace gui_forms {

namespace {

bool finite_range(const ScrubRange& range) noexcept {
    const bool finite = std::isfinite(range.start) && std::isfinite(range.minimum) && std::isfinite(range.maximum) &&
                        std::isfinite(range.rate) && std::isfinite(range.snap_step) &&
                        std::isfinite(range.keyboard_step);
    return finite;
}

} // namespace

std::size_t ScrubTarget::describe_scrub(double value, std::span<char> out) const {
    const std::to_chars_result written = std::to_chars(out.data(), out.data() + out.size(), value);
    if (written.ec != std::errc{}) return 0U;
    const std::size_t length = static_cast<std::size_t>(written.ptr - out.data());
    return length;
}

DragEngine* ScrubGesture::engine() const noexcept {
    SurfaceRoot* const root = (*owner_).attached_surface_root();
    if (root == nullptr) return nullptr;
    DragEngine& made = (*root).drag_engine();
    return &made;
}

bool ScrubGesture::running() const noexcept {
    const SurfaceRoot* const root = (*owner_).attached_surface_root();
    if (root == nullptr) return false;
    const DragEngine* const made = (*root).drag_engine_if_made();
    const bool result = made != nullptr && (*made).runs(*this);
    return result;
}

bool ScrubGesture::press(const PointerEvent& event, ScrubPointerLock lock) {
    if (event.action != PointerAction::down || event.button != PointerButton::primary) return false;
    DragEngine* const drags = engine();
    if (drags == nullptr || (*drags).phase() != DragPhase::idle) return false;
    lock_ = lock;
    begun_ = false;
    state_ = ScrubState{};
    const Rect source = (*owner_).absolute_bounds();
    const bool armed = (*drags).arm(*this, *owner_, *owner_, 0U, event.position, source, scrub_threshold);
    if (armed) ++statistics_.presses;
    return armed;
}

ScrubPointerResult ScrubGesture::pointer(const PointerEvent& event) {
    DragEngine* const drags = engine();
    if (drags == nullptr || !(*drags).runs(*this)) return ScrubPointerResult::ignored;
    // A right press while scrubbing restores, as Escape does.
    if (event.action == PointerAction::down && event.button == PointerButton::secondary) {
        (*drags).cancel(DragEnd::escaped);
        return ScrubPointerResult::taken;
    }
    const bool armed = (*drags).phase() == DragPhase::armed;
    const bool taken = (*drags).pointer(event);
    if (taken) return ScrubPointerResult::taken;
    if (armed && event.action == PointerAction::up) {
        ++statistics_.clicks;
        return ScrubPointerResult::clicked;
    }
    return ScrubPointerResult::ignored;
}

bool ScrubGesture::key(const KeyEvent& event) {
    DragEngine* const drags = engine();
    if (drags == nullptr || !(*drags).runs(*this)) return false;
    const bool taken = (*drags).key(event);
    return taken;
}

bool ScrubGesture::begin_keyboard() {
    DragEngine* const drags = engine();
    if (drags == nullptr || (*drags).phase() != DragPhase::idle) return false;
    lock_ = ScrubPointerLock::clamped;
    begun_ = false;
    state_ = ScrubState{};
    const bool started = (*drags).begin_keyboard(*this, *owner_, *owner_, 0U);
    return started;
}

DragPlanRefusal ScrubGesture::plan_drag(DragPlanBuilder& builder, const DragSubject& subject) {
    ScrubRange range{};
    if (!(*target_).begin_scrub(range)) return DragPlanRefusal::client_refused;
    // The target began: it is owed end_scrub, which end_drag sends even
    // for a refusal below (the engine ends a refused plan as rejected).
    begun_ = true;
    range_ = range;
    raw_ = range.start;
    state_.value = range.start;
    if (!finite_range(range) || range.rate <= 0.0 || range.keyboard_step <= 0.0 || range.minimum > range.maximum) {
        return DragPlanRefusal::bad_value;
    }
    low_ = std::min(range.minimum, range.start);
    high_ = std::max(range.maximum, range.start);
    last_pointer_ = subject.press;
    modifiers_ = Modifier::none;
    DragProjection projection{};
    projection.axis = DragAxis::x;
    projection.origin_value = range.start;
    projection.scale = range.rate;
    projection.minimum = low_;
    projection.maximum = high_;
    projection.keyboard_step = range.keyboard_step;
    const DragNodeIndex root = builder.project(projection);
    const DragPlanRefusal refusal = builder.finish(root);
    if (refusal != DragPlanRefusal::none) return refusal;
    state_.scrubbing = true;
    state_.pointer_hidden = subject.mode == DragInputMode::pointer && lock_ == ScrubPointerLock::relative;
    ++statistics_.scrubs;
    return refusal;
}

double ScrubGesture::snapped(double value) const noexcept {
    if (range_.snap_step <= 0.0) return value;
    const double steps = std::round((value - range_.minimum) / range_.snap_step);
    const double grid = range_.minimum + steps * range_.snap_step;
    const double result = std::clamp(grid, low_, high_);
    return result;
}

bool ScrubGesture::on_edge(Point pointer) const noexcept {
    const SurfaceRoot* const root = (*owner_).attached_surface_root();
    if (root == nullptr) return false;
    const Size client = (*root).client_size();
    const bool edge = pointer.x <= 0.0 || pointer.x >= client.width - 1.0;
    return edge;
}

double ScrubGesture::advance_to(Point pointer, Modifier modifiers) noexcept {
    const double travel = pointer.x - last_pointer_.x;
    const double factor = has_modifier(modifiers, Modifier::shift) ? scrub_fine_factor : 1.0;
    const double moved = raw_ + travel * range_.rate * factor;
    raw_ = std::clamp(moved, low_, high_);
    last_pointer_ = pointer;
    modifiers_ = modifiers;
    const bool snapping = has_modifier(modifiers, Modifier::control);
    const double shown = snapping ? snapped(raw_) : raw_;
    return shown;
}

void ScrubGesture::preview_drag(const DragFrame& frame) {
    if (frame.selection.kind != DragSelectionKind::value) return;
    double value = frame.selection.value;
    if (frame.mode == DragInputMode::pointer) {
        value = advance_to(frame.pointer, frame.modifiers);
        const bool edge = lock_ == ScrubPointerLock::clamped && on_edge(frame.pointer);
        state_.at_edge = edge;
        if (edge) ++statistics_.edge_frames;
    }
    state_.value = value;
    ++statistics_.previews;
    (*target_).preview_scrub(value);
}

void ScrubGesture::announce_drag(const DragFrame& frame, DragAnnouncement& announcement) {
    if (frame.selection.kind != DragSelectionKind::value && frame.end == DragEnd::none) return;
    const bool running_now = frame.end == DragEnd::none;
    const bool committed = frame.end == DragEnd::free;
    const double shown = running_now || committed ? state_.value : range_.start;
    std::array<char, 96> text{};
    const std::size_t length = (*target_).describe_scrub(shown, std::span<char>(text.data(), 80U));
    std::string_view suffix{};
    if (committed) suffix = ", done";
    if (!committed && !running_now) suffix = ", restored";
    std::size_t total = std::min(length, std::size_t{80U});
    for (const char character : suffix) {
        if (total >= text.size()) break;
        text[total] = character;
        ++total;
    }
    announcement.assign(std::string_view(text.data(), total));
}

void ScrubGesture::end_drag(const DragOutcome& outcome) {
    const bool begun = begun_;
    begun_ = false;
    state_.scrubbing = false;
    state_.pointer_hidden = false;
    state_.at_edge = false;
    if (!begun) return;
    if (outcome.end == DragEnd::free && outcome.selection.kind == DragSelectionKind::value) {
        double value = outcome.selection.value;
        // The release is not previewed: its travel since the last move
        // counts as one more step, under the last modifiers.
        if (outcome.mode == DragInputMode::pointer) value = advance_to(outcome.pointer, modifiers_);
        state_.value = value;
        ++statistics_.commits;
        (*target_).end_scrub(ScrubEnd::committed, value);
        return;
    }
    state_.value = range_.start;
    ++statistics_.restores;
    (*target_).end_scrub(ScrubEnd::restored, range_.start);
}

} // namespace gui_forms
