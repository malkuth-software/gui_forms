// GUIFormsView's private opt-in diagnostics (macos_view_diagnostics.hpp)
// and its JSON and snapshot reports (the Diagnostics category).

#include "macos_view.hpp"

namespace gui_forms::host::macos {

std::uint64_t MacCausalTrace::timestamp(const gui_forms::FrameTime value) noexcept {
    // macOS FrameClock is steady_clock with signed nanosecond ticks. Enforce
    // the unit instead of overflowing an unchecked duration conversion.
    static_assert(gui_forms::FrameClock::period::num == 1);
    static_assert(gui_forms::FrameClock::period::den == 1000000000);
    static_assert(std::numeric_limits<gui_forms::FrameClock::duration::rep>::digits <= 64);
    const gui_forms::FrameClock::duration::rep ticks = value.time_since_epoch().count();
    if (ticks < 0) {
        clock_failure = true;
        return 0U;
    }
    const std::uint64_t result = static_cast<std::uint64_t>(ticks);
    return result;
}

void MacCausalTrace::advance(std::uint64_t& serial) noexcept {
    if (serial == std::numeric_limits<std::uint64_t>::max()) serial_exhausted = true;
    else ++serial;
}

void MacCausalTrace::seed(const std::optional<gui_forms::FrameTime>& deadline) noexcept {
    if (!enabled) return;
    has_deadline = deadline.has_value();
    if (deadline) desired_ns = timestamp(*deadline);
}

MacCausalRow MacCausalTrace::make_row(const MacCausalKind kind,
                                      const gui_forms::FrameTime time) noexcept {
    MacCausalRow row{};
    row.kind = kind;
    row.time_ns = timestamp(time);
    row.arm = arm_serial;
    row.desired_ns = desired_ns;
    row.delay_ns = delay_ns;
    row.wake = active_wake;
    row.poll = poll_serial;
    row.draw = draw_serial;
    row.linked_poll = linked_poll;
    row.pending = pending;
    if (has_deadline) row.flags |= causal_has_deadline;
    if (arm_serial == 0U) row.flags |= causal_inherited_arm;
    return row;
}

void MacCausalTrace::append(MacCausalRow row) noexcept {
    if (count != 0U && row.time_ns < last_time_ns) clock_failure = true;
    last_time_ns = row.time_ns;
    if (clock_failure) row.flags |= causal_clock_invalid;
    if (count == rows.size()) {
        overflow = true;
        return;
    }
    rows[count] = row;
    ++count;
}

void MacCausalTrace::record_arm(const gui_forms::FrameTime now,
                                const std::optional<gui_forms::FrameTime>& deadline,
                                const std::int64_t requested_delay_ns) noexcept {
    if (!enabled) return;
    advance(arm_serial);
    seed(deadline);
    if (!deadline) desired_ns = 0U;
    if (requested_delay_ns < 0) {
        clock_failure = true;
        delay_ns = 0U;
    } else delay_ns = static_cast<std::uint64_t>(requested_delay_ns);
    const MacCausalRow row = make_row(MacCausalKind::arm, now);
    append(row);
}

void MacCausalTrace::record_wake(const gui_forms::FrameTime now) noexcept {
    if (!enabled) return;
    const bool nested = active_wake != 0U;
    if (nested) ambiguous = true;
    advance(wake_serial);
    active_wake = wake_serial;
    MacCausalRow row = make_row(MacCausalKind::wake, now);
    if (nested) row.flags |= causal_ambiguous;
    if (has_deadline && row.time_ns < desired_ns) row.flags |= causal_early_wake;
    append(row);
}

void MacCausalTrace::record_poll(const gui_forms::FrameTime now, const std::uint64_t fired,
                                 const std::optional<gui_forms::FrameTime>& next) noexcept {
    if (!enabled) return;
    advance(poll_serial);
    if (fired != 0U) {
        if (pending != 0U || fired != 1U) ambiguous = true;
        if (fired > std::numeric_limits<std::uint64_t>::max() - pending) {
            serial_exhausted = true;
            pending = std::numeric_limits<std::uint64_t>::max();
        } else pending += fired;
        linked_poll = pending == 1U ? poll_serial : 0U;
    }
    MacCausalRow row = make_row(MacCausalKind::poll, now);
    row.fired = fired;
    if (next) {
        row.next_ns = timestamp(*next);
        row.flags |= causal_has_next;
    }
    if (active_wake == 0U) row.flags |= causal_untimed_poll;
    if (pending > 1U) row.flags |= causal_ambiguous;
    append(row);
}

void MacCausalTrace::record_draw_entry(const gui_forms::FrameTime now) noexcept {
    if (!enabled) return;
    if (drawing) ambiguous = true;
    drawing = true;
    advance(draw_serial);
    drawing_pending = pending;
    drawing_poll = linked_poll;
    MacCausalRow row = make_row(MacCausalKind::draw_entry, now);
    if (pending == 0U) {
        unpaired = true;
        row.flags |= causal_unpaired;
    }
    if (pending > 1U) row.flags |= causal_ambiguous;
    append(row);
    pending = 0U;
    linked_poll = 0U;
}

void MacCausalTrace::record_draw_exit(const gui_forms::FrameTime now) noexcept {
    if (!enabled) return;
    MacCausalRow row = make_row(MacCausalKind::draw_exit, now);
    row.pending = drawing_pending;
    row.linked_poll = drawing_poll;
    if (!drawing || drawing_pending == 0U) {
        unpaired = true;
        row.flags |= causal_unpaired;
    }
    if (drawing_pending > 1U) row.flags |= causal_ambiguous;
    append(row);
    drawing = false;
}

MacCausalSnapshot MacCausalTrace::snapshot() const noexcept {
    MacCausalSnapshot output{};
    output[0] = {1U, static_cast<std::uint64_t>(count), overflow, ambiguous,
        clock_failure, serial_exhausted, unpaired, drawing, pending, enabled, 0U, 0U, 0U};
    for (std::size_t index = 0; index < count; ++index) {
        const MacCausalRow& row = rows[index];
        output[index + 1U] = {static_cast<std::uint64_t>(row.kind), row.time_ns,
            row.arm, row.desired_ns, row.delay_ns, row.wake, row.poll, row.fired,
            row.next_ns, row.linked_poll, row.draw, row.pending, row.flags};
    }
    return output;
}

MacCausalWakeScope::MacCausalWakeScope(MacCausalTrace& trace) noexcept
    : trace_(trace), previous_wake_(trace.active_wake) {
    if (trace_.enabled) trace_.record_wake(gui_forms::FrameClock::now());
}

MacCausalWakeScope::~MacCausalWakeScope() { trace_.active_wake = previous_wake_; }

void MacCpuAttribution::add(const std::size_t index, const std::uint64_t amount) noexcept {
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
    if (amount > limit - values[index]) {
        values[index] = limit;
        values[6] = 1U;
    } else {
        values[index] += amount;
    }
}

void MacCpuAttribution::record_draw(const std::uint64_t wall_ns,
                                    const std::array<double, 4>& current,
                                    const std::size_t bytes) noexcept {
    if (values[12] == 0U) {
        extents = current;
        source_bytes = bytes;
        values[8] = wall_ns;
    } else {
        if (current != extents || bytes != source_bytes) add(7U, 1U);
        if (wall_ns < values[9]) {
            add(4U, 1U);
        } else {
            const std::uint64_t gap = wall_ns - values[9];
            if (values[12] == 1U) values[10] = gap;
            else values[10] = std::min(values[10], gap);
            values[11] = std::max(values[11], gap);
        }
    }
    values[9] = wall_ns;
    add(12U, 1U);
}

bool read_mac_thread_cpu(std::uint64_t& output) noexcept {
    timespec value{};
    const int status = clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value);
    if (status != 0 || value.tv_sec < 0 || value.tv_nsec < 0 ||
        value.tv_nsec >= 1000000000L) return false;
    const std::uint64_t seconds = static_cast<std::uint64_t>(value.tv_sec);
    const std::uint64_t fraction = static_cast<std::uint64_t>(value.tv_nsec);
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
    if (seconds > (limit - fraction) / 1000000000U) return false;
    output = seconds * 1000000000U + fraction;
    return true;
}

MacCpuScope::MacCpuScope(MacCpuAttribution& owner, const std::size_t index) noexcept
    : owner_(owner), index_(index) {
    if (owner_.active) {
        owner_.add(5U, 1U);
        return;
    }
    owner_.active = true;
    owns_scope_ = true;
    valid_ = read_mac_thread_cpu(start_);
    if (!valid_) owner_.add(4U, 1U);
}

MacCpuScope::~MacCpuScope() {
    if (!owns_scope_) return;
    if (valid_) {
        std::uint64_t end{0};
        const bool read = read_mac_thread_cpu(end);
        if (!read || end < start_) {
            owner_.add(4U, 1U);
        } else {
            const std::uint64_t elapsed = end - start_;
            owner_.add(index_, 1U);
            owner_.add(index_ + 1U, elapsed);
        }
    }
    owner_.active = false;
}

void MacPaintPhase::record(const std::chrono::steady_clock::time_point start,
                           const std::chrono::steady_clock::time_point end) noexcept {
    const std::chrono::nanoseconds elapsed =
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);
    const std::uint64_t duration = elapsed.count() > 0
        ? static_cast<std::uint64_t>(elapsed.count()) : 0U;
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
    if (calls == limit) saturated = true;
    else ++calls;
    if (duration > limit - nanoseconds) {
        nanoseconds = limit;
        saturated = true;
    } else {
        nanoseconds += duration;
    }
    maximum_nanoseconds = std::max(maximum_nanoseconds, duration);
}

std::string MacPaintPhase::json_fields(const std::string_view name) const {
    const std::string prefix = ",\"paint_" + std::string(name);
    const std::string result = prefix + "_calls\":" + std::to_string(calls) +
        prefix + "_nanoseconds\":" + std::to_string(nanoseconds) +
        prefix + "_maximum_nanoseconds\":" + std::to_string(maximum_nanoseconds) +
        prefix + "_saturated\":" + (saturated ? "true" : "false");
    return result;
}

} // namespace gui_forms::host::macos

@implementation GUIFormsView (Diagnostics)

- (std::string)metricsJSON {
    return _model ? (*_model).metrics_snapshot().to_json() : std::string("{}");
}

- (BOOL)setCpuAttributionEnabled:(const BOOL)enabled {
    if (![NSThread isMainThread] || _cpuAttribution.active) return NO;
    if (enabled == YES) {
        std::unique_ptr<MacCausalTrace> replacement = std::make_unique<MacCausalTrace>();
        (*replacement).enabled = true;
        if (_model) (*replacement).seed((*_model).next_wake());
        _causalTrace = std::move(replacement);
        _cpuAttribution = {};
    } else if (_causalTrace) (*_causalTrace).enabled = false;
    _cpuAttribution.enabled = enabled == YES;
    return YES;
}

- (std::array<std::uint64_t, 13>)cpuAttributionSnapshot {
    return _cpuAttribution.values;
}

- (MacCausalSnapshot)cpuCausalTraceSnapshot {
    MacCausalSnapshot result{};
    if (_causalTrace) result = (*_causalTrace).snapshot();
    return result;
}

- (std::string)hostJSON {
    const std::string session = _hostSession
        ? (*_hostSession).snapshot().to_json() : std::string("{}");
    const std::string services = _hostServices
        ? (*_hostServices).snapshot().to_json() : std::string("{}");
    const NSWindow* nativeWindow = self.window;
    const bool fullSizeContent = nativeWindow != nil &&
        ((nativeWindow.styleMask & NSWindowStyleMaskFullSizeContentView) != 0);
    const bool titleHidden = nativeWindow != nil &&
        nativeWindow.titleVisibility == NSWindowTitleHidden;
    const bool titlebarTransparent = nativeWindow != nil &&
        nativeWindow.titlebarAppearsTransparent;
    const bool closeButtonPresent = nativeWindow != nil &&
        [nativeWindow standardWindowButton:NSWindowCloseButton] != nil;
    const bool minimizeButtonPresent = nativeWindow != nil &&
        [nativeWindow standardWindowButton:NSWindowMiniaturizeButton] != nil;
    const bool zoomButtonPresent = nativeWindow != nil &&
        [nativeWindow standardWindowButton:NSWindowZoomButton] != nil;
    const bool nativeTitlePresent = nativeWindow != nil &&
        nativeWindow.title.length != 0;
    const bool dragRegionConfigured = !_windowDragRegionIds.empty();
    std::size_t resolvedDragRegions{};
    if (_model) {
        for (const std::string& regionId : _windowDragRegionIds) {
            resolvedDragRegions +=
                static_cast<bool>((*_model).find(regionId)) ? 1U : 0U;
        }
    }
    const bool dragRegionResolved =
        dragRegionConfigured && resolvedDragRegions == _windowDragRegionIds.size();
    return "{\"session\":" + session + ",\"services\":" + services +
        ",\"titlebar_presentation\":\"" +
        (_transparentFullSizeContent
             ? std::string("transparent_full_size_content")
             : std::string("standard")) + "\"" +
        ",\"native_full_size_content\":" +
        (fullSizeContent ? "true" : "false") +
        ",\"native_title_hidden\":" + (titleHidden ? "true" : "false") +
        ",\"native_titlebar_transparent\":" +
        (titlebarTransparent ? "true" : "false") +
        ",\"native_close_button_present\":" +
        (closeButtonPresent ? "true" : "false") +
        ",\"native_minimize_button_present\":" +
        (minimizeButtonPresent ? "true" : "false") +
        ",\"native_zoom_button_present\":" +
        (zoomButtonPresent ? "true" : "false") +
        ",\"native_system_title_present\":" +
        (nativeTitlePresent ? "true" : "false") +
        ",\"window_drag_region_configured\":" +
        (dragRegionConfigured ? "true" : "false") +
        ",\"window_drag_region_resolved\":" +
        (dragRegionResolved ? "true" : "false") +
        ",\"window_drag_region_count\":" +
        std::to_string(_windowDragRegionIds.size()) +
        ",\"window_drag_region_resolved_count\":" +
        std::to_string(resolvedDragRegions) +
        ",\"initial_host_occluded\":" + (_initialHostOccluded ? "true" : "false") +
        ",\"host_occluded\":" + (_hostOccluded ? "true" : "false") +
        ",\"scheduled_wake_count\":" + std::to_string(_scheduledWakeCount) +
        ",\"damage_collection_count\":" + std::to_string(_damageCollectionCount) +
        ",\"native_draw_count\":" + std::to_string(_nativeDrawCount) +
        ",\"display_tick_count\":" + std::to_string(_displayTickCount) +
        _rasterPreparePhase.json_fields("raster_prepare") +
        _retainedPaintPhase.json_fields("retained") +
        _rasterFinishPhase.json_fields("raster_finish") +
        _cgSetupPhase.json_fields("cg_setup") +
        _cgDrawPhase.json_fields("cg_draw") +
        _cgReleasePhase.json_fields("cg_release") +
        ",\"last_native_dirty_area\":" + std::to_string(_lastNativeDirtyArea) +
        ",\"last_frame_damage_bounds_area\":" + std::to_string(_lastFrameDamageBoundsArea) +
        ",\"last_cg_destination_area\":" + std::to_string(_lastCGDestinationArea) +
        ",\"last_cg_clip_bounds_area\":" + std::to_string(_lastCGClipBoundsArea) +
        ",\"last_cg_source_bytes\":" + std::to_string(_lastCGSourceBytes) + "}";
}

@end

// This unit's link anchor (macos_view.hpp): referenced from
// link_view_categories in macos_view.mm so that the archive member holding
// the Diagnostics category is linked wherever GUIFormsView is.
namespace gui_forms::host::macos {
void gui_forms_macos_view_diagnostics_anchor() noexcept {}
} // namespace gui_forms::host::macos
