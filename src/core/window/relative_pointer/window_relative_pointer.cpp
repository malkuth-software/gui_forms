// Relative pointer mode, the window's side (gui_forms/events/relative_pointer
// gives the contract): the grant and its fallbacks, the host request, the
// samples, the shell's release regions (suspend and resume) and every end.
// The pointer and key paths call in from window.cpp
// (relative_pointer_takes_pointer, relative_pointer_takes_key); focus,
// focus scopes, subtree revocation and deactivation call the ends.
//
// Holder: the surface root's RelativePointerRecord. The owner is observed;
// an owner that has gone ends the mode as detached, raising nothing.
//
// Reentrancy: a handler of a sample may end the mode, request it again or
// dispose the owner. Each path reads the record again after raising, and an
// end clears the record before it raises relative_pointer_ended.

#include "gui_forms/window.hpp"
#include "gui_forms/host/services/host_services.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace gui_forms {
namespace {

bool finite_rect(Rect rect) noexcept {
    const bool finite = std::isfinite(rect.x) && std::isfinite(rect.y) && std::isfinite(rect.width) &&
                        std::isfinite(rect.height);
    return finite && rect.width > 0.0 && rect.height > 0.0;
}

bool rect_holds(Rect rect, Point point) noexcept {
    const bool inside = point.x >= rect.x && point.y >= rect.y && point.x < rect.x + rect.width &&
                        point.y < rect.y + rect.height;
    return inside;
}

Rect rect_union(Rect left, Rect right) noexcept {
    const double x0 = std::min(left.x, right.x);
    const double y0 = std::min(left.y, right.y);
    const double x1 = std::max(left.x + left.width, right.x + right.width);
    const double y1 = std::max(left.y + left.height, right.y + right.height);
    const Rect joined{x0, y0, x1 - x0, y1 - y0};
    return joined;
}

Rect rect_clip(Rect rect, Rect bound) noexcept {
    const double x0 = std::max(rect.x, bound.x);
    const double y0 = std::max(rect.y, bound.y);
    const double x1 = std::min(rect.x + rect.width, bound.x + bound.width);
    const double y1 = std::min(rect.y + rect.height, bound.y + bound.height);
    const Rect clipped{x0, y0, std::max(0.0, x1 - x0), std::max(0.0, y1 - y0)};
    return clipped;
}

// The nearest point of `area` to `point` (a point on the far edges is moved
// just inside, so rect_holds keeps answering true).
Point clamp_to(Rect area, Point point) noexcept {
    const double right = std::max(area.x, area.x + area.width - 1.0e-6);
    const double bottom = std::max(area.y, area.y + area.height - 1.0e-6);
    const Point clamped{std::clamp(point.x, area.x, right), std::clamp(point.y, area.y, bottom)};
    return clamped;
}

bool holds_control(const Control::Ptr& root, const Control::Ptr& candidate) noexcept {
    if (!root || !candidate) return false;
    if (root == candidate) return true;
    const bool inside = (*root).contains(*candidate);
    return inside;
}

bool known_mode(RelativePointerMode mode) noexcept {
    switch (mode) {
    case RelativePointerMode::free:
    case RelativePointerMode::confined:
    case RelativePointerMode::locked:
        return true;
    default:
        return false;
    }
}

std::uint64_t nanoseconds_of(FrameTime time) noexcept {
    const std::int64_t count = std::chrono::duration_cast<std::chrono::nanoseconds>(time.time_since_epoch()).count();
    const std::uint64_t result = count < 0 ? 0U : static_cast<std::uint64_t>(count);
    return result;
}

} // namespace

RelativePointerSupport SurfaceRoot::relative_pointer_support() const {
    RelativePointerSupport support{};
    if (host_services_ == nullptr) return support;
    const HostServicesSnapshot services = (*host_services_).snapshot();
    const HostCapabilities& capabilities = services.capabilities;
    support.relative_motion = capabilities.supports(HostCapability::relative_pointer);
    support.raw_motion = support.relative_motion && capabilities.supports(HostCapability::raw_pointer_motion);
    support.confine = support.relative_motion && capabilities.supports(HostCapability::pointer_confinement);
    // Holding the pointer still is the host's relative motion with the
    // pointer confined where it stands.
    support.lock = support.confine;
    support.warp = capabilities.supports(HostCapability::pointer_warp);
    return support;
}

Rect SurfaceRoot::relative_pointer_area(const Control& owner) const {
    Rect area = owner.absolute_bounds();
    for (std::size_t index = 0; index < relative_pointer_.region_count; ++index) {
        area = rect_union(area, relative_pointer_.regions[index]);
    }
    const Rect window{0.0, 0.0, client_size_.width, client_size_.height};
    const Rect clipped = rect_clip(area, window);
    return clipped;
}

bool SurfaceRoot::over_release_region(Point window_position) const noexcept {
    for (std::size_t index = 0; index < relative_pointer_.region_count; ++index) {
        if (rect_holds(relative_pointer_.regions[index], window_position)) return true;
    }
    return false;
}

RelativePointerGrant SurfaceRoot::begin_relative_pointer(const Control::Ptr& owner, RelativePointerRequest request) {
    require_root_thread("relative pointer request");
    RelativePointerGrant grant{};
    if (!known_mode(request.mode)) {
        grant.result = RelativePointerResult::invalid_request;
        return grant;
    }
    if (!owner || (*owner).surface_root_ != this || !eligible(owner)) {
        grant.result = RelativePointerResult::detached;
        return grant;
    }
    if (!active_ || !focus_allowed_by_active_scope(owner)) {
        grant.result = RelativePointerResult::not_active;
        return grant;
    }
    PointerButton press_button = PointerButton::none;
    if (request.until_release) {
        if (holds_control(owner, pressed_.lock())) {
            press_button = PointerButton::primary;
        } else if (holds_control(owner, pressed_other_[0].lock())) {
            press_button = PointerButton::secondary;
        } else if (holds_control(owner, pressed_other_[1].lock())) {
            press_button = PointerButton::middle;
        }
        if (press_button == PointerButton::none) {
            grant.result = RelativePointerResult::no_press;
            return grant;
        }
    }
    // Another control's mode ends first; the same owner's is replaced
    // without an end.
    const Control::Ptr current = relative_pointer_.owner.lock();
    if (relative_pointer_.active && current != owner) {
        static_cast<void>(end_relative_pointer(RelativePointerEnd::superseded));
        // A handler of that end may have closed this window's input to the
        // new owner; check again.
        if (!eligible(owner) || !active_ || !focus_allowed_by_active_scope(owner)) {
            grant.result = RelativePointerResult::not_active;
            return grant;
        }
    } else if (relative_pointer_.active) {
        static_cast<void>(apply_relative_pointer_to_host(false));
    }

    // The fallback chain: locked, then confined, then free with deltas the
    // window derives from positions.
    const RelativePointerSupport support = relative_pointer_support();
    RelativePointerMode mode = request.mode;
    if (mode == RelativePointerMode::locked && !support.lock) {
        mode = support.confine ? RelativePointerMode::confined : RelativePointerMode::free;
    }
    if (mode == RelativePointerMode::confined && !support.confine) mode = RelativePointerMode::free;
    grant.mode = mode;
    grant.host_relative = support.relative_motion;
    grant.raw = request.raw_motion && support.raw_motion;
    grant.cursor_hidden = mode == RelativePointerMode::locked ||
                          (mode == RelativePointerMode::confined && request.hide_cursor);

    RelativePointerRecord& record = relative_pointer_;
    record.owner = owner;
    record.request = request;
    record.grant = grant;
    record.active = true;
    record.suspended = false;
    record.escape_taken = false;
    record.press_button = press_button;
    const Rect bounds = (*owner).absolute_bounds();
    const Point centre{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0};
    const Point start = record.last_position.value_or(centre);
    record.virtual_position = clamp_to(relative_pointer_area(*owner), start);
    if (!apply_relative_pointer_to_host(true)) {
        // The host refused the mode: free, with its deltas where it has them.
        record.grant.mode = RelativePointerMode::free;
        record.grant.cursor_hidden = false;
        static_cast<void>(apply_relative_pointer_to_host(true));
    }
    const bool lowered = record.grant.mode != request.mode || (request.raw_motion && !record.grant.raw);
    record.grant.result = lowered ? RelativePointerResult::degraded : RelativePointerResult::granted;
    ++record.grants;
    // No click derives across a mode that takes the buttons, and no
    // capture outlasts them: the mode takes the release that would end it.
    if (record.grant.mode != RelativePointerMode::free) {
        break_pointer_sequences();
        if (captured_.lock()) release_pointer();
    }
    const RelativePointerGrant result = record.grant;
    return result;
}

bool SurfaceRoot::apply_relative_pointer_to_host(bool active) {
    if (host_services_ == nullptr || !relative_pointer_.grant.host_relative) {
        // Without host deltas only free mode exists, and needs nothing.
        const bool done = !active || relative_pointer_.grant.mode == RelativePointerMode::free;
        return done;
    }
    HostRelativePointerRequest request{};
    request.active = active;
    request.mode = relative_pointer_.grant.mode;
    request.hide_cursor = relative_pointer_.grant.cursor_hidden;
    request.raw_motion = relative_pointer_.grant.raw;
    const Control::Ptr owner = relative_pointer_.owner.lock();
    if (owner) request.confine = relative_pointer_area(*owner);
    const HostServiceStatus status = (*host_services_).set_relative_pointer(request);
    const bool accepted = status.accepted() || !active;
    return accepted;
}

bool SurfaceRoot::end_relative_pointer(RelativePointerEnd reason) {
    if (!relative_pointer_.active) return false;
    const Control::Ptr owner = relative_pointer_.owner.lock();
    static_cast<void>(apply_relative_pointer_to_host(false));
    RelativePointerRecord& record = relative_pointer_;
    record.owner.reset();
    record.request = {};
    record.grant = {};
    record.active = false;
    record.suspended = false;
    record.escape_taken = false;
    record.press_button = PointerButton::none;
    ++record.ends;
    record.last_end = reason;
    if (owner && (*owner).is_alive() && (*owner).relative_pointer_events_) {
        RelativePointerEnded ended{};
        ended.reason = reason;
        metrics_.record_callback_emitted();
        (*(*owner).relative_pointer_events_).relative_pointer_ended.emit(ended);
    }
    return true;
}

void SurfaceRoot::end_relative_pointer_for_subtree(const Control::Ptr& control) {
    if (!relative_pointer_.active) return;
    const Control::Ptr owner = relative_pointer_.owner.lock();
    if (!owner || holds_control(control, owner)) {
        static_cast<void>(end_relative_pointer(RelativePointerEnd::detached));
    }
}

void SurfaceRoot::end_relative_pointer_for_focus(const Control::Ptr& focused) {
    // While suspended the ordinary pointer works the shell's region, whose
    // press may take focus: that is the region's use, not a focus loss
    // (what it opens, a menu or dialog, ends the mode as a focus scope).
    if (!relative_pointer_.active || relative_pointer_.suspended || !focused) return;
    const Control::Ptr owner = relative_pointer_.owner.lock();
    if (!holds_control(owner, focused)) {
        static_cast<void>(end_relative_pointer(RelativePointerEnd::focus_lost));
    }
}

void SurfaceRoot::end_relative_pointer_for_scope(const Control::Ptr& scope_root) {
    if (!relative_pointer_.active) return;
    const Control::Ptr owner = relative_pointer_.owner.lock();
    if (!holds_control(scope_root, owner)) {
        static_cast<void>(end_relative_pointer(RelativePointerEnd::modal));
    }
}

bool SurfaceRoot::set_pointer_release_regions(std::span<const Rect> regions) {
    require_root_thread("pointer release regions");
    if (regions.size() > maximum_pointer_release_regions) return false;
    for (const Rect region : regions) {
        if (!finite_rect(region)) return false;
    }
    relative_pointer_.region_count = regions.size();
    std::copy(regions.begin(), regions.end(), relative_pointer_.regions.begin());
    // A confined pointer's area covers the regions: tell the host again.
    if (relative_pointer_.active && !relative_pointer_.suspended &&
        relative_pointer_.grant.mode != RelativePointerMode::free) {
        static_cast<void>(apply_relative_pointer_to_host(true));
    }
    return true;
}

void SurfaceRoot::suspend_relative_pointer(Point window_position) {
    RelativePointerRecord& record = relative_pointer_;
    record.suspended = true;
    ++record.suspensions;
    static_cast<void>(apply_relative_pointer_to_host(false));
    // A locked pointer reappears where its virtual position reached the
    // region, where the host can put it there.
    if (record.grant.mode == RelativePointerMode::locked && host_services_ != nullptr &&
        relative_pointer_support().warp) {
        static_cast<void>((*host_services_).warp_pointer(window_position));
    }
    record.last_position.set(window_position);
}

void SurfaceRoot::resume_relative_pointer() {
    RelativePointerRecord& record = relative_pointer_;
    record.suspended = false;
    ++record.resumptions;
    if (record.last_position.present()) record.virtual_position = *record.last_position.value();
    if (!apply_relative_pointer_to_host(true)) {
        record.grant.mode = RelativePointerMode::free;
        record.grant.cursor_hidden = false;
        static_cast<void>(apply_relative_pointer_to_host(true));
    }
    // Whatever the ordinary pointer pressed over a region stays there.
    break_pointer_sequences();
}

void SurfaceRoot::raise_relative_sample(const Control::Ptr& owner, RelativePointerEventArgs& arguments,
                                        Point window_position) {
    // The owner's own coordinates, through its window frame (stage VA).
    arguments.position = (*owner).point_from_window(window_position);
    arguments.mode = relative_pointer_.grant.mode;
    ++relative_pointer_.samples;
    metrics_.record_callback_emitted();
    if ((*owner).relative_pointer_events_) {
        (*(*owner).relative_pointer_events_).relative_pointer.emit(arguments);
    }
}

bool SurfaceRoot::relative_pointer_takes_pointer(const PointerEvent& event) {
    const Control::Ptr owner = relative_pointer_.owner.lock();
    if (!owner || !eligible(owner)) {
        static_cast<void>(end_relative_pointer(RelativePointerEnd::detached));
        return false;
    }
    const stxlib::Optional<Point> previous = relative_pointer_.last_position;
    if (event.action != PointerAction::leave) relative_pointer_.last_position.set(event.position);
    const bool until_release_ends = relative_pointer_.request.until_release && event.action == PointerAction::up &&
                                    event.button == relative_pointer_.press_button;

    if (relative_pointer_.grant.mode == RelativePointerMode::free) {
        // The ordinary pointer routes on; without host deltas each move is
        // also a sample, its delta from the last position.
        if (event.action == PointerAction::move && !relative_pointer_.grant.host_relative && previous.present()) {
            RelativePointerEventArgs arguments{};
            arguments.kind = RelativePointerSampleKind::motion;
            arguments.delta = Point{event.position.x - (*previous.value()).x, event.position.y - (*previous.value()).y};
            arguments.modifiers = event.modifiers;
            arguments.timestamp_nanoseconds = event.timestamp_nanoseconds;
            raise_relative_sample(owner, arguments, event.position);
        }
        if (until_release_ends && relative_pointer_.active) {
            static_cast<void>(end_relative_pointer(RelativePointerEnd::press_released));
        }
        return false;
    }

    if (relative_pointer_.suspended) {
        // The ordinary pointer, until it leaves every region.
        if (over_release_region(event.position) || event.action != PointerAction::move) return false;
        resume_relative_pointer();
        return true;
    }

    switch (event.action) {
    case PointerAction::move: {
        if (relative_pointer_.grant.mode == RelativePointerMode::confined && over_release_region(event.position)) {
            suspend_relative_pointer(event.position);
            return false;
        }
        // With host deltas an absolute move is no sample (hosts send none;
        // one that arrives is dropped). Without, the window derives it.
        if (!relative_pointer_.grant.host_relative && previous.present()) {
            RelativePointerEventArgs arguments{};
            arguments.kind = RelativePointerSampleKind::motion;
            arguments.delta = Point{event.position.x - (*previous.value()).x, event.position.y - (*previous.value()).y};
            arguments.modifiers = event.modifiers;
            arguments.timestamp_nanoseconds = event.timestamp_nanoseconds;
            raise_relative_sample(owner, arguments, event.position);
        }
        return true;
    }
    case PointerAction::down:
    case PointerAction::up: {
        RelativePointerEventArgs arguments{};
        arguments.kind = event.action == PointerAction::down ? RelativePointerSampleKind::button_down
                                                             : RelativePointerSampleKind::button_up;
        arguments.button = event.button;
        arguments.modifiers = event.modifiers;
        arguments.timestamp_nanoseconds = event.timestamp_nanoseconds;
        const Point at = relative_pointer_.grant.mode == RelativePointerMode::locked
                             ? relative_pointer_.virtual_position
                             : event.position;
        raise_relative_sample(owner, arguments, at);
        if (until_release_ends && relative_pointer_.active) {
            static_cast<void>(end_relative_pointer(RelativePointerEnd::press_released));
        }
        return true;
    }
    case PointerAction::wheel: {
        RelativePointerEventArgs arguments{};
        arguments.kind = RelativePointerSampleKind::wheel;
        arguments.wheel_delta = event.wheel_delta;
        arguments.wheel_unit = event.wheel_unit;
        arguments.modifiers = event.modifiers;
        arguments.timestamp_nanoseconds = event.timestamp_nanoseconds;
        const Point at = relative_pointer_.grant.mode == RelativePointerMode::locked
                             ? relative_pointer_.virtual_position
                             : event.position;
        raise_relative_sample(owner, arguments, at);
        return true;
    }
    case PointerAction::enter:
        return true;
    case PointerAction::leave:
    default:
        // The window's hover bookkeeping still hears the pointer leave.
        return false;
    }
}

bool SurfaceRoot::relative_pointer_takes_key(const KeyEvent& event) {
    const bool escape = event.physical_key == PhysicalKey::escape;
    if (escape && event.action == KeyAction::up && relative_pointer_.escape_taken) {
        relative_pointer_.escape_taken = false;
        return true;
    }
    if (!relative_pointer_.active || !escape || event.action != KeyAction::down) return false;
    static_cast<void>(end_relative_pointer(RelativePointerEnd::escape));
    relative_pointer_.escape_taken = true;
    return true;
}

bool SurfaceRoot::dispatch_relative_motion(const HostRelativeMotionEvent& event) {
    require_root_thread("relative pointer motion");
    if (in_paint_) {
        return defer_input(DeferredInput{event});
    }
    if (!relative_pointer_.active || relative_pointer_.suspended) return false;
    const Control::Ptr owner = relative_pointer_.owner.lock();
    if (!owner || !eligible(owner)) {
        static_cast<void>(end_relative_pointer(RelativePointerEnd::detached));
        return false;
    }
    metrics_.record_input();
    const Rect area = relative_pointer_area(*owner);
    Point at{};
    switch (relative_pointer_.grant.mode) {
    case RelativePointerMode::locked: {
        const Point moved{relative_pointer_.virtual_position.x + event.delta.x,
                          relative_pointer_.virtual_position.y + event.delta.y};
        relative_pointer_.virtual_position = clamp_to(area, moved);
        at = relative_pointer_.virtual_position;
        break;
    }
    case RelativePointerMode::confined: {
        const Point base = relative_pointer_.last_position.value_or(relative_pointer_.virtual_position);
        const Point moved = event.position.value_or(Point{base.x + event.delta.x, base.y + event.delta.y});
        at = clamp_to(area, moved);
        relative_pointer_.last_position.set(at);
        break;
    }
    case RelativePointerMode::free:
    default: {
        const Point base = relative_pointer_.last_position.value_or(relative_pointer_.virtual_position);
        at = event.position.value_or(Point{base.x + event.delta.x, base.y + event.delta.y});
        break;
    }
    }
    // The shell's regions: a confined or locked pointer reaching one is
    // given back there, and this sample is not raised.
    if (relative_pointer_.grant.mode != RelativePointerMode::free && over_release_region(at)) {
        suspend_relative_pointer(at);
        return true;
    }
    RelativePointerEventArgs arguments{};
    arguments.kind = RelativePointerSampleKind::motion;
    arguments.delta = event.delta;
    arguments.modifiers = event.modifiers;
    arguments.unaccelerated = event.unaccelerated;
    arguments.timestamp_nanoseconds =
        event.timestamp_nanoseconds != 0U ? event.timestamp_nanoseconds : nanoseconds_of(input_now());
    raise_relative_sample(owner, arguments, at);
    return true;
}

bool SurfaceRoot::warp_pointer(Point position) {
    require_root_thread("pointer warp");
    if (!relative_pointer_.active || !std::isfinite(position.x) || !std::isfinite(position.y)) return false;
    const Control::Ptr owner = relative_pointer_.owner.lock();
    if (!owner) return false;
    if (relative_pointer_.grant.mode == RelativePointerMode::locked && !relative_pointer_.suspended) {
        // The pointer is hidden and held: only its virtual position moves.
        relative_pointer_.virtual_position = clamp_to(relative_pointer_area(*owner), position);
        return true;
    }
    if (host_services_ == nullptr || !relative_pointer_support().warp) return false;
    const HostServiceStatus status = (*host_services_).warp_pointer(position);
    if (status.accepted()) relative_pointer_.last_position.set(position);
    return status.accepted();
}

RelativePointerSnapshot SurfaceRoot::relative_pointer_snapshot() const noexcept {
    RelativePointerSnapshot snapshot{};
    snapshot.active = relative_pointer_.active;
    snapshot.suspended = relative_pointer_.suspended;
    snapshot.mode = relative_pointer_.grant.mode;
    snapshot.raw = relative_pointer_.grant.raw;
    snapshot.host_relative = relative_pointer_.grant.host_relative;
    snapshot.grants = relative_pointer_.grants;
    snapshot.samples = relative_pointer_.samples;
    snapshot.suspensions = relative_pointer_.suspensions;
    snapshot.resumptions = relative_pointer_.resumptions;
    snapshot.ends = relative_pointer_.ends;
    snapshot.last_end = relative_pointer_.last_end;
    return snapshot;
}

} // namespace gui_forms
