#include "display_link_trace.hpp"
#import <AppKit/AppKit.h>
// kAEQuitReason and the session-end reasons (AERegistry.h); constants only.
#import <CoreServices/CoreServices.h>
#import <CoreGraphics/CoreGraphics.h>
#import <QuartzCore/CABase.h>
#import <QuartzCore/CADisplayLink.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include "macos_host.hpp"
#include "../../../core/failure/fail_fast.hpp"
#include "application_menu.hpp"
#include "front_reuse.hpp"
#include "live_surface_damage.hpp"
#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/live_surface.hpp"
#include "gui_forms/text/types/text_types.hpp"
#include "../../../core/damage/device_damage/device_damage.hpp"
#include "../../../render/skia/raster/skia_raster.hpp"
#include "../../../render/skia/raster/skia_splash_bake.hpp"
#include "../../../core/host/frame_capture/frame_capture.hpp"
#include "../../../core/application/splashscreen_surface.hpp"
#include "../compositor/macos_compositor.hpp"
#include "../../posix/fonts/sealed_pages_posix.hpp"
#include "../../../core/resources/fonts/bundled_font_set.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <exception>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <time.h>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

using gui_forms::DamageRegion;
using gui_forms::DragAction;
using gui_forms::DragBinaryData;
using gui_forms::DragDataItem;
using gui_forms::DragEffect;
using gui_forms::DragEvent;
using gui_forms::DragFileListData;
using gui_forms::DragLimits;
using gui_forms::DragTextData;
using gui_forms::HostActivationEvent;
using gui_forms::HostAttachEvent;
using gui_forms::HostCapability;
using gui_forms::HostCloseReason;
using gui_forms::HostCloseRequest;
using gui_forms::HostClipboardTextResult;
using gui_forms::HostClosedEvent;
using gui_forms::HostContextRequestEvent;
using gui_forms::ContextRequestOrigin;
using gui_forms::HostDispatchResult;
using gui_forms::HostDialogChoice;
using gui_forms::HostDialogOutcome;
using gui_forms::HostDialogRequest;
using gui_forms::HostDialogResult;
using gui_forms::HostColorDialogRequest;
using gui_forms::HostColorDialogResult;
using gui_forms::HostDisplayEvent;
using gui_forms::HostFolderDialogRequest;
using gui_forms::HostMessageButtons;
using gui_forms::HostMessageDialogRequest;
using gui_forms::HostMessageDialogResult;
using gui_forms::HostMessageIcon;
using gui_forms::HostOpenFileDialogRequest;
using gui_forms::HostPathDialogResult;
using gui_forms::HostSaveFileDialogRequest;
using gui_forms::HostEvent;
using gui_forms::HostEventPayload;
using gui_forms::HostOcclusionEvent;
using gui_forms::HostResizeEvent;
using gui_forms::HostScaleEvent;
using gui_forms::HostMonitor;
using gui_forms::HostMonitorResult;
using gui_forms::HostServiceError;
using gui_forms::HostServiceStatus;
using gui_forms::HostSoundCue;
using gui_forms::HostSoundCueRequest;
using gui_forms::HostServices;
using gui_forms::HostSession;
using gui_forms::HostShutdownEvent;
using gui_forms::KeyAction;
using gui_forms::KeyEvent;
using gui_forms::LiveSurfacePresentation;
using gui_forms::Modifier;
using gui_forms::PaintReceipt;
using GFPoint = gui_forms::Point;
using gui_forms::PointerAction;
using gui_forms::PointerButton;
using gui_forms::PointerEvent;
using gui_forms::WheelDeltaUnit;
using gui_forms::SemanticAction;
using gui_forms::SemanticNode;
using gui_forms::SemanticOrientation;
using gui_forms::SemanticRole;
using gui_forms::SemanticState;
using gui_forms::SemanticTextGrapheme;
using gui_forms::SemanticTextLayout;
using gui_forms::SemanticTextLine;
using gui_forms::SemanticTextSelection;
using GFRect = gui_forms::Rect;
using GFSize = gui_forms::Size;
using gui_forms::TextInputEvent;
using gui_forms::Window;
using gui_forms::CursorKind;
using gui_forms::render::SkiaRaster;

namespace {
// Private fixture wire image: header then 256 rows, each with 13 uint64 fields.
// Header: version, count, overflow, ambiguity, clock failure, serial exhaustion,
// unpaired draw, unfinished draw, pending deadlines, enabled, reserved x3.
// Row: kind, time ns, arm, desired ns, delay ns, wake, poll, fired, next ns,
// linked poll, draw, pending count, flags. No native pointers cross this boundary.
using MacCausalSnapshot = std::array<std::array<std::uint64_t, 13>, 257>;

enum class MacCausalKind : std::uint64_t { arm = 1, wake, poll, draw_entry, draw_exit };
enum MacCausalFlag : std::uint64_t {
    causal_has_deadline = 1U, causal_has_next = 2U, causal_early_wake = 4U,
    causal_untimed_poll = 8U, causal_ambiguous = 16U, causal_unpaired = 32U,
    causal_clock_invalid = 64U, causal_inherited_arm = 128U,
};
struct MacCausalRow final {
    MacCausalKind kind{MacCausalKind::arm};
    std::uint64_t time_ns{0};
    std::uint64_t arm{0};
    std::uint64_t desired_ns{0};
    std::uint64_t delay_ns{0};
    std::uint64_t wake{0};
    std::uint64_t poll{0};
    std::uint64_t fired{0};
    std::uint64_t next_ns{0};
    std::uint64_t linked_poll{0};
    std::uint64_t draw{0};
    std::uint64_t pending{0};
    std::uint64_t flags{0};
};
struct MacCausalTrace final {
    // Main-executor owned; allocated only when the private diagnostic is enabled.
    // Overflow retains the first 256 rows and permanently invalidates pairing.
    std::array<MacCausalRow, 256> rows{};
    std::size_t count{0};
    bool enabled{false};
    bool overflow{false};
    bool ambiguous{false};
    bool clock_failure{false};
    bool serial_exhausted{false};
    bool unpaired{false};
    bool drawing{false};
    bool has_deadline{false};
    std::uint64_t last_time_ns{0};
    std::uint64_t arm_serial{0};
    std::uint64_t wake_serial{0};
    std::uint64_t poll_serial{0};
    std::uint64_t draw_serial{0};
    std::uint64_t active_wake{0};
    std::uint64_t desired_ns{0};
    std::uint64_t delay_ns{0};
    std::uint64_t pending{0};
    std::uint64_t linked_poll{0};
    std::uint64_t drawing_pending{0};
    std::uint64_t drawing_poll{0};

    [[nodiscard]] std::uint64_t timestamp(const gui_forms::FrameTime value) noexcept {
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
    void advance(std::uint64_t& serial) noexcept {
        if (serial == std::numeric_limits<std::uint64_t>::max()) serial_exhausted = true;
        else ++serial;
    }
    void seed(const std::optional<gui_forms::FrameTime>& deadline) noexcept {
        if (!enabled) return;
        has_deadline = deadline.has_value();
        if (deadline) desired_ns = timestamp(*deadline);
    }
    [[nodiscard]] MacCausalRow make_row(const MacCausalKind kind,
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
    void append(MacCausalRow row) noexcept {
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
    void record_arm(const gui_forms::FrameTime now,
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
    void record_wake(const gui_forms::FrameTime now) noexcept {
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
    void record_poll(const gui_forms::FrameTime now, const std::uint64_t fired,
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
    void record_draw_entry(const gui_forms::FrameTime now) noexcept {
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
    void record_draw_exit(const gui_forms::FrameTime now) noexcept {
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
    [[nodiscard]] MacCausalSnapshot snapshot() const noexcept {
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
};

class MacCausalWakeScope final {
public:
    explicit MacCausalWakeScope(MacCausalTrace& trace) noexcept
        : trace_(trace), previous_wake_(trace.active_wake) {
        if (trace_.enabled) trace_.record_wake(gui_forms::FrameClock::now());
    }
    ~MacCausalWakeScope() { trace_.active_wake = previous_wake_; }
    MacCausalWakeScope(const MacCausalWakeScope&) = delete;
    MacCausalWakeScope& operator=(const MacCausalWakeScope&) = delete;
private:
    MacCausalTrace& trace_; // View-owned trace outlives this callback scope.
    const std::uint64_t previous_wake_;
};

// Private opt-in diagnostics. Array layout is shared only with the native test:
// draw calls/ns, collect calls/ns, clock failures, overlapping scopes, saturation,
// extent mismatches, first/last draw wall ns, min/max draw gap ns, draw count.
// All access is on the main executor. Disabled views never sample a CPU clock.
struct MacCpuAttribution final {
    std::array<std::uint64_t, 13> values{};
    std::array<double, 4> extents{};
    std::size_t source_bytes{0};
    bool enabled{false};
    bool active{false};

    void add(const std::size_t index, const std::uint64_t amount) noexcept {
        const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max();
        if (amount > limit - values[index]) {
            values[index] = limit;
            values[6] = 1U;
        } else {
            values[index] += amount;
        }
    }

    void record_draw(const std::uint64_t wall_ns,
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
};

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

class MacCpuScope final {
public:
    explicit MacCpuScope(MacCpuAttribution& owner, const std::size_t index) noexcept
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
    ~MacCpuScope() {
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
    MacCpuScope(const MacCpuScope&) = delete;
    MacCpuScope& operator=(const MacCpuScope&) = delete;
private:
    MacCpuAttribution& owner_; // Borrowed view field outlives this stack scope.
    const std::size_t index_;
    std::uint64_t start_{0};
    bool owns_scope_{false};
    bool valid_{false};
};

// Completed synchronous wall phases; not CPU, deferred AppKit work or bytes copied.
struct MacPaintPhase final {
    std::uint64_t calls{0};
    std::uint64_t nanoseconds{0};
    std::uint64_t maximum_nanoseconds{0};
    bool saturated{false};

    void record(const std::chrono::steady_clock::time_point start,
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

    [[nodiscard]] std::string json_fields(const std::string_view name) const {
        const std::string prefix = ",\"paint_" + std::string(name);
        const std::string result = prefix + "_calls\":" + std::to_string(calls) +
            prefix + "_nanoseconds\":" + std::to_string(nanoseconds) +
            prefix + "_maximum_nanoseconds\":" + std::to_string(maximum_nanoseconds) +
            prefix + "_saturated\":" + (saturated ? "true" : "false");
        return result;
    }
};
} // namespace

static std::uint64_t host_now_nanoseconds() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

static std::uint64_t host_event_nanoseconds(NSEvent* event) {
    if (event == nil || !std::isfinite(event.timestamp) || event.timestamp < 0.0) {
        return host_now_nanoseconds();
    }
    return static_cast<std::uint64_t>(event.timestamp * 1'000'000'000.0);
}

static Modifier modifiers_for(NSEventModifierFlags flags) {
    Modifier result = Modifier::none;
    if ((flags & NSEventModifierFlagShift) != 0) {
        result = result | Modifier::shift;
    }
    if ((flags & NSEventModifierFlagControl) != 0) {
        result = result | Modifier::control;
    }
    if ((flags & NSEventModifierFlagOption) != 0) {
        result = result | Modifier::alt;
    }
    if ((flags & NSEventModifierFlagCommand) != 0) {
        result = result | Modifier::meta;
    }
    return result;
}

static PointerButton button_for(NSInteger button_number) {
    if (button_number == 0) {
        return PointerButton::primary;
    }
    if (button_number == 1) {
        return PointerButton::secondary;
    }
    if (button_number == 2) {
        return PointerButton::middle;
    }
    return PointerButton::none;
}

// AppKit keyCode values describe positions in Apple's hardware vocabulary.
// Translate them at the adapter boundary so retained controls see the same USB
// HID usages on every host. Text remains owned by NSTextInputClient below.
static std::uint32_t physical_key_for(unsigned short key_code) {
    switch (key_code) {
    case 0: return 0x04U;   // A
    case 1: return 0x16U;   // S
    case 2: return 0x07U;   // D
    case 3: return 0x09U;   // F
    case 4: return 0x0BU;   // H
    case 5: return 0x0AU;   // G
    case 6: return 0x1DU;   // Z
    case 7: return 0x1BU;   // X
    case 8: return 0x06U;   // C
    case 9: return 0x19U;   // V
    case 10: return 0x64U;  // ISO section key (Non-US backslash; N108)
    case 11: return 0x05U;  // B
    case 12: return 0x14U;  // Q
    case 13: return 0x1AU;  // W
    case 14: return 0x08U;  // E
    case 15: return 0x15U;  // R
    case 16: return 0x1CU;  // Y
    case 17: return 0x17U;  // T
    case 18: return 0x1EU;  // 1
    case 19: return 0x1FU;  // 2
    case 20: return 0x20U;  // 3
    case 21: return 0x21U;  // 4
    case 22: return 0x23U;  // 6
    case 23: return 0x22U;  // 5
    case 24: return 0x2EU;  // =
    case 25: return 0x26U;  // 9
    case 26: return 0x24U;  // 7
    case 27: return 0x2DU;  // -
    case 28: return 0x25U;  // 8
    case 29: return 0x27U;  // 0
    case 30: return 0x30U;  // ]
    case 31: return 0x12U;  // O
    case 32: return 0x18U;  // U
    case 33: return 0x2FU;  // [
    case 34: return 0x0CU;  // I
    case 35: return 0x13U;  // P
    case 36: return 0x28U;  // Return
    case 37: return 0x0FU;  // L
    case 38: return 0x0DU;  // J
    case 39: return 0x34U;  // '
    case 40: return 0x0EU;  // K
    case 41: return 0x33U;  // ;
    case 42: return 0x31U;  // backslash
    case 43: return 0x36U;  // comma
    case 44: return 0x38U;  // slash
    case 45: return 0x11U;  // N
    case 46: return 0x10U;  // M
    case 47: return 0x37U;  // period
    case 48: return 0x2BU;  // Tab
    case 49: return 0x2CU;  // Space
    case 50: return 0x35U;  // grave
    case 51: return 0x2AU;  // Backspace
    case 53: return 0x29U;  // Escape
    case 54: return 0xE7U;  // Right Command
    case 55: return 0xE3U;  // Left Command
    case 56: return 0xE1U;  // Left Shift
    case 57: return 0x39U;  // Caps Lock
    case 58: return 0xE2U;  // Left Option
    case 59: return 0xE0U;  // Left Control
    case 60: return 0xE5U;  // Right Shift
    case 61: return 0xE6U;  // Right Option
    case 62: return 0xE4U;  // Right Control
    case 64: return 0x6CU;  // F17
    case 65: return 0x63U;  // Keypad decimal
    case 67: return 0x55U;  // Keypad multiply
    case 69: return 0x57U;  // Keypad plus
    case 71: return 0x53U;  // Keypad clear
    case 75: return 0x54U;  // Keypad divide
    case 76: return 0x58U;  // Keypad enter
    case 78: return 0x56U;  // Keypad minus
    case 79: return 0x6DU;  // F18 (N108)
    case 80: return 0x6EU;  // F19 (N108)
    case 81: return 0x67U;  // Keypad equals
    case 82: return 0x62U;  // Keypad 0
    case 83: return 0x59U;  // Keypad 1
    case 84: return 0x5AU;  // Keypad 2
    case 85: return 0x5BU;  // Keypad 3
    case 86: return 0x5CU;  // Keypad 4
    case 87: return 0x5DU;  // Keypad 5
    case 88: return 0x5EU;  // Keypad 6
    case 89: return 0x5FU;  // Keypad 7
    case 90: return 0x6FU;  // F20 (N108)
    case 91: return 0x60U;  // Keypad 8
    case 92: return 0x61U;  // Keypad 9
    case 93: return 0x89U;  // JIS Yen (International3; N108)
    case 94: return 0x87U;  // JIS underscore (International1; N108)
    case 95: return 0x85U;  // JIS keypad comma (N108)
    case 96: return 0x3EU;  // F5
    case 97: return 0x3FU;  // F6
    case 98: return 0x40U;  // F7
    case 99: return 0x3CU;  // F3
    case 100: return 0x41U; // F8
    case 101: return 0x42U; // F9
    case 102: return 0x91U; // JIS Eisu (LANG2; N108)
    case 103: return 0x44U; // F11
    case 104: return 0x90U; // JIS Kana (LANG1; N108)
    case 105: return 0x68U; // F13
    case 106: return 0x6BU; // F16
    case 107: return 0x69U; // F14
    case 109: return 0x43U; // F10
    // Apple keyboards have no context-menu key; a PC keyboard's reports this
    // code (kVK_ContextualMenu in recent SDKs).
    case 110: return 0x65U; // Context menu
    case 111: return 0x45U; // F12
    case 113: return 0x6AU; // F15
    case 114: return 0x49U; // Help/Insert
    case 115: return 0x4AU; // Home
    case 116: return 0x4BU; // Page Up
    case 117: return 0x4CU; // Forward Delete
    case 118: return 0x3DU; // F4
    case 119: return 0x4DU; // End
    case 120: return 0x3BU; // F2
    case 121: return 0x4EU; // Page Down
    case 122: return 0x3AU; // F1
    case 123: return 0x50U; // Left
    case 124: return 0x4FU; // Right
    case 125: return 0x51U; // Down
    case 126: return 0x52U; // Up
    default: return 0U;
    }
}

static DragEffect drag_effects_for(NSDragOperation operations) {
    DragEffect result = DragEffect::none;
    if ((operations & NSDragOperationCopy) != 0) {
        result = result | DragEffect::copy;
    }
    if ((operations & NSDragOperationMove) != 0) {
        result = result | DragEffect::move;
    }
    if ((operations & NSDragOperationLink) != 0) {
        result = result | DragEffect::link;
    }
    return result;
}

static NSDragOperation native_drag_operation(DragEffect effect) {
    switch (effect) {
    case DragEffect::copy: return NSDragOperationCopy;
    case DragEffect::move: return NSDragOperationMove;
    case DragEffect::link: return NSDragOperationLink;
    case DragEffect::none: return NSDragOperationNone;
    }
    return NSDragOperationNone;
}

static std::vector<DragDataItem> drag_items_for(NSPasteboard* pasteboard) {
    std::vector<DragDataItem> result;
    std::size_t total_bytes = 0;
    NSArray<NSURL*>* urls = [pasteboard
        readObjectsForClasses:@[[NSURL class]]
                    options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    DragFileListData files;
    if (urls.count > DragLimits::maximum_paths) {
        return {};
    }
    for (NSURL* url in urls) {
        NSString* native_path = url.path;
        const NSUInteger byte_count =
            [native_path lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        if (byte_count == 0 || byte_count > DragLimits::maximum_path_bytes ||
            byte_count > DragLimits::maximum_total_bytes - total_bytes) {
            return {};
        }
        const char* path = native_path.UTF8String;
        if (path == nullptr) {
            return {};
        }
        files.paths_utf8.emplace_back(path, byte_count);
        total_bytes += byte_count;
    }
    if (!files.paths_utf8.empty()) {
        result.emplace_back(std::move(files));
    }
    NSString* text = [pasteboard stringForType:NSPasteboardTypeString];
    if (text != nil) {
        const NSUInteger byte_count =
            [text lengthOfBytesUsingEncoding:NSUTF8StringEncoding];
        if (byte_count > DragLimits::maximum_text_bytes ||
            byte_count > DragLimits::maximum_total_bytes - total_bytes) {
            return {};
        }
        const char* bytes = text.UTF8String;
        if (bytes != nullptr) {
            result.emplace_back(DragTextData{std::string(bytes, byte_count)});
            total_bytes += byte_count;
        }
    }
    NSPasteboardType type = [pasteboard availableTypeFromArray:@[@"public.data"]];
    if (type != nil && ![type isEqualToString:NSPasteboardTypeString] &&
        ![type isEqualToString:NSPasteboardTypeFileURL]) {
        NSData* data = [pasteboard dataForType:type];
        const char* media = type.UTF8String;
        if (data != nil && data.length != 0 && media != nullptr &&
            [type lengthOfBytesUsingEncoding:NSUTF8StringEncoding] <=
                DragLimits::maximum_media_type_bytes &&
            data.length <= DragLimits::maximum_total_bytes - total_bytes) {
            const std::uint8_t* begin = static_cast<const std::uint8_t*>(data.bytes);
            result.emplace_back(DragBinaryData{
                media, std::vector<std::uint8_t>(begin, begin + data.length)});
        }
    }
    if (result.size() > DragLimits::maximum_items) {
        return {};
    }
    return result;
}

static std::string utf8_string(id value) {
    NSString* string = nil;
    if ([value isKindOfClass:[NSAttributedString class]]) {
        string = [(NSAttributedString*)value string];
    } else if ([value isKindOfClass:[NSString class]]) {
        string = (NSString*)value;
    }
    if (string == nil) {
        return {};
    }
    const char* bytes = [string UTF8String];
    return bytes == nullptr ? std::string{} : std::string(bytes);
}

// A byte in the module that holds GUI.Forms, whose address names that module to dladdr.
static const char gui_forms_module_anchor{};

// The directory of the image holding this code: the GUI.Forms dynamic library or framework
// binary when another process (a .NET runtime, a plug-in host) loaded it, else the executable
// it is linked into; empty when dladdr cannot say.
static std::filesystem::path gui_forms_module_directory() {
    Dl_info information{};
    const int found = dladdr(&gui_forms_module_anchor, &information);
    if (found == 0 || information.dli_fname == nullptr || information.dli_fname[0] == '\0') return {};
    std::error_code error;
    const std::filesystem::path module = std::filesystem::canonical(information.dli_fname, error);
    if (error) return {};
    return module.parent_path();
}

// The bundled font directory: the first of GUI_FORMS_FONT_DIR, the main bundle's
// Contents/Resources/fonts, "fonts" beside the GUI.Forms image and a framework's
// Resources/fonts next to it that holds the required faces; empty when none does, which
// locate_bundled_fonts counts and reports (bundled_font_diagnostics, bundled_font_refusal).
static std::filesystem::path bundle_font_directory() {
    std::array<gui_forms::detail::BundledFontCandidate, 4> candidates{};
    const char* override_path = std::getenv("GUI_FORMS_FONT_DIR");
    candidates[0].source = gui_forms::BundledFontSource::environment;
    if (override_path != nullptr) candidates[0].directory = override_path;
    NSString* resources = [[NSBundle mainBundle] resourcePath];
    candidates[1].source = gui_forms::BundledFontSource::application_bundle;
    if (resources != nil)
        candidates[1].directory = std::filesystem::path(resources.fileSystemRepresentation) / "fonts";
    const std::filesystem::path library_directory = gui_forms_module_directory();
    candidates[2].source = gui_forms::BundledFontSource::library_module;
    candidates[3].source = gui_forms::BundledFontSource::library_module;
    if (!library_directory.empty()) {
        candidates[2].directory = library_directory / "fonts";
        // A framework's binary sits in Versions/A beside its Resources folder.
        candidates[3].directory = library_directory / "Resources" / "fonts";
    }
    const gui_forms::detail::BundledFontLocation location =
        gui_forms::detail::locate_bundled_fonts(candidates);
    return location.directory;
}

// The process's one bundled font set, shared by every view and held by the application run:
// each face is read once, checked against the manifest and sealed read-only.
static std::shared_ptr<gui_forms::detail::BundledFontSet> bundle_font_set() {
    const std::filesystem::path directory = bundle_font_directory();
    if (directory.empty()) return {};
    const std::shared_ptr<gui_forms::detail::BundledFontSet> set =
        gui_forms::detail::shared_bundled_font_set(directory, gui_forms::host::posix_sealed_pages());
    return set;
}

namespace {

NSString* native_string(std::string_view text) {
    return [[NSString alloc] initWithBytes:text.data()
                                    length:text.size()
                                  encoding:NSUTF8StringEncoding];
}

NSString* native_accessibility_role(SemanticRole role) {
    switch (role) {
    case SemanticRole::group: return NSAccessibilityGroupRole;
    case SemanticRole::static_text: return NSAccessibilityStaticTextRole;
    case SemanticRole::button: return NSAccessibilityButtonRole;
    // A switch is a check box with AppKit's switch subrole.
    case SemanticRole::check_box:
    case SemanticRole::toggle_switch: return NSAccessibilityCheckBoxRole;
    case SemanticRole::radio_button: return NSAccessibilityRadioButtonRole;
    case SemanticRole::toolbar: return NSAccessibilityToolbarRole;
    case SemanticRole::radio_group: return NSAccessibilityRadioGroupRole;
    case SemanticRole::link: return NSAccessibilityLinkRole;
    // A search box is a text field with AppKit's search-field subrole.
    case SemanticRole::text_box:
    case SemanticRole::search_box:
    case SemanticRole::numeric_field: return NSAccessibilityTextFieldRole;
    case SemanticRole::list: return NSAccessibilityListRole;
    case SemanticRole::list_item: return NSAccessibilityRowRole;
    case SemanticRole::check_list_item: return NSAccessibilityCheckBoxRole;
    case SemanticRole::combo_box: return NSAccessibilityComboBoxRole;
    case SemanticRole::slider: return NSAccessibilitySliderRole;
    case SemanticRole::scroll_bar: return NSAccessibilityScrollBarRole;
    case SemanticRole::progress_bar: return NSAccessibilityProgressIndicatorRole;
    // AppKit has no chart role: a figure is an image with its summary as
    // the description.
    case SemanticRole::image:
    case SemanticRole::figure: return NSAccessibilityImageRole;
    case SemanticRole::tab_group: return NSAccessibilityTabGroupRole;
    case SemanticRole::tab: return NSAccessibilityRadioButtonRole;
    case SemanticRole::split_pane: return NSAccessibilitySplitGroupRole;
    case SemanticRole::tool_tip: return NSAccessibilityStaticTextRole;
    case SemanticRole::date_picker: return NSAccessibilityComboBoxRole;
    case SemanticRole::calendar: return NSAccessibilityGroupRole;
    case SemanticRole::date_cell: return NSAccessibilityButtonRole;
    case SemanticRole::property_grid: return NSAccessibilityGroupRole;
    case SemanticRole::property_group: return NSAccessibilityGroupRole;
    case SemanticRole::property_row: return NSAccessibilityStaticTextRole;
    case SemanticRole::menu_bar: return NSAccessibilityMenuBarRole;
    case SemanticRole::menu_bar_item: return NSAccessibilityMenuBarItemRole;
    case SemanticRole::menu: return NSAccessibilityMenuRole;
    case SemanticRole::menu_item: return NSAccessibilityMenuItemRole;
    // AppKit exposes no separator role. A visual menu separator is structural,
    // never an adjustable splitter, so retain it as a neutral group.
    case SemanticRole::separator: return NSAccessibilityGroupRole;
    // AppKit has no split-button role. The primary action is a press, so a
    // split button is a button; a menu button is AppKit's menu button.
    case SemanticRole::split_button: return NSAccessibilityButtonRole;
    case SemanticRole::menu_button: return NSAccessibilityMenuButtonRole;
    // AppKit has no pane or tab-panel role: both are groups, a tab panel
    // with the AXTabPanel subrole (accessibilitySubrole).
    case SemanticRole::pane: return NSAccessibilityGroupRole;
    case SemanticRole::tab_panel: return NSAccessibilityGroupRole;
    // The stage A vocabulary (12.13), as AppKit's outline and table views
    // and WebKit publish them: a tree and a tree grid are outlines, a tree
    // item and a table row are rows, a cell and a header are cells, and a
    // dialog, a status bar and a live region are groups told apart by their
    // subroles (accessibilitySubrole).
    case SemanticRole::tree: return NSAccessibilityOutlineRole;
    case SemanticRole::tree_item: return NSAccessibilityRowRole;
    case SemanticRole::table: return NSAccessibilityTableRole;
    case SemanticRole::row: return NSAccessibilityRowRole;
    case SemanticRole::cell: return NSAccessibilityCellRole;
    case SemanticRole::column_header: return NSAccessibilityCellRole;
    case SemanticRole::row_header: return NSAccessibilityCellRole;
    case SemanticRole::tree_grid: return NSAccessibilityOutlineRole;
    case SemanticRole::dialog: return NSAccessibilityGroupRole;
    case SemanticRole::status_bar: return NSAccessibilityGroupRole;
    case SemanticRole::live_region: return NSAccessibilityGroupRole;
    case SemanticRole::meter: return NSAccessibilityLevelIndicatorRole;
    case SemanticRole::generic: return NSAccessibilityGroupRole;
    case SemanticRole::count: return NSAccessibilityGroupRole;
    }
    return NSAccessibilityGroupRole;
}

bool semantic_has_action(const SemanticNode& node, SemanticAction action) {
    return std::find(node.actions.begin(), node.actions.end(), action) !=
           node.actions.end();
}

} // namespace

// What the published accessibility tree was built from. The semantic
// generation moves with every semantic invalidation (focus, text, enabled,
// visibility, bounds requests, attach and detach, focus scopes), but an
// arrangement that moves a control without such an invalidation of its own
// only advances the content revision, and the elements keep each node's
// bounds. The display generation moves when a control is painted again, in
// case a node describes what was painted (as the Windows tree's key does).
// The elements convert their window-local bounds to the screen when read,
// so window moves and scale changes need no entry here.
struct MacAccessibilityTreeKey final {
    std::uint64_t semantic_generation{0};
    std::uint64_t content_revision{0};
    std::uint64_t display_generation{0};
};

static bool operator==(const MacAccessibilityTreeKey& left,
                       const MacAccessibilityTreeKey& right) noexcept {
    const bool equal = left.semantic_generation == right.semantic_generation &&
                       left.content_revision == right.content_revision &&
                       left.display_generation == right.display_generation;
    return equal;
}

// The key `window` gives now; builds no snapshot. metrics_snapshot copies
// the metrics record, including its renderer-name string (at most one small
// allocation), since Window has no narrower reader of the display generation.
static MacAccessibilityTreeKey read_accessibility_tree_key(const Window& window) {
    MacAccessibilityTreeKey key;
    key.semantic_generation = window.semantic_generation();
    key.content_revision = window.paint_lease_snapshot().content_revision;
    key.display_generation = window.metrics_snapshot().display_generation;
    return key;
}

@class GUIFormsAccessibilityElement;
@class GUIFormsView;

// CADisplayLink retains its target. This proxy observes the view weakly, so a
// paused link cannot keep an otherwise closed window's view alive.
@interface GUIFormsDisplayTarget : NSObject {
    __weak GUIFormsView* _view;
}
- (instancetype)initWithView:(GUIFormsView*)view;
- (void)tick:(CADisplayLink*)sender;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerTick:(CADisplayLink*)sender;
@end

@interface GUIFormsView : NSView <NSTextInputClient, NSDraggingDestination> {
    std::unique_ptr<Window> _model;
    std::unique_ptr<HostSession> _hostSession;
    std::unique_ptr<HostServices> _hostServices;
    // The last compositor report's generation; 0 before the first (T5).
    gui_forms::CompositorGeneration _compositorGeneration;
    gui_forms::SubscriptionToken _closeRequestSubscription;
    std::uint64_t _nextHostSequence;
    BOOL _hostAttached;
    // The double-click interval last reported, in milliseconds; 0 (as the
    // runtime zeroes ivars) before the first report.
    std::uint32_t _reportedDoubleClickMilliseconds;
    // The caret settings last reported (audit N75).
    gui_forms::CaretSettings _reportedCaret;
    SkiaRaster _raster;
    BOOL _rasterHasContent;
    DamageRegion _pendingDamage;
    // Scroll copies taken from the model and not yet applied to the raster,
    // in order (SurfaceRoot::take_scroll_copies); scratch for the take; the
    // clips of copies the current draw applied, damaged again if its frame
    // is not presented.
    std::vector<gui_forms::ScrollCopy> _pendingCopies;
    std::vector<gui_forms::ScrollCopy> _takenCopies;
    std::vector<GFRect> _appliedCopyClips;
    NSMutableAttributedString* _markedText;
    NSRange _selectedRange;
    NSTrackingArea* _trackingArea;
    dispatch_source_t _wakeSource;
    // The sRGB colour space every present tags the raster with: immutable,
    // made once per view instead of once per draw, released in dealloc.
    CGColorSpaceRef _presentColorSpace;
    std::unique_ptr<MacDisplayLinkTrace> _displayTrace;
    CADisplayLink* _displayLink;
    BOOL _hostOccluded;
    // The full-screen state the session last accepted (notifyWindowState).
    BOOL _windowStateReported;
    BOOL _reportedFullScreen;
    std::vector<LiveSurfacePresentation> _pendingLivePresentations;
    // Objective-C++ constructs these vectors empty with the view. Reuse storage
    // across ticks; immutable leases survive producer reconfiguration until
    // this pass has finished consuming their pixels.
    std::vector<gui_forms::LiveSurfaceFrame> _liveFrames;
    std::vector<GFRect> _opaqueLiveClips;
    // Per pending presentation: the index of the frame it draws (fragments of
    // one surface share one acquisition) and the part of its clip to copy.
    std::vector<std::size_t> _liveFrameSources;
    std::vector<GFRect> _liveRegions;
    // Per acquired frame (indexed like _liveFrames): its generation, kept
    // after the frames are released so that a presented pass can report
    // what it showed (LiveSurface::notify_presented); 0 for no frame.
    std::vector<std::uint64_t> _liveFrameGenerations;
    std::vector<GFRect> _uncoveredDamage;
    // Per drawRect: on a reusable raster, the pending live presentations'
    // destinations and the parts of the uncovered damage inside them.
    std::vector<GFRect> _liveDestinations;
    std::vector<GFRect> _liveExposure;
    // A presentation region was taken but its pixels never reached a
    // presented raster. The next pass copies complete clips.
    BOOL _liveRegionLost;
    // The surface epoch of the receipt the raster's front presents; 0 before
    // the first presented frame.
    std::uint64_t _frontSurfaceEpoch;
    std::uint64_t _lastSemanticGeneration;
    // The key the cached accessibility children were built under; read only
    // while _semanticAccessibilityChildren is not nil.
    MacAccessibilityTreeKey _accessibilityCacheKey;
    // Whether the last published tree held a live region: a semantic change
    // then republishes the tree at once, so that a changed announcement is
    // posted when it changes rather than when a client next reads.
    BOOL _publishesLiveRegion;
    // Whether the last published tree held a text node with text ranges: a
    // semantic change then republishes the tree at once, so that its
    // element posts a changed value or selection when it changes.
    BOOL _publishesTextRanges;
    BOOL _initialHostOccluded;
    std::uint64_t _scheduledWakeCount;
    std::uint64_t _damageCollectionCount;
    std::uint64_t _nativeDrawCount;
    std::uint64_t _displayTickCount;
    MacPaintPhase _rasterPreparePhase;
    MacPaintPhase _retainedPaintPhase;
    MacPaintPhase _rasterFinishPhase;
    MacPaintPhase _cgSetupPhase;
    MacPaintPhase _cgDrawPhase;
    MacPaintPhase _cgReleasePhase;
    MacCpuAttribution _cpuAttribution;
    std::unique_ptr<MacCausalTrace> _causalTrace;
    double _lastNativeDirtyArea;
    double _lastFrameDamageBoundsArea;
    double _lastCGDestinationArea;
    double _lastCGClipBoundsArea;
    std::size_t _lastCGSourceBytes;
    NSArray* _semanticAccessibilityChildren;
    // The published elements by accessibility_element_key, and the first
    // element for each stable identity (the tab-to-panel relation).
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>*
        _semanticAccessibilityElements;
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>*
        _semanticAccessibilityElementsByStableId;
    BOOL _transparentFullSizeContent;
    std::vector<std::string> _windowDragRegionIds;
    // A Control-click is AppKit's context-menu request, not a press: its
    // drags and release are not dispatched either.
    BOOL _controlClickInProgress;
    // Portable frame capture (capture_path host option). Completed once,
    // after the first presented frame whose dirty rect covers the view.
    std::string _frameCapturePath;
    BOOL _frameCaptureCompleted;
    BOOL _frameCaptureSaved;
    // The invalidation overlay's present-time storage (gui_forms/diagnostics/
    // invalidation_overlay), reused: the scratch frame the outlines are
    // composited in (sized on first use), the device areas they cover now,
    // those they covered at the last fade tick, and the fade timer, nil
    // while nothing shows. The timer retains a weak proxy, not the view.
    std::vector<std::byte> _overlayScratch;
    std::vector<gui_forms::DevicePixelRect> _overlayRegions;
    std::vector<gui_forms::DevicePixelRect> _overlayDrawn;
    NSTimer* _overlayTimer;
}
- (instancetype)initWithModel:(std::unique_ptr<Window>)model
       transparentFullSizeContent:(BOOL)transparentFullSizeContent
              windowDragRegionIds:(std::vector<std::string>)windowDragRegionIds;
- (BOOL)initializeHost;
- (void)installCloseRequestHandler:(std::function<void(HostCloseRequest&)>)handler;
- (void)installFrameCapturePath:(std::string)path;
- (BOOL)frameCaptureSaved;
- (void)completeFrameCaptureIfDue:(NSRect)dirtyRect presented:(BOOL)presented;
- (HostDispatchResult)dispatchHostPayload:(HostEventPayload)payload
                         timestampNanoseconds:(std::uint64_t)timestamp;
- (BOOL)requestCloseHiding:(BOOL)hiding reason:(HostCloseReason)reason;
// Whether the closing handler refuses a close for `reason`, asked without
// closing (audit P2). A detached host, or a request nothing accepted,
// refuses nothing, as in the headless host's session end.
- (BOOL)closeRefusedForReason:(HostCloseReason)reason;
- (void)notifyClosed;
- (void)notifyClosedWithReason:(HostCloseReason)reason;
- (void)notifyActivation:(BOOL)active;
- (void)notifyOcclusion:(BOOL)occluded;
- (void)notifyScaleChanged;
- (void)notifyWindowState;
- (void)notifyDisplaysChanged;
- (void)screenParametersChanged:(NSNotification*)notification;
// Stage T5: the accessibility display options changed (reduce
// transparency); the compositor state is reported again.
- (void)accessibilityDisplayOptionsChanged:(NSNotification*)notification;
- (void)systemColoursChanged:(NSNotification*)notification;
- (void)reportCompositorState;
// The view the native window shows: this view, or the backdrop container
// holding it for a window declared non-opaque (make_macos_content_view).
// Called once, when the window is made.
- (NSView*)hostContentView;
- (void)collectDamage;
- (void)takeScrollCopies;
- (void)drainPostedWork;
- (void)armWakeTimer;
- (void)scheduledWake;
- (void)startDisplayLinkIfNeeded;
- (void)stopDisplayLink;
- (void)displayLinkTick;
- (void)armInvalidationOverlayTimer;
- (void)stopInvalidationOverlayTimer;
- (void)invalidationOverlayTick;
- (void)setDisplayTraceEnabled:(BOOL)enabled;
- (MacDisplayLinkTrace)displayTraceSnapshot;
- (void)answerDisplayLinkTick;
- (void)answerDrainPostedWork;
- (void)drawRetainedRect:(NSRect)dirtyRect;
- (void)prepareForShutdown;
- (BOOL)acceptsWindowRequest;
- (HostDialogResult)showHostDialog:(const HostDialogRequest&)request;
- (HostClipboardTextResult)readHostClipboard;
- (HostServiceStatus)writeHostClipboard:(std::string_view)text;
- (DragEvent)dragEventFor:(id<NSDraggingInfo>)sender action:(DragAction)action;
- (std::string)metricsJSON;
- (std::string)hostJSON;
- (BOOL)setCpuAttributionEnabled:(const BOOL)enabled;
- (std::array<std::uint64_t, 13>)cpuAttributionSnapshot;
- (MacCausalSnapshot)cpuCausalTraceSnapshot;
- (NSRect)accessibilityFrameForSemanticBounds:(GFRect)bounds;
// Performs `action` on a published node named as the shared routing API
// names it: the runtime identity of its control and, for a virtual child,
// the child's stable identity (empty for the control's own node). A virtual
// child with no exposed control above it (control 0) is named by its stable
// identity alone.
- (BOOL)performSemanticAction:(SemanticAction)action
                      control:(std::uint64_t)control
                 virtualChild:(const std::string&)virtualChild
                     stableId:(const std::string&)stableId
                        value:(NSString*)value;
- (id)semanticAccessibilityElementForStableId:(NSString*)stableId;
// macOS's own keyboard context request (owner's ruling, 2026-11-08): the
// accessibility show-menu action (NSAccessibilityShowMenuAction, VoiceOver
// VO+Shift+M) on an element with no menu of its own, reported to the
// window as a keyboard HostContextRequestEvent after the element's control
// took focus. Returns whether a control handled it.
- (BOOL)dispatchKeyboardContextRequest;
// The text queries of owner decision A-T1 for the control `control`
// (Window::semantic_text_layout and Window::set_semantic_text_selection).
// Each returns NO when the window has gone or the control refuses.
- (BOOL)readSemanticTextLayout:(std::uint64_t)control layout:(SemanticTextLayout&)layout;
- (BOOL)selectSemanticText:(std::uint64_t)control selection:(SemanticTextSelection)selection;
// The window's accessibility tree key now (read_accessibility_tree_key);
// NO when the window has gone.
- (BOOL)readAccessibilityTreeKey:(MacAccessibilityTreeKey&)key;
// A point in screen coordinates (AppKit's, origin at the bottom left) in
// the view's semantic coordinates.
- (NSPoint)semanticPointForScreenPoint:(NSPoint)point;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerAccessibilityDisplayOptionsChanged:(NSNotification*)notification;
- (void)answerSystemColoursChanged:(NSNotification*)notification;
- (void)answerViewDidChangeEffectiveAppearance;
- (NSRect)answerAccessibilityFrame;
- (void)answerScreenParametersChanged:(NSNotification*)notification;
- (NSString*)answerAccessibilityLabel;
- (NSArray*)answerAccessibilityChildren;
- (NSArray*)answerAccessibilityChildrenInNavigationOrder;
- (NSArray*)answerAccessibilityVisibleChildren;
- (NSRect)answerAccessibilityFrameForSemanticBounds:(GFRect)bounds;
- (void)answerViewDidMoveToWindow;
- (void)answerViewWillMoveToWindow:(NSWindow*)newWindow;
- (void)answerDealloc;
- (void)answerSetFrameSize:(NSSize)newSize;
- (void)answerUpdateTrackingAreas;
- (void)answerInvalidationOverlayTick;
- (NSDragOperation)answerDraggingEntered:(id<NSDraggingInfo>)sender;
- (NSDragOperation)answerDraggingUpdated:(id<NSDraggingInfo>)sender;
- (void)answerDraggingExited:(id<NSDraggingInfo>)sender;
- (BOOL)answerPrepareForDragOperation:(id<NSDraggingInfo>)sender;
- (BOOL)answerPerformDragOperation:(id<NSDraggingInfo>)sender;
- (void)answerCursorUpdate:(NSEvent*)event;
- (void)answerMouseExited:(NSEvent*)event;
- (void)answerMouseDown:(NSEvent*)event;
- (void)answerMouseUp:(NSEvent*)event;
- (void)answerRightMouseDown:(NSEvent*)event;
- (NSMenu*)answerMenuForEvent:(NSEvent*)event;
- (void)answerRightMouseUp:(NSEvent*)event;
- (void)answerOtherMouseDown:(NSEvent*)event;
- (void)answerOtherMouseUp:(NSEvent*)event;
- (void)answerMouseMoved:(NSEvent*)event;
- (void)answerMouseDragged:(NSEvent*)event;
- (void)answerRightMouseDragged:(NSEvent*)event;
- (void)answerOtherMouseDragged:(NSEvent*)event;
- (void)answerScrollWheel:(NSEvent*)event;
- (void)answerKeyDown:(NSEvent*)event;
- (void)answerKeyUp:(NSEvent*)event;
- (void)answerFlagsChanged:(NSEvent*)event;
- (void)answerInsertText:(id)string replacementRange:(NSRange)replacementRange;
- (void)answerSetMarkedText:(id)string selectedRange:(NSRange)selectedRange replacementRange:(NSRange)replacementRange;
- (void)answerUnmarkText;
- (BOOL)answerHasMarkedText;
- (NSRange)answerMarkedRange;
- (NSRange)answerSelectedRange;
- (nullable NSAttributedString*)answerAttributedSubstringForProposedRange:(NSRange)range
        actualRange:(nullable NSRangePointer)actualRange;
- (NSArray<NSAttributedStringKey>*)answerValidAttributesForMarkedText;
- (NSRect)answerFirstRectForCharacterRange:(NSRange)range actualRange:(nullable NSRangePointer)actualRange;
- (void)answerDoCommandBySelector:(SEL)selector;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsDisplayTarget's native entries: each sends the entry's answer message.
void run_display_target_tick(GUIFormsDisplayTarget* object, CADisplayLink* sender) {
    [object answerTick:sender];
}
} // namespace

@implementation GUIFormsDisplayTarget
- (instancetype)initWithView:(GUIFormsView*)view {
    self = [super init];
    if (self != nil) _view = view;
    return self;
}
- (void)tick:(CADisplayLink*)sender {
    gui_forms::detail::call_failing_fast("macos.display_target.tick:", &run_display_target_tick, self, sender);
}

- (void)answerTick:(CADisplayLink*)sender {
    GUIFormsView* view = _view;
    if (view == nil) {
        [sender invalidate];
        return;
    }
    [view displayLinkTick];
}
@end

// NSTimer retains its target: this proxy observes the view weakly, so the
// invalidation overlay's fade timer cannot keep a closed window's view
// alive, and a tick after the view is gone stops the timer.
@interface GUIFormsOverlayTimerTarget : NSObject {
    __weak GUIFormsView* _view;
}
- (instancetype)initWithView:(GUIFormsView*)view;
- (void)tick:(NSTimer*)sender;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerTick:(NSTimer*)sender;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsOverlayTimerTarget's native entries: each sends the entry's answer message.
void run_overlay_timer_tick(GUIFormsOverlayTimerTarget* object, NSTimer* sender) {
    [object answerTick:sender];
}
} // namespace

@implementation GUIFormsOverlayTimerTarget
- (instancetype)initWithView:(GUIFormsView*)view {
    self = [super init];
    if (self != nil) _view = view;
    return self;
}
- (void)tick:(NSTimer*)sender {
    gui_forms::detail::call_failing_fast("macos.overlay_timer.tick:", &run_overlay_timer_tick, self, sender);
}

- (void)answerTick:(NSTimer*)sender {
    GUIFormsView* view = _view;
    if (view == nil) {
        [sender invalidate];
        return;
    }
    [view invalidationOverlayTick];
}
@end

// One published semantic node. Its identity is the runtime identity of the
// control that publishes it and, for a virtual child, the child's stable
// identity (accessibility_element_key). Runtime identities are never reused,
// so an element a client keeps after its control is disposed or replaced
// names a control the window no longer has, and its actions are refused
// (interview GF017).
@interface GUIFormsAccessibilityElement : NSAccessibilityElement {
    __weak GUIFormsView* _owner;
    SemanticNode _node;
    // The runtime identity of the control that publishes this node: the
    // node's own, or, for a virtual child, the nearest control node's above
    // it (0 when there is none).
    std::uint64_t _control;
    // The virtual child's stable identity; empty for a control's own node.
    std::string _virtualChild;
    NSArray* _semanticChildren;
    // A text node's layout (readSemanticText), kept while the window's
    // accessibility tree key reads as it did after the layout was read
    // (N101): the key moves with the semantic generation, the content
    // revision and the display generation, so the value and the layout
    // pass are then unchanged. Every text attribute reads it, so VoiceOver
    // reading a character at a time no longer lays the text out for each
    // attribute.
    SemanticTextLayout _textLayout;
    MacAccessibilityTreeKey _textLayoutKey;
    BOOL _textLayoutCurrent;
}
- (instancetype)initWithNode:(const SemanticNode&)node
                       owner:(GUIFormsView*)owner
                      parent:(id)parent
                     control:(std::uint64_t)control
                virtualChild:(const std::string&)virtualChild;
- (void)updateWithNode:(const SemanticNode&)node
                 owner:(GUIFormsView*)owner
                parent:(id)parent;
- (void)setSemanticChildren:(NSArray*)children;
- (BOOL)performNodeAction:(SemanticAction)action value:(NSString*)value;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (NSString*)answerAccessibilityRole;
- (NSString*)answerAccessibilityLabel;
- (NSString*)answerAccessibilityHelp;
- (NSString*)answerAccessibilityIdentifier;
- (NSArray*)answerAccessibilityChildren;
- (NSRect)answerAccessibilityFrame;
- (BOOL)answerIsAccessibilityEnabled;
- (BOOL)answerIsAccessibilityFocused;
- (BOOL)answerIsAccessibilitySelected;
- (BOOL)answerIsAccessibilityExpanded;
- (NSInteger)answerAccessibilityIndex;
- (BOOL)answerIsAccessibilityProtectedContent;
- (NSString*)answerAccessibilitySubrole;
- (NSInteger)answerAccessibilityDisclosureLevel;
- (NSRange)answerAccessibilityRowIndexRange;
- (NSRange)answerAccessibilityColumnIndexRange;
- (NSInteger)answerAccessibilityRowCount;
- (NSInteger)answerAccessibilityColumnCount;
- (BOOL)answerIsAccessibilityModal;
- (NSAccessibilityOrientation)answerAccessibilityOrientation;
- (NSArray*)answerAccessibilityLinkedUIElements;
- (NSArray<NSAccessibilityCustomAction*>*)answerAccessibilityCustomActions;
- (id)answerAccessibilityValue;
- (id)answerAccessibilityMinValue;
- (id)answerAccessibilityMaxValue;
- (BOOL)answerAccessibilityPerformPress;
- (void)answerSetAccessibilitySelected:(BOOL)selected;
- (BOOL)answerAccessibilityPerformShowMenu;
- (BOOL)answerAccessibilityPerformIncrement;
- (BOOL)answerAccessibilityPerformDecrement;
- (void)answerSetAccessibilityFocused:(BOOL)focused;
- (void)answerSetAccessibilityValue:(id)value;
- (NSInteger)answerAccessibilityNumberOfCharacters;
- (NSRange)answerAccessibilitySelectedTextRange;
- (void)answerSetAccessibilitySelectedTextRange:(NSRange)range;
- (NSString*)answerAccessibilitySelectedText;
- (NSRange)answerAccessibilityVisibleCharacterRange;
- (NSInteger)answerAccessibilityInsertionPointLineNumber;
- (NSString*)answerAccessibilityStringForRange:(NSRange)range;
- (NSAttributedString*)answerAccessibilityAttributedStringForRange:(NSRange)range;
- (NSInteger)answerAccessibilityLineForIndex:(NSInteger)index;
- (NSRange)answerAccessibilityRangeForLine:(NSInteger)line;
- (NSRect)answerAccessibilityFrameForRange:(NSRange)range;
- (NSRange)answerAccessibilityRangeForPosition:(NSPoint)point;
- (NSRange)answerAccessibilityRangeForIndex:(NSInteger)index;
- (NSRange)answerAccessibilityStyleRangeForIndex:(NSInteger)index;
- (BOOL)answerIsAccessibilitySelectorAllowed:(SEL)selector;
- (BOOL)answerAccessibilityIsAttributeSettable:(NSAccessibilityAttributeName)attribute;
@end

// Asks VoiceOver to speak a live region's latest announcement (DK-51) at
// medium priority, the polite setting the other hosts publish. Posted to the
// view's window, or to the application while the view has none.
static void announce_live_region(GUIFormsView* owner, const std::string& text) {
    NSString* const announcement = native_string(text);
    NSDictionary* const info = @{
        NSAccessibilityAnnouncementKey : announcement,
        NSAccessibilityPriorityKey : @(NSAccessibilityPriorityMedium),
    };
    NSWindow* const window = owner == nil ? nil : [owner window];
    const id target = window != nil ? static_cast<id>(window) : static_cast<id>(NSApp);
    NSAccessibilityPostNotificationWithUserInfo(target, NSAccessibilityAnnouncementRequestedNotification, info);
}

// Whether `node` or a node below it is a live region.
// -- editable text (stage A, A-T1) -------------------------------------------
//
// A control's own text box that publishes a text selection (TextBox,
// MaskedTextBox, TokenBox's editor; a password box publishes none, A-T2)
// publishes AppKit's text attributes and parameterized attributes. AppKit
// counts text in UTF-16 code units (NSRange) and the core in UTF-8 bytes:
// every range crosses here, and a range that falls inside a UTF-16
// surrogate pair widens to the whole scalar. Lines are visual lines, each
// running to the next line's start (so it holds its break); a composed
// character is a grapheme; the text has one style run.

// Whether `node` publishes text ranges.
static bool publishes_text_ranges(const SemanticNode& node) noexcept {
    const bool text = gui_forms::semantic_role_fallback(node.role) == SemanticRole::text_box &&
                      node.text_selection.has_value() &&
                      node.runtime_id != 0U && !gui_forms::is_virtual_semantic_runtime_id(node.runtime_id) &&
                      !gui_forms::has_semantic_state(node.states, SemanticState::protected_content);
    return text;
}

// Whether `node` or a node below it publishes text ranges.
// Recurses once per semantic level. Control levels are bounded by
// maximum_control_tree_depth (control.hpp); the virtual item levels a
// control publishes below itself are not yet (an open target of the
// depth rule, owner 2026-11-11).
static bool contains_text_ranges(const SemanticNode& node) {
    if (publishes_text_ranges(node)) return true;
    for (const SemanticNode& child : node.children) {
        if (contains_text_ranges(child)) return true;
    }
    return false;
}

// The bytes of the UTF-8 sequence `lead` starts; 1 for a byte that starts
// none.
static std::size_t utf8_sequence_bytes(unsigned char lead) noexcept {
    if (lead >= 0xF0U) return 4U;
    if (lead >= 0xE0U) return 3U;
    if (lead >= 0xC0U) return 2U;
    return 1U;
}

// The UTF-16 code units of value[0, bytes).
static std::size_t utf16_units_before(std::string_view value, std::size_t bytes) noexcept {
    const std::size_t end = std::min(bytes, value.size());
    std::size_t units = 0U;
    std::size_t index = 0U;
    while (index < end) {
        const std::size_t length = utf8_sequence_bytes(static_cast<unsigned char>(value[index]));
        units += length == 4U ? 2U : 1U;
        index += length;
    }
    return units;
}

// How a UTF-16 position inside a surrogate pair maps to a byte offset.
enum class SurrogateRounding : std::uint8_t {
    down,
    up,
};

// The byte offset in `value` of UTF-16 unit `units`: the end of the value
// past its last unit, and inside a surrogate pair the start (down) or the
// end (up) of its scalar.
static std::size_t utf8_offset_at_units(std::string_view value, std::size_t units,
                                        SurrogateRounding rounding) noexcept {
    std::size_t counted = 0U;
    std::size_t index = 0U;
    while (index < value.size() && counted < units) {
        const std::size_t length = utf8_sequence_bytes(static_cast<unsigned char>(value[index]));
        const std::size_t width = length == 4U ? 2U : 1U;
        if (counted + width > units && rounding == SurrogateRounding::down) break;
        counted += width;
        index += length;
    }
    const std::size_t offset = std::min(index, value.size());
    return offset;
}

// [start, end) of `value`, in bytes, as an NSRange in UTF-16 code units.
static NSRange utf16_range(std::string_view value, std::size_t start, std::size_t end) noexcept {
    const std::size_t first = utf16_units_before(value, start);
    const std::size_t last = utf16_units_before(value, std::max(start, end));
    const NSRange range = NSMakeRange(static_cast<NSUInteger>(first), static_cast<NSUInteger>(last - first));
    return range;
}

// `range`, in UTF-16 code units, as bytes [start, end) of `value`. Returns
// false, leaving both unchanged, when the range starts past the text's end
// or its end overflows; a range that runs past the end stops there.
static bool utf8_range(std::string_view value, NSRange range, std::size_t& start, std::size_t& end) noexcept {
    const std::size_t units = utf16_units_before(value, value.size());
    if (range.location > units || range.length > std::numeric_limits<NSUInteger>::max() - range.location) {
        return false;
    }
    const std::size_t last = std::min(static_cast<std::size_t>(range.location + range.length), units);
    start = utf8_offset_at_units(value, range.location, SurrogateRounding::down);
    end = utf8_offset_at_units(value, last, SurrogateRounding::up);
    return true;
}

// Orders an offset before a line that starts after it (std::upper_bound
// over the lines, which run in text order).
struct OffsetBeforeLine final {
    bool operator()(std::size_t offset, const SemanticTextLine& line) const noexcept {
        return offset < line.range.start.value();
    }
};

// The visual line that holds byte `offset`: the last line starting at or
// before it, so a line break belongs to the line it ends. A binary search
// (N101): the lines start in ascending order.
static std::size_t line_of_offset(const SemanticTextLayout& layout, std::size_t offset) noexcept {
    const std::vector<SemanticTextLine>::const_iterator after =
        std::upper_bound(layout.lines.begin(), layout.lines.end(), offset, OffsetBeforeLine{});
    const std::size_t past = static_cast<std::size_t>(after - layout.lines.begin());
    const std::size_t found = past == 0U ? 0U : past - 1U;
    return found;
}

// The box of the caret at byte `offset`, one point wide: at the left edge
// of the grapheme starting there, else at the right edge of the grapheme
// ending there, else at the start of the line that holds it. None for a
// layout without lines.
static std::optional<GFRect> caret_box(const SemanticTextLayout& layout, std::size_t offset) noexcept {
    if (layout.lines.empty()) return std::nullopt;
    std::size_t line = line_of_offset(layout, offset);
    double x = layout.lines[line].bounds.x;
    bool placed = false;
    for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
        if (grapheme.range.start.value() == offset) {
            line = grapheme.line.value();
            x = grapheme.bounds.x;
            placed = true;
            break;
        }
    }
    if (!placed) {
        for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
            if (grapheme.range.end.value() != offset) continue;
            line = grapheme.line.value();
            x = grapheme.bounds.x + grapheme.bounds.width;
        }
    }
    if (line >= layout.lines.size()) return std::nullopt;
    const GFRect line_bounds = layout.lines[line].bounds;
    const GFRect box{x, line_bounds.y, 1.0, line_bounds.height};
    return box;
}

// The union of the boxes of the graphemes within bytes [start, end), or the
// caret's box for an empty range; none when no grapheme lies within.
static std::optional<GFRect> range_box(const SemanticTextLayout& layout, std::size_t start,
                                       std::size_t end) noexcept {
    if (start == end) return caret_box(layout, start);
    std::optional<GFRect> united;
    for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
        if (grapheme.range.start.value() < start || grapheme.range.end.value() > end) continue;
        united = united ? GFRect::united(*united, grapheme.bounds) : grapheme.bounds;
    }
    return united;
}

// The grapheme at the point (x, y), in the view's semantic coordinates: on
// the line whose band holds y, or the nearest line, the grapheme whose
// horizontal extent holds x, or the nearest one. Null when the text has
// none. Borrows `layout`.
static const SemanticTextGrapheme* grapheme_at_point(const SemanticTextLayout& layout, double x,
                                                     double y) noexcept {
    std::size_t line = 0U;
    double nearest_line = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < layout.lines.size(); ++index) {
        const GFRect band = layout.lines[index].bounds;
        const double distance = std::max(0.0, std::max(band.y - y, y - (band.y + band.height)));
        if (distance < nearest_line) {
            nearest_line = distance;
            line = index;
        }
    }
    const SemanticTextGrapheme* found = nullptr;
    double nearest = std::numeric_limits<double>::infinity();
    for (const SemanticTextGrapheme& grapheme : layout.graphemes) {
        if (grapheme.line.value() != line) continue;
        const double left = grapheme.bounds.x;
        const double right = grapheme.bounds.x + grapheme.bounds.width;
        const double distance = std::max(0.0, std::max(left - x, x - right));
        if (distance < nearest) {
            nearest = distance;
            found = &grapheme;
        }
    }
    return found;
}
static const SemanticTextGrapheme* grapheme_at_point(SemanticTextLayout&&, double, double) = delete;

// Whether `selector` is one of the text attributes above.
static bool text_range_selector(SEL selector) noexcept {
    const bool text = selector == @selector(accessibilityNumberOfCharacters) ||
                      selector == @selector(accessibilitySelectedTextRange) ||
                      selector == @selector(setAccessibilitySelectedTextRange:) ||
                      selector == @selector(accessibilitySelectedText) ||
                      selector == @selector(accessibilityVisibleCharacterRange) ||
                      selector == @selector(accessibilityInsertionPointLineNumber) ||
                      selector == @selector(accessibilityStringForRange:) ||
                      selector == @selector(accessibilityAttributedStringForRange:) ||
                      selector == @selector(accessibilityLineForIndex:) ||
                      selector == @selector(accessibilityRangeForLine:) ||
                      selector == @selector(accessibilityFrameForRange:) ||
                      selector == @selector(accessibilityRangeForPosition:) ||
                      selector == @selector(accessibilityRangeForIndex:) ||
                      selector == @selector(accessibilityStyleRangeForIndex:);
    return text;
}

// Recurses once per semantic level. Control levels are bounded by
// maximum_control_tree_depth (control.hpp); the virtual item levels a
// control publishes below itself are not yet (an open target of the
// depth rule, owner 2026-11-11).
static bool contains_live_region(const SemanticNode& node) {
    if (node.role == SemanticRole::live_region) return true;
    for (const SemanticNode& child : node.children) {
        if (contains_live_region(child)) return true;
    }
    return false;
}

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsAccessibilityElement's native entries: each sends the entry's answer message.
NSString* run_accessibility_element_accessibility_role(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityRole];
}
NSString* run_accessibility_element_accessibility_label(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityLabel];
}
NSString* run_accessibility_element_accessibility_help(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityHelp];
}
NSString* run_accessibility_element_accessibility_identifier(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityIdentifier];
}
NSArray* run_accessibility_element_accessibility_children(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityChildren];
}
NSRect run_accessibility_element_accessibility_frame(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityFrame];
}
BOOL run_accessibility_element_is_accessibility_enabled(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityEnabled];
}
BOOL run_accessibility_element_is_accessibility_focused(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityFocused];
}
BOOL run_accessibility_element_is_accessibility_selected(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilitySelected];
}
BOOL run_accessibility_element_is_accessibility_expanded(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityExpanded];
}
NSInteger run_accessibility_element_accessibility_index(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityIndex];
}
BOOL run_accessibility_element_is_accessibility_protected_content(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityProtectedContent];
}
NSString* run_accessibility_element_accessibility_subrole(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilitySubrole];
}
NSInteger run_accessibility_element_accessibility_disclosure_level(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityDisclosureLevel];
}
NSRange run_accessibility_element_accessibility_row_index_range(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityRowIndexRange];
}
NSRange run_accessibility_element_accessibility_column_index_range(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityColumnIndexRange];
}
NSInteger run_accessibility_element_accessibility_row_count(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityRowCount];
}
NSInteger run_accessibility_element_accessibility_column_count(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityColumnCount];
}
BOOL run_accessibility_element_is_accessibility_modal(GUIFormsAccessibilityElement* object) {
    return [object answerIsAccessibilityModal];
}
NSAccessibilityOrientation run_accessibility_element_accessibility_orientation(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityOrientation];
}
NSArray* run_accessibility_element_accessibility_linked_u_i_elements(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityLinkedUIElements];
}
NSArray<NSAccessibilityCustomAction*>* run_accessibility_element_accessibility_custom_actions(
    GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityCustomActions];
}
id run_accessibility_element_accessibility_value(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityValue];
}
id run_accessibility_element_accessibility_min_value(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityMinValue];
}
id run_accessibility_element_accessibility_max_value(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityMaxValue];
}
BOOL run_accessibility_element_accessibility_perform_press(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityPerformPress];
}
void run_accessibility_element_set_accessibility_selected(GUIFormsAccessibilityElement* object, BOOL selected) {
    [object answerSetAccessibilitySelected:selected];
}
BOOL run_accessibility_element_accessibility_perform_show_menu(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityPerformShowMenu];
}
BOOL run_accessibility_element_accessibility_perform_increment(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityPerformIncrement];
}
BOOL run_accessibility_element_accessibility_perform_decrement(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityPerformDecrement];
}
void run_accessibility_element_set_accessibility_focused(GUIFormsAccessibilityElement* object, BOOL focused) {
    [object answerSetAccessibilityFocused:focused];
}
void run_accessibility_element_set_accessibility_value(GUIFormsAccessibilityElement* object, id value) {
    [object answerSetAccessibilityValue:value];
}
NSInteger run_accessibility_element_accessibility_number_of_characters(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityNumberOfCharacters];
}
NSRange run_accessibility_element_accessibility_selected_text_range(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilitySelectedTextRange];
}
void run_accessibility_element_set_accessibility_selected_text_range(
    GUIFormsAccessibilityElement* object, NSRange range) {
    [object answerSetAccessibilitySelectedTextRange:range];
}
NSString* run_accessibility_element_accessibility_selected_text(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilitySelectedText];
}
NSRange run_accessibility_element_accessibility_visible_character_range(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityVisibleCharacterRange];
}
NSInteger run_accessibility_element_accessibility_insertion_point_line_number(GUIFormsAccessibilityElement* object) {
    return [object answerAccessibilityInsertionPointLineNumber];
}
NSString* run_accessibility_element_accessibility_string_for_range(
    GUIFormsAccessibilityElement* object, NSRange range) {
    return [object answerAccessibilityStringForRange:range];
}
NSAttributedString* run_accessibility_element_accessibility_attributed_string_for_range(
    GUIFormsAccessibilityElement* object, NSRange range) {
    return [object answerAccessibilityAttributedStringForRange:range];
}
NSInteger run_accessibility_element_accessibility_line_for_index(
    GUIFormsAccessibilityElement* object, NSInteger index) {
    return [object answerAccessibilityLineForIndex:index];
}
NSRange run_accessibility_element_accessibility_range_for_line(GUIFormsAccessibilityElement* object, NSInteger line) {
    return [object answerAccessibilityRangeForLine:line];
}
NSRect run_accessibility_element_accessibility_frame_for_range(GUIFormsAccessibilityElement* object, NSRange range) {
    return [object answerAccessibilityFrameForRange:range];
}
NSRange run_accessibility_element_accessibility_range_for_position(
    GUIFormsAccessibilityElement* object, NSPoint point) {
    return [object answerAccessibilityRangeForPosition:point];
}
NSRange run_accessibility_element_accessibility_range_for_index(GUIFormsAccessibilityElement* object, NSInteger index) {
    return [object answerAccessibilityRangeForIndex:index];
}
NSRange run_accessibility_element_accessibility_style_range_for_index(
    GUIFormsAccessibilityElement* object, NSInteger index) {
    return [object answerAccessibilityStyleRangeForIndex:index];
}
BOOL run_accessibility_element_is_accessibility_selector_allowed(GUIFormsAccessibilityElement* object, SEL selector) {
    return [object answerIsAccessibilitySelectorAllowed:selector];
}
BOOL run_accessibility_element_accessibility_is_attribute_settable(
    GUIFormsAccessibilityElement* object, NSAccessibilityAttributeName attribute) {
    return [object answerAccessibilityIsAttributeSettable:attribute];
}
} // namespace

@implementation GUIFormsAccessibilityElement

// The children are set by reconcile_accessibility_element, which keys them.
- (instancetype)initWithNode:(const SemanticNode&)node
                       owner:(GUIFormsView*)owner
                      parent:(id)parent
                     control:(std::uint64_t)control
                virtualChild:(const std::string&)virtualChild {
    self = [super init];
    if (self != nil) {
        _owner = owner;
        _node = node;
        _control = control;
        _virtualChild = virtualChild;
        self.accessibilityParent = parent;
        _semanticChildren = @[];
    }
    return self;
}

// A live region whose name changed to a new, non-empty announcement posts
// it (semantic_announcement_due, the rule every host shares; a new element
// announces only when it announces on appearance, reconcile_accessibility_element).
// A text node whose value or selection changed posts it, as AppKit's text
// views do.
- (void)updateWithNode:(const SemanticNode&)node
                 owner:(GUIFormsView*)owner
                parent:(id)parent {
    const bool announce = gui_forms::semantic_announcement_due(&_node, node);
    const bool text = publishes_text_ranges(node) && publishes_text_ranges(_node);
    const bool value_changed = text && node.value != _node.value;
    const bool selection_changed = text && node.text_selection != _node.text_selection;
    _owner = owner;
    _node = node;
    self.accessibilityParent = parent;
    if (announce) announce_live_region(owner, node.name);
    if (value_changed) NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
    if (selection_changed) NSAccessibilityPostNotification(self, NSAccessibilitySelectedTextChangedNotification);
}

- (void)setSemanticChildren:(NSArray*)children {
    _semanticChildren = [children copy];
}

- (BOOL)performNodeAction:(SemanticAction)action value:(NSString*)value {
    if (_owner == nil) return NO;
    return [_owner performSemanticAction:action
                                 control:_control
                            virtualChild:_virtualChild
                                stableId:_node.stable_id
                                   value:value];
}

- (BOOL)isAccessibilityElement { return YES; }
// A text box whose text runs over several lines is AppKit's text area.
- (NSString*)accessibilityRole {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRole", &run_accessibility_element_accessibility_role, self);
}

- (NSString*)answerAccessibilityRole {
    if (_node.role == SemanticRole::text_box && _node.multiline) return NSAccessibilityTextAreaRole;
    return native_accessibility_role(_node.role);
}
- (NSString*)accessibilityLabel {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityLabel", &run_accessibility_element_accessibility_label, self);
}

- (NSString*)answerAccessibilityLabel { return native_string(_node.name); }
- (NSString*)accessibilityHelp {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityHelp", &run_accessibility_element_accessibility_help, self);
}

- (NSString*)answerAccessibilityHelp { return native_string(_node.description); }
- (NSString*)accessibilityIdentifier {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityIdentifier",
        &run_accessibility_element_accessibility_identifier, self);
}

- (NSString*)answerAccessibilityIdentifier { return native_string(_node.stable_id); }
- (NSArray*)accessibilityChildren {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityChildren", &run_accessibility_element_accessibility_children, self);
}

- (NSArray*)answerAccessibilityChildren { return _semanticChildren; }
- (NSRect)accessibilityFrame {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityFrame", &run_accessibility_element_accessibility_frame, self);
}

- (NSRect)answerAccessibilityFrame {
    return _owner == nil ? NSZeroRect
                         : [_owner accessibilityFrameForSemanticBounds:_node.bounds];
}
- (BOOL)isAccessibilityEnabled {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityEnabled",
        &run_accessibility_element_is_accessibility_enabled, self);
}

- (BOOL)answerIsAccessibilityEnabled {
    return gui_forms::has_semantic_state(_node.states, SemanticState::enabled);
}
- (BOOL)isAccessibilityFocused {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityFocused",
        &run_accessibility_element_is_accessibility_focused, self);
}

- (BOOL)answerIsAccessibilityFocused {
    return gui_forms::has_semantic_state(_node.states, SemanticState::focused);
}
- (BOOL)isAccessibilitySelected {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilitySelected",
        &run_accessibility_element_is_accessibility_selected, self);
}

- (BOOL)answerIsAccessibilitySelected {
    return gui_forms::has_semantic_state(_node.states, SemanticState::selected);
}
- (BOOL)isAccessibilityExpanded {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityExpanded",
        &run_accessibility_element_is_accessibility_expanded, self);
}

- (BOOL)answerIsAccessibilityExpanded {
    return gui_forms::has_semantic_state(_node.states, SemanticState::expanded);
}
// AppKit's index attribute carries the zero-based position in a set; it has
// no set-size or has-popup attribute (a menu button's role says it opens a
// menu).
- (NSInteger)accessibilityIndex {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityIndex", &run_accessibility_element_accessibility_index, self);
}

- (NSInteger)answerAccessibilityIndex {
    if (!_node.position_in_set || *_node.position_in_set == 0) return 0;
    return static_cast<NSInteger>(*_node.position_in_set - 1);
}
- (BOOL)isAccessibilityProtectedContent {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityProtectedContent",
        &run_accessibility_element_is_accessibility_protected_content, self);
}

- (BOOL)answerIsAccessibilityProtectedContent {
    return gui_forms::has_semantic_state(
        _node.states, SemanticState::protected_content);
}
// AppKit declares no constant for the tab-panel subrole; WebKit publishes
// ARIA tab panels with this one. A tab is a radio button with AppKit's
// tab-button subrole, as NSTabView's tabs are.
// A tree item is an outline row; a dialog, a status bar and a live region
// carry WebKit's subroles for them. A switch and a search box carry
// AppKit's own.
- (NSString*)accessibilitySubrole {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilitySubrole", &run_accessibility_element_accessibility_subrole, self);
}

- (NSString*)answerAccessibilitySubrole {
    if (_node.role == SemanticRole::toggle_switch) return NSAccessibilitySwitchSubrole;
    if (_node.role == SemanticRole::search_box) return NSAccessibilitySearchFieldSubrole;
    if (_node.role == SemanticRole::tab_panel) return @"AXTabPanel";
    if (_node.role == SemanticRole::tab) return NSAccessibilityTabButtonSubrole;
    if (_node.role == SemanticRole::tree_item) return NSAccessibilityOutlineRowSubrole;
    if (_node.role == SemanticRole::dialog) return @"AXApplicationDialog";
    if (_node.role == SemanticRole::status_bar) return @"AXApplicationStatus";
    if (_node.role == SemanticRole::live_region) return @"AXApplicationLog";
    return [super accessibilitySubrole];
}
// A tree item's depth, counting from zero as AppKit does.
- (NSInteger)accessibilityDisclosureLevel {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityDisclosureLevel",
        &run_accessibility_element_accessibility_disclosure_level, self);
}

- (NSInteger)answerAccessibilityDisclosureLevel {
    if (!_node.level || *_node.level == 0) return 0;
    return static_cast<NSInteger>(*_node.level - 1);
}
// A row's, cell's or header's place in its table, counting from zero, and a
// table's size. A node without an index does not publish the attribute
// (isAccessibilitySelectorAllowed:): AppKit would otherwise send the
// no-range value, {NSNotFound, 0}, which reaches a client as a location of
// LONG_MAX rather than kCFNotFound.
- (NSRange)accessibilityRowIndexRange {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRowIndexRange",
        &run_accessibility_element_accessibility_row_index_range, self);
}

- (NSRange)answerAccessibilityRowIndexRange {
    if (!_node.row_index || *_node.row_index == 0) return NSMakeRange(NSNotFound, 0);
    return NSMakeRange(static_cast<NSUInteger>(*_node.row_index - 1), 1);
}
- (NSRange)accessibilityColumnIndexRange {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityColumnIndexRange",
        &run_accessibility_element_accessibility_column_index_range, self);
}

- (NSRange)answerAccessibilityColumnIndexRange {
    if (!_node.column_index || *_node.column_index == 0) return NSMakeRange(NSNotFound, 0);
    return NSMakeRange(static_cast<NSUInteger>(*_node.column_index - 1), 1);
}
- (NSInteger)accessibilityRowCount {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRowCount", &run_accessibility_element_accessibility_row_count, self);
}

- (NSInteger)answerAccessibilityRowCount {
    return _node.row_count ? static_cast<NSInteger>(*_node.row_count) : 0;
}
- (NSInteger)accessibilityColumnCount {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityColumnCount",
        &run_accessibility_element_accessibility_column_count, self);
}

- (NSInteger)answerAccessibilityColumnCount {
    return _node.column_count ? static_cast<NSInteger>(*_node.column_count) : 0;
}
// A dialog that blocks the rest of its window.
- (BOOL)isAccessibilityModal {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilityModal", &run_accessibility_element_is_accessibility_modal, self);
}

- (BOOL)answerIsAccessibilityModal {
    return _node.modal ? YES : NO;
}
// A tab list's orientation.
- (NSAccessibilityOrientation)accessibilityOrientation {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityOrientation",
        &run_accessibility_element_accessibility_orientation, self);
}

- (NSAccessibilityOrientation)answerAccessibilityOrientation {
    if (!_node.orientation) return NSAccessibilityOrientationUnknown;
    if (*_node.orientation == SemanticOrientation::horizontal) return NSAccessibilityOrientationHorizontal;
    return NSAccessibilityOrientationVertical;
}
// The tab-to-panel relation as AppKit's linked elements: the panel's element
// when the window publishes the panel.
- (NSArray*)accessibilityLinkedUIElements {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityLinkedUIElements",
        &run_accessibility_element_accessibility_linked_u_i_elements, self);
}

- (NSArray*)answerAccessibilityLinkedUIElements {
    if (_owner == nil || _node.controlled_panel.empty()) return nil;
    id panel = [_owner semanticAccessibilityElementForStableId:native_string(_node.controlled_panel)];
    if (panel == nil) return nil;
    return @[ panel ];
}
// The close action has no AppKit action name; it is a custom action named
// "Close".
- (BOOL)performSemanticClose {
    if (_owner == nil || !semantic_has_action(_node, SemanticAction::close)) return NO;
    return [self performNodeAction:SemanticAction::close value:nil];
}
- (NSArray<NSAccessibilityCustomAction*>*)accessibilityCustomActions {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityCustomActions",
        &run_accessibility_element_accessibility_custom_actions, self);
}

- (NSArray<NSAccessibilityCustomAction*>*)answerAccessibilityCustomActions {
    if (!semantic_has_action(_node, SemanticAction::close)) return nil;
    NSAccessibilityCustomAction* close =
        [[NSAccessibilityCustomAction alloc] initWithName:@"Close"
                                                   target:self
                                                 selector:@selector(performSemanticClose)];
    return @[ close ];
}
- (id)accessibilityValue {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityValue", &run_accessibility_element_accessibility_value, self);
}

- (id)answerAccessibilityValue {
    const SemanticRole acting = gui_forms::semantic_role_fallback(_node.role);
    if (acting == SemanticRole::check_box ||
        acting == SemanticRole::check_list_item ||
        acting == SemanticRole::radio_button) {
        if (gui_forms::has_semantic_state(_node.states, SemanticState::mixed)) {
            return @2;
        }
        return @(gui_forms::has_semantic_state(_node.states, SemanticState::checked));
    }
    // A tab publishes AppKit's radio-button value: 1 when it is the
    // selected tab.
    if (_node.role == SemanticRole::tab) {
        return @(gui_forms::has_semantic_state(_node.states, SemanticState::selected));
    }
    if (_node.numeric_value) return @(*_node.numeric_value);
    return native_string(_node.value);
}
- (id)accessibilityMinValue {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityMinValue", &run_accessibility_element_accessibility_min_value, self);
}

- (id)answerAccessibilityMinValue {
    return _node.minimum_value ? @(*_node.minimum_value) : nil;
}
- (id)accessibilityMaxValue {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityMaxValue", &run_accessibility_element_accessibility_max_value, self);
}

- (id)answerAccessibilityMaxValue {
    return _node.maximum_value ? @(*_node.maximum_value) : nil;
}
- (BOOL)accessibilityPerformPress {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityPerformPress",
        &run_accessibility_element_accessibility_perform_press, self);
}

- (BOOL)answerAccessibilityPerformPress {
    if (_owner == nil) return NO;
    SemanticAction action = SemanticAction::press;
    if (!semantic_has_action(_node, action)) {
        if (semantic_has_action(_node, SemanticAction::select)) {
            action = SemanticAction::select;
        } else if (semantic_has_action(_node, SemanticAction::collapse)) {
            action = SemanticAction::collapse;
        } else if (semantic_has_action(_node, SemanticAction::expand)) {
            action = SemanticAction::expand;
        } else {
            return NO;
        }
    }
    return [self performNodeAction:action value:nil];
}
- (void)setAccessibilitySelected:(BOOL)selected {
    gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.setAccessibilitySelected:",
        &run_accessibility_element_set_accessibility_selected, self, selected);
}

- (void)answerSetAccessibilitySelected:(BOOL)selected {
    if (selected && _owner != nil &&
        semantic_has_action(_node, SemanticAction::select)) {
        static_cast<void>([self performNodeAction:SemanticAction::select value:nil]);
    }
}
- (BOOL)accessibilityPerformShowMenu {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityPerformShowMenu",
        &run_accessibility_element_accessibility_perform_show_menu, self);
}

- (BOOL)answerAccessibilityPerformShowMenu {
    if (_owner == nil) return NO;
    if (semantic_has_action(_node, SemanticAction::show_menu)) {
        return [self performNodeAction:SemanticAction::show_menu value:nil];
    }
    if (semantic_has_action(_node, SemanticAction::expand)) {
        return [self performNodeAction:SemanticAction::expand value:nil];
    }
    // No menu of the element's own: the show-menu action is macOS's
    // keyboard context request for the element's control, which takes
    // focus first so that the request reaches it.
    if (!semantic_has_action(_node, SemanticAction::focus)) return NO;
    if (![self performNodeAction:SemanticAction::focus value:nil]) return NO;
    return [_owner dispatchKeyboardContextRequest];
}
- (BOOL)accessibilityPerformIncrement {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityPerformIncrement",
        &run_accessibility_element_accessibility_perform_increment, self);
}

- (BOOL)answerAccessibilityPerformIncrement {
    if (_owner == nil || !semantic_has_action(_node, SemanticAction::increment)) return NO;
    return [self performNodeAction:SemanticAction::increment value:nil];
}
- (BOOL)accessibilityPerformDecrement {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityPerformDecrement",
        &run_accessibility_element_accessibility_perform_decrement, self);
}

- (BOOL)answerAccessibilityPerformDecrement {
    if (_owner == nil || !semantic_has_action(_node, SemanticAction::decrement)) return NO;
    return [self performNodeAction:SemanticAction::decrement value:nil];
}
- (void)setAccessibilityFocused:(BOOL)focused {
    gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.setAccessibilityFocused:",
        &run_accessibility_element_set_accessibility_focused, self, focused);
}

- (void)answerSetAccessibilityFocused:(BOOL)focused {
    if (focused && _owner != nil &&
        semantic_has_action(_node, SemanticAction::focus)) {
        static_cast<void>([self performNodeAction:SemanticAction::focus value:nil]);
    }
}
- (void)setAccessibilityValue:(id)value {
    gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.setAccessibilityValue:",
        &run_accessibility_element_set_accessibility_value, self, value);
}

- (void)answerSetAccessibilityValue:(id)value {
    if (_owner == nil || !semantic_has_action(_node, SemanticAction::set_value)) return;
    NSString* text = [value isKindOfClass:[NSString class]]
        ? (NSString*)value : [value stringValue];
    static_cast<void>([self performNodeAction:SemanticAction::set_value value:text]);
}

// Brings this element's node up to date and makes _textLayout its current
// layout: the one kept, while the tree key reads the same, else read again.
// Returns NO when the node publishes no text ranges, its control has left
// the window or the window has gone.
- (BOOL)readSemanticText {
    if (_owner == nil || !publishes_text_ranges(_node)) return NO;
    try {
        // The view republishes its tree when the semantic generation has
        // moved, which updates this element's node in place.
        static_cast<void>([_owner accessibilityChildren]);
        if (!publishes_text_ranges(_node)) return NO;
        MacAccessibilityTreeKey key;
        if (![_owner readAccessibilityTreeKey:key]) return NO;
        if (_textLayoutCurrent && key == _textLayoutKey) return YES;
        _textLayoutCurrent = NO;
        if (![_owner readSemanticTextLayout:_control layout:_textLayout]) return NO;
        // The key after the read, in case the query itself settled a layout.
        if (![_owner readAccessibilityTreeKey:_textLayoutKey]) return NO;
        _textLayoutCurrent = YES;
        return YES;
    } catch (...) {
        return NO;
    }
}
// The selection's ends in bytes, low first, within the value.
- (void)selectionBytesLow:(std::size_t&)low high:(std::size_t&)high {
    const SemanticTextSelection selection = *_node.text_selection;
    const std::size_t size = _node.value.size();
    low = std::min(std::min(selection.anchor.value(), selection.caret.value()), size);
    high = std::min(std::max(selection.anchor.value(), selection.caret.value()), size);
}
- (NSInteger)accessibilityNumberOfCharacters {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityNumberOfCharacters",
        &run_accessibility_element_accessibility_number_of_characters, self);
}

- (NSInteger)answerAccessibilityNumberOfCharacters {
    if (![self readSemanticText]) return 0;
    const std::size_t units = utf16_units_before(_node.value, _node.value.size());
    return static_cast<NSInteger>(units);
}
- (NSRange)accessibilitySelectedTextRange {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilitySelectedTextRange",
        &run_accessibility_element_accessibility_selected_text_range, self);
}

- (NSRange)answerAccessibilitySelectedTextRange {
    if (![self readSemanticText]) return NSMakeRange(0, 0);
    std::size_t low = 0U;
    std::size_t high = 0U;
    [self selectionBytesLow:low high:high];
    return utf16_range(_node.value, low, high);
}
// The selection becomes the range, anchored at its start with the caret at
// its end.
- (void)setAccessibilitySelectedTextRange:(NSRange)range {
    gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.setAccessibilitySelectedTextRange:",
        &run_accessibility_element_set_accessibility_selected_text_range, self, range);
}

- (void)answerSetAccessibilitySelectedTextRange:(NSRange)range {
    if (![self readSemanticText]) return;
    std::size_t start = 0U;
    std::size_t end = 0U;
    if (!utf8_range(_node.value, range, start, end)) return;
    const SemanticTextSelection selection{.anchor = gui_forms::Utf8Offset(start),
                                          .caret = gui_forms::Utf8Offset(end)};
    static_cast<void>([_owner selectSemanticText:_control selection:selection]);
}
- (NSString*)accessibilitySelectedText {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilitySelectedText",
        &run_accessibility_element_accessibility_selected_text, self);
}

- (NSString*)answerAccessibilitySelectedText {
    if (![self readSemanticText]) return nil;
    std::size_t low = 0U;
    std::size_t high = 0U;
    [self selectionBytesLow:low high:high];
    const std::string_view selected = std::string_view(_node.value).substr(low, high - low);
    return native_string(selected);
}
// From the first grapheme whose box meets the node's bounds to the last.
- (NSRange)accessibilityVisibleCharacterRange {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityVisibleCharacterRange",
        &run_accessibility_element_accessibility_visible_character_range, self);
}

- (NSRange)answerAccessibilityVisibleCharacterRange {
    if (![self readSemanticText]) return NSMakeRange(0, 0);
    std::optional<std::size_t> first;
    std::size_t last = 0U;
    for (const SemanticTextGrapheme& grapheme : _textLayout.graphemes) {
        const GFRect shown = GFRect::intersection(grapheme.bounds, _node.bounds);
        if (shown.empty()) continue;
        if (!first) first = grapheme.range.start.value();
        last = grapheme.range.end.value();
    }
    if (!first) return NSMakeRange(0, 0);
    return utf16_range(_node.value, *first, last);
}
- (NSInteger)accessibilityInsertionPointLineNumber {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityInsertionPointLineNumber",
        &run_accessibility_element_accessibility_insertion_point_line_number, self);
}

- (NSInteger)answerAccessibilityInsertionPointLineNumber {
    if (![self readSemanticText]) return 0;
    const std::size_t caret = (*_node.text_selection).caret.value();
    return static_cast<NSInteger>(line_of_offset(_textLayout, caret));
}
- (NSString*)accessibilityStringForRange:(NSRange)range {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityStringForRange:",
        &run_accessibility_element_accessibility_string_for_range, self, range);
}

- (NSString*)answerAccessibilityStringForRange:(NSRange)range {
    if (![self readSemanticText]) return nil;
    std::size_t start = 0U;
    std::size_t end = 0U;
    if (!utf8_range(_node.value, range, start, end)) return nil;
    const std::string_view text = std::string_view(_node.value).substr(start, end - start);
    return native_string(text);
}
// The text has one style run, so the attributed string carries no
// attributes.
- (NSAttributedString*)accessibilityAttributedStringForRange:(NSRange)range {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityAttributedStringForRange:",
        &run_accessibility_element_accessibility_attributed_string_for_range, self, range);
}

- (NSAttributedString*)answerAccessibilityAttributedStringForRange:(NSRange)range {
    NSString* const text = [self accessibilityStringForRange:range];
    if (text == nil) return nil;
    return [[NSAttributedString alloc] initWithString:text];
}
- (NSInteger)accessibilityLineForIndex:(NSInteger)index {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityLineForIndex:",
        &run_accessibility_element_accessibility_line_for_index, self, index);
}

- (NSInteger)answerAccessibilityLineForIndex:(NSInteger)index {
    if (![self readSemanticText] || index < 0) return 0;
    const std::size_t offset =
        utf8_offset_at_units(_node.value, static_cast<std::size_t>(index), SurrogateRounding::down);
    return static_cast<NSInteger>(line_of_offset(_textLayout, offset));
}
// A line runs to the next line's start, so it holds its break.
- (NSRange)accessibilityRangeForLine:(NSInteger)line {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRangeForLine:",
        &run_accessibility_element_accessibility_range_for_line, self, line);
}

- (NSRange)answerAccessibilityRangeForLine:(NSInteger)line {
    if (![self readSemanticText] || line < 0) return NSMakeRange(NSNotFound, 0);
    const std::size_t index = static_cast<std::size_t>(line);
    const std::vector<SemanticTextLine>& lines = _textLayout.lines;
    if (index >= lines.size()) return NSMakeRange(NSNotFound, 0);
    const std::size_t start = lines[index].range.start.value();
    const std::size_t end = index + 1U < lines.size() ? lines[index + 1U].range.start.value() : _node.value.size();
    return utf16_range(_node.value, start, end);
}
// The union of the range's grapheme boxes (the caret's box for an empty
// range), cut to the node's bounds, in screen coordinates.
- (NSRect)accessibilityFrameForRange:(NSRange)range {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityFrameForRange:",
        &run_accessibility_element_accessibility_frame_for_range, self, range);
}

- (NSRect)answerAccessibilityFrameForRange:(NSRange)range {
    if (![self readSemanticText]) return NSZeroRect;
    std::size_t start = 0U;
    std::size_t end = 0U;
    if (!utf8_range(_node.value, range, start, end)) return NSZeroRect;
    const std::optional<GFRect> box = range_box(_textLayout, start, end);
    if (!box) return NSZeroRect;
    const GFRect shown = GFRect::intersection(*box, _node.bounds);
    if (shown.empty()) return NSZeroRect;
    return [_owner accessibilityFrameForSemanticBounds:shown];
}
// The composed character at a screen point.
- (NSRange)accessibilityRangeForPosition:(NSPoint)point {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRangeForPosition:",
        &run_accessibility_element_accessibility_range_for_position, self, point);
}

- (NSRange)answerAccessibilityRangeForPosition:(NSPoint)point {
    if (![self readSemanticText]) return NSMakeRange(NSNotFound, 0);
    const NSPoint local = [_owner semanticPointForScreenPoint:point];
    const SemanticTextGrapheme* const hit = grapheme_at_point(_textLayout, local.x, local.y);
    if (hit == nullptr) return NSMakeRange(NSNotFound, 0);
    return utf16_range(_node.value, (*hit).range.start.value(), (*hit).range.end.value());
}
// The composed character holding a UTF-16 index: its grapheme, or the line
// break there, which is one character.
- (NSRange)accessibilityRangeForIndex:(NSInteger)index {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityRangeForIndex:",
        &run_accessibility_element_accessibility_range_for_index, self, index);
}

- (NSRange)answerAccessibilityRangeForIndex:(NSInteger)index {
    if (![self readSemanticText] || index < 0) return NSMakeRange(NSNotFound, 0);
    const std::size_t offset =
        utf8_offset_at_units(_node.value, static_cast<std::size_t>(index), SurrogateRounding::down);
    if (offset >= _node.value.size()) return NSMakeRange(NSNotFound, 0);
    for (const SemanticTextGrapheme& grapheme : _textLayout.graphemes) {
        if (grapheme.range.start.value() <= offset && offset < grapheme.range.end.value()) {
            return utf16_range(_node.value, grapheme.range.start.value(), grapheme.range.end.value());
        }
    }
    const std::size_t next = offset + utf8_sequence_bytes(static_cast<unsigned char>(_node.value[offset]));
    return utf16_range(_node.value, offset, next);
}
- (NSRange)accessibilityStyleRangeForIndex:(NSInteger)index {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityStyleRangeForIndex:",
        &run_accessibility_element_accessibility_style_range_for_index, self, index);
}

- (NSRange)answerAccessibilityStyleRangeForIndex:(NSInteger)index {
    if (![self readSemanticText] || index < 0) return NSMakeRange(NSNotFound, 0);
    return utf16_range(_node.value, 0U, _node.value.size());
}

- (BOOL)isAccessibilitySelectorAllowed:(SEL)selector {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.isAccessibilitySelectorAllowed:",
        &run_accessibility_element_is_accessibility_selector_allowed, self, selector);
}

- (BOOL)answerIsAccessibilitySelectorAllowed:(SEL)selector {
    if (text_range_selector(selector)) {
        return publishes_text_ranges(_node) ? YES : NO;
    }
    if (selector == @selector(setAccessibilityFocused:)) {
        return semantic_has_action(_node, SemanticAction::focus);
    }
    if (selector == @selector(setAccessibilityValue:)) {
        return semantic_has_action(_node, SemanticAction::set_value);
    }
    if (selector == @selector(setAccessibilitySelected:)) {
        return semantic_has_action(_node, SemanticAction::select);
    }
    if (selector == @selector(accessibilityPerformPress)) {
        return semantic_has_action(_node, SemanticAction::press) ||
               semantic_has_action(_node, SemanticAction::select) ||
               semantic_has_action(_node, SemanticAction::expand) ||
               semantic_has_action(_node, SemanticAction::collapse);
    }
    if (selector == @selector(accessibilityPerformShowMenu)) {
        return semantic_has_action(_node, SemanticAction::show_menu) ||
               semantic_has_action(_node, SemanticAction::expand) ||
               semantic_has_action(_node, SemanticAction::focus);
    }
    if (selector == @selector(accessibilityPerformIncrement)) {
        return semantic_has_action(_node, SemanticAction::increment);
    }
    if (selector == @selector(accessibilityPerformDecrement)) {
        return semantic_has_action(_node, SemanticAction::decrement);
    }
    if (selector == @selector(accessibilityRowIndexRange)) {
        return _node.row_index && *_node.row_index != 0 ? YES : NO;
    }
    if (selector == @selector(accessibilityColumnIndexRange)) {
        return _node.column_index && *_node.column_index != 0 ? YES : NO;
    }
    return [super isAccessibilitySelectorAllowed:selector];
}

- (BOOL)accessibilityIsAttributeSettable:(NSAccessibilityAttributeName)attribute {
    return gui_forms::detail::call_failing_fast(
        "macos.accessibility_element.accessibilityIsAttributeSettable:",
        &run_accessibility_element_accessibility_is_attribute_settable, self, attribute);
}

- (BOOL)answerAccessibilityIsAttributeSettable:(NSAccessibilityAttributeName)attribute {
    if ([attribute isEqualToString:NSAccessibilityFocusedAttribute]) {
        return semantic_has_action(_node, SemanticAction::focus);
    }
    if ([attribute isEqualToString:NSAccessibilityValueAttribute]) {
        return semantic_has_action(_node, SemanticAction::set_value);
    }
    if ([attribute isEqualToString:NSAccessibilitySelectedAttribute]) {
        return semantic_has_action(_node, SemanticAction::select);
    }
    if ([attribute isEqualToString:NSAccessibilitySelectedTextRangeAttribute]) {
        return publishes_text_ranges(_node) ? YES : NO;
    }
    return [super accessibilityIsAttributeSettable:attribute];
}

@end


// The key of an element in the view's element table: the publishing
// control's runtime identity, then the virtual child's stable identity
// (empty for a control's own node). Unique within one window's tree; a
// stable identity alone is not, since controls that show one model repeat
// their virtual children's identities.
static NSString* accessibility_element_key(std::uint64_t control, std::string_view virtual_child) {
    std::string key = std::to_string(control);
    key += '/';
    key += virtual_child;
    NSString* result = native_string(key);
    return result;
}

// Publishes `node` under `parent`, reusing the element `previous` holds for
// its key and recording it in `next`; `by_stable_id` records the first
// element in tree order for each stable identity (the tab-to-panel
// relation). `owner_control` is the runtime identity of the nearest control
// node at or above `parent`, which publishes `node` when it is a virtual
// child. The node's children are moved out of it first, so the element
// keeps a copy of the node's own fields only (audit N100: no element holds
// a deep copy of its subtree); the snapshot is not read again.
static GUIFormsAccessibilityElement* reconcile_accessibility_element(
    SemanticNode& node,
    GUIFormsView* owner,
    id parent,
    std::uint64_t owner_control,
    NSDictionary<NSString*, GUIFormsAccessibilityElement*>* previous,
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* next,
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* by_stable_id) {
    std::vector<SemanticNode> child_nodes = std::move(node.children);
    node.children.clear();
    const bool virtual_child = gui_forms::is_virtual_semantic_runtime_id(node.runtime_id);
    const std::uint64_t control = virtual_child ? owner_control : node.runtime_id;
    const std::string child_identity = virtual_child ? node.stable_id : std::string{};
    NSString* key = accessibility_element_key(control, child_identity);
    GUIFormsAccessibilityElement* element = previous[key];
    if (element == nil) {
        element = [[GUIFormsAccessibilityElement alloc] initWithNode:node
                                                               owner:owner
                                                              parent:parent
                                                             control:control
                                                        virtualChild:child_identity];
        // A new live region that announces on appearance (a toast) is
        // spoken as it appears.
        if (gui_forms::semantic_announcement_due(nullptr, node)) announce_live_region(owner, node.name);
    } else {
        [element updateWithNode:node owner:owner parent:parent];
    }
    next[key] = element;
    NSString* stable_identity = native_string(node.stable_id);
    if (by_stable_id[stable_identity] == nil) by_stable_id[stable_identity] = element;
    NSMutableArray* children = [[NSMutableArray alloc]
        initWithCapacity:child_nodes.size()];
    for (SemanticNode& childNode : child_nodes) {
        [children addObject:reconcile_accessibility_element(
            childNode, owner, element, control, previous, next, by_stable_id)];
    }
    [element setSemanticChildren:children];
    return element;
}

// Main-thread work through the run loop rather than the main dispatch
// queue. The main queue is serial: while one of its functions runs, a
// nested event loop inside it (a modal window's, HostWindowing::run_modal)
// would never see the queue's later work, so posted work, damage
// collection and close requests would stall until the modal window closed.
// A perform request on the main thread in the common modes is a run loop
// source, which the modal session's run loop mode (a common mode) services
// at any depth. Thread-safe; the target and its context are released after
// the function runs.
@interface GUIFormsMainWork : NSObject
- (instancetype)initWithFunction:(void (*)(void*))function context:(void*)context;
- (void)perform;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerPerform;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsMainWork's native entries: each sends the entry's answer message.
void run_main_work_perform(GUIFormsMainWork* object) {
    [object answerPerform];
}
} // namespace

@implementation GUIFormsMainWork {
    void (*_function)(void*);
    void* _context;
}

- (instancetype)initWithFunction:(void (*)(void*))function context:(void*)context {
    self = [super init];
    if (self != nil) {
        _function = function;
        _context = context;
    }
    return self;
}

- (void)perform {
    gui_forms::detail::call_failing_fast("macos.main_work.perform", &run_main_work_perform, self);
}

- (void)answerPerform {
    void (*const function)(void*) = _function;
    void* const context = _context;
    _function = nullptr;
    _context = nullptr;
    if (function != nullptr) function(context);
}
@end

namespace {
// Runs function(context) on the main thread from the run loop, in the
// common modes. The function adopts the context as dispatch_async_f's
// would.
void post_main_work(void* context, void (*function)(void*)) {
    GUIFormsMainWork* const work = [[GUIFormsMainWork alloc] initWithFunction:function context:context];
    NSArray<NSString*>* const modes = @[ NSRunLoopCommonModes ];
    [work performSelectorOnMainThread:@selector(perform) withObject:nil waitUntilDone:NO modes:modes];
}
} // namespace

// Main-queue work without Objective-C blocks (house style section 9, owner
// decision V2-1). Each posting allocates one named context on the heap and
// passes it to dispatch_async_f with a named dispatch_function_t; the
// function adopts the context (std::unique_ptr) and so deletes it whether or
// not its target still lives. Contexts hold their Objective-C objects as the
// blocks they replace captured them: __weak where the block re-checked a
// weak reference, strong (ARC's default) where it captured the object.

namespace {
// A view that may be gone by the time the work runs.
struct WeakViewWork final {
    __weak GUIFormsView* view;
};

void drain_posted_work_on_main(void* context) {
    const std::unique_ptr<WeakViewWork> work(static_cast<WeakViewWork*>(context));
    GUIFormsView* const view = (*work).view;
    if (view != nil) [view drainPostedWork];
}

void collect_damage_on_main(void* context) {
    const std::unique_ptr<WeakViewWork> work(static_cast<WeakViewWork*>(context));
    GUIFormsView* const view = (*work).view;
    if (view != nil) [view collectDamage];
}

// The wake timer's event handler. Its context belongs to the dispatch
// source (dispatch_set_context) and is deleted by release_weak_view_work,
// the source's finalizer, when the source is released.
void scheduled_wake_on_main(void* context) {
    WeakViewWork& work = *static_cast<WeakViewWork*>(context);
    GUIFormsView* const view = work.view;
    if (view != nil) [view scheduledWake];
}

void release_weak_view_work(void* context) {
    const std::unique_ptr<WeakViewWork> work(static_cast<WeakViewWork*>(context));
}
} // namespace

class DrainPostedWorkWake final {
public:
    explicit DrainPostedWorkWake(GUIFormsView* view) noexcept
        : view_(view) {}

    void operator()() const
    {
        WeakViewWork* const work = new WeakViewWork{view_};
        post_main_work(work, &drain_posted_work_on_main);
    }

private:
    __weak GUIFormsView* view_;
};

class CollectDamageWake final {
public:
    explicit CollectDamageWake(GUIFormsView* view) noexcept
        : view_(view) {}

    void operator()() const
    {
        WeakViewWork* const work = new WeakViewWork{view_};
        post_main_work(work, &collect_damage_on_main);
    }

private:
    __weak GUIFormsView* view_;
};

namespace {
// A window the work keeps alive until it runs, as the replaced blocks did.
struct WindowWork final {
    NSWindow* window;
};

void perform_close_on_main(void* context) {
    const std::unique_ptr<WindowWork> work(static_cast<WindowWork*>(context));
    [(*work).window performClose:nil];
}

void post_perform_close(NSWindow* window) {
    WindowWork* const work = new WindowWork{window};
    post_main_work(work, &perform_close_on_main);
}
// The named targets the fail-fast boundary calls for GUIFormsView's native
// entries (call_failing_fast): each sends the entry's body message.
void run_display_link_tick(GUIFormsView* view) {
    [view answerDisplayLinkTick];
}
void run_posted_work(GUIFormsView* view) {
    [view answerDrainPostedWork];
}
void run_collect_damage(GUIFormsView* view) {
    [view collectDamage];
}
void run_draw_retained(GUIFormsView* view, NSRect dirty) {
    [view drawRetainedRect:dirty];
}
void run_view_accessibility_display_options_changed(GUIFormsView* object, NSNotification* notification) {
    [object answerAccessibilityDisplayOptionsChanged:notification];
}
void run_view_system_colours_changed(GUIFormsView* object, NSNotification* notification) {
    [object answerSystemColoursChanged:notification];
}
void run_view_view_did_change_effective_appearance(GUIFormsView* object) {
    [object answerViewDidChangeEffectiveAppearance];
}
NSRect run_view_accessibility_frame(GUIFormsView* object) {
    return [object answerAccessibilityFrame];
}
void run_view_screen_parameters_changed(GUIFormsView* object, NSNotification* notification) {
    [object answerScreenParametersChanged:notification];
}
NSString* run_view_accessibility_label(GUIFormsView* object) {
    return [object answerAccessibilityLabel];
}
NSArray* run_view_accessibility_children(GUIFormsView* object) {
    return [object answerAccessibilityChildren];
}
NSArray* run_view_accessibility_children_in_navigation_order(GUIFormsView* object) {
    return [object answerAccessibilityChildrenInNavigationOrder];
}
NSArray* run_view_accessibility_visible_children(GUIFormsView* object) {
    return [object answerAccessibilityVisibleChildren];
}
NSRect run_view_accessibility_frame_for_semantic_bounds(GUIFormsView* object, GFRect bounds) {
    return [object answerAccessibilityFrameForSemanticBounds:bounds];
}
void run_view_view_did_move_to_window(GUIFormsView* object) {
    [object answerViewDidMoveToWindow];
}
void run_view_view_will_move_to_window(GUIFormsView* object, NSWindow* newWindow) {
    [object answerViewWillMoveToWindow:newWindow];
}
void run_view_dealloc(__unsafe_unretained GUIFormsView* object) {
    [object answerDealloc];
}
void run_view_set_frame_size(GUIFormsView* object, NSSize newSize) {
    [object answerSetFrameSize:newSize];
}
void run_view_update_tracking_areas(GUIFormsView* object) {
    [object answerUpdateTrackingAreas];
}
void run_view_invalidation_overlay_tick(GUIFormsView* object) {
    [object answerInvalidationOverlayTick];
}
NSDragOperation run_view_dragging_entered(GUIFormsView* object, id<NSDraggingInfo> sender) {
    return [object answerDraggingEntered:sender];
}
NSDragOperation run_view_dragging_updated(GUIFormsView* object, id<NSDraggingInfo> sender) {
    return [object answerDraggingUpdated:sender];
}
void run_view_dragging_exited(GUIFormsView* object, id<NSDraggingInfo> sender) {
    [object answerDraggingExited:sender];
}
BOOL run_view_prepare_for_drag_operation(GUIFormsView* object, id<NSDraggingInfo> sender) {
    return [object answerPrepareForDragOperation:sender];
}
BOOL run_view_perform_drag_operation(GUIFormsView* object, id<NSDraggingInfo> sender) {
    return [object answerPerformDragOperation:sender];
}
void run_view_cursor_update(GUIFormsView* object, NSEvent* event) {
    [object answerCursorUpdate:event];
}
void run_view_mouse_exited(GUIFormsView* object, NSEvent* event) {
    [object answerMouseExited:event];
}
void run_view_mouse_down(GUIFormsView* object, NSEvent* event) {
    [object answerMouseDown:event];
}
void run_view_mouse_up(GUIFormsView* object, NSEvent* event) {
    [object answerMouseUp:event];
}
void run_view_right_mouse_down(GUIFormsView* object, NSEvent* event) {
    [object answerRightMouseDown:event];
}
NSMenu* run_view_menu_for_event(GUIFormsView* object, NSEvent* event) {
    return [object answerMenuForEvent:event];
}
void run_view_right_mouse_up(GUIFormsView* object, NSEvent* event) {
    [object answerRightMouseUp:event];
}
void run_view_other_mouse_down(GUIFormsView* object, NSEvent* event) {
    [object answerOtherMouseDown:event];
}
void run_view_other_mouse_up(GUIFormsView* object, NSEvent* event) {
    [object answerOtherMouseUp:event];
}
void run_view_mouse_moved(GUIFormsView* object, NSEvent* event) {
    [object answerMouseMoved:event];
}
void run_view_mouse_dragged(GUIFormsView* object, NSEvent* event) {
    [object answerMouseDragged:event];
}
void run_view_right_mouse_dragged(GUIFormsView* object, NSEvent* event) {
    [object answerRightMouseDragged:event];
}
void run_view_other_mouse_dragged(GUIFormsView* object, NSEvent* event) {
    [object answerOtherMouseDragged:event];
}
void run_view_scroll_wheel(GUIFormsView* object, NSEvent* event) {
    [object answerScrollWheel:event];
}
void run_view_key_down(GUIFormsView* object, NSEvent* event) {
    [object answerKeyDown:event];
}
void run_view_key_up(GUIFormsView* object, NSEvent* event) {
    [object answerKeyUp:event];
}
void run_view_flags_changed(GUIFormsView* object, NSEvent* event) {
    [object answerFlagsChanged:event];
}
void run_view_insert_text(GUIFormsView* object, id string, NSRange replacementRange) {
    [object answerInsertText:string replacementRange:replacementRange];
}
void run_view_set_marked_text(GUIFormsView* object, id string, NSRange selectedRange, NSRange replacementRange) {
    [object answerSetMarkedText:string selectedRange:selectedRange replacementRange:replacementRange];
}
void run_view_unmark_text(GUIFormsView* object) {
    [object answerUnmarkText];
}
BOOL run_view_has_marked_text(GUIFormsView* object) {
    return [object answerHasMarkedText];
}
NSRange run_view_marked_range(GUIFormsView* object) {
    return [object answerMarkedRange];
}
NSRange run_view_selected_range(GUIFormsView* object) {
    return [object answerSelectedRange];
}
NSAttributedString* run_view_attributed_substring_for_proposed_range(
    GUIFormsView* object, NSRange range, NSRangePointer actualRange) {
    return [object answerAttributedSubstringForProposedRange:range actualRange:actualRange];
}
NSArray<NSAttributedStringKey>* run_view_valid_attributes_for_marked_text(GUIFormsView* object) {
    return [object answerValidAttributesForMarkedText];
}
NSRect run_view_first_rect_for_character_range(GUIFormsView* object, NSRange range, NSRangePointer actualRange) {
    return [object answerFirstRectForCharacterRange:range actualRange:actualRange];
}
void run_view_do_command_by_selector(GUIFormsView* object, SEL selector) {
    [object answerDoCommandBySelector:selector];
}
} // namespace

@implementation GUIFormsView

- (instancetype)initWithModel:(std::unique_ptr<Window>)model
       transparentFullSizeContent:(BOOL)transparentFullSizeContent
              windowDragRegionIds:(std::vector<std::string>)windowDragRegionIds {
    self = [super initWithFrame:NSMakeRect(0.0, 0.0, 980.0, 680.0)];
    if (self != nil) {
        _model = std::move(model);
        // The availability record (stage DP): AppKit clips each copy to the
        // rectangles being drawn, so the host presents a rectangle set.
        gui_forms::PresentationDeclaration presentation;
        presentation.contract_version = gui_forms::paint_contract_version;
        presentation.extensions.partial_present = true;
        (*_model).declare_presentation(presentation);
        _hostServices = gui_forms::host::make_macos_host_services();
        gui_forms::host::detail::bind_macos_backdrop_view(*_hostServices, self);
        _compositorGeneration = gui_forms::CompositorGeneration{};
        _hostSession = std::make_unique<HostSession>(
            *_model, gui_forms::host::macos_capabilities(), _hostServices.get());
        _nextHostSequence = 1;
        _hostAttached = NO;
        _lastSemanticGeneration = 0;
        _accessibilityCacheKey = MacAccessibilityTreeKey{};
        _initialHostOccluded = YES;
        _scheduledWakeCount = 0;
        _damageCollectionCount = 0;
        _nativeDrawCount = 0;
        _displayTickCount = 0;
        _rasterPreparePhase = {};
        _retainedPaintPhase = {};
        _rasterFinishPhase = {};
        _cgSetupPhase = {};
        _cgDrawPhase = {};
        _cgReleasePhase = {};
        _cpuAttribution = {};
        _causalTrace = nullptr;
        _lastNativeDirtyArea = 0.0;
        _lastFrameDamageBoundsArea = 0.0;
        _lastCGDestinationArea = 0.0;
        _lastCGClipBoundsArea = 0.0;
        _lastCGSourceBytes = 0U;
        _transparentFullSizeContent = transparentFullSizeContent;
        _windowDragRegionIds = std::move(windowDragRegionIds);
        _semanticAccessibilityChildren = nil;
        _publishesLiveRegion = NO;
        _publishesTextRanges = NO;
        _semanticAccessibilityElements = [[NSMutableDictionary alloc] init];
        _semanticAccessibilityElementsByStableId = [[NSMutableDictionary alloc] init];
        _markedText = [[NSMutableAttributedString alloc] init];
        _selectedRange = NSMakeRange(NSNotFound, 0);
        _displayLink = nil;
        _hostOccluded = NO;
        _presentColorSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        _wakeSource = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_TIMER, 0, 0, dispatch_get_main_queue());
        // The source owns its context; its finalizer deletes it.
        WeakViewWork* const wakeWork = new WeakViewWork{self};
        dispatch_set_context(_wakeSource, wakeWork);
        dispatch_set_finalizer_f(_wakeSource, &release_weak_view_work);
        dispatch_source_set_event_handler_f(_wakeSource, &scheduled_wake_on_main);
        dispatch_resume(_wakeSource);
        dispatch_source_set_timer(_wakeSource, DISPATCH_TIME_FOREVER,
                                  DISPATCH_TIME_FOREVER, 0);
        (*_model).set_dispatch_wake_handler(DrainPostedWorkWake(self));
        (*_model).set_paint_wake_handler(CollectDamageWake(self));
        // The raster keeps its pixels between draws, so a scroll's pixels
        // can be moved instead of repainted (drawRetainedRect:).
        (*_model).set_scroll_copies_supported(true);
        (*_model).set_live_surface_idle_wake_handler(CollectDamageWake(self));
        [self setWantsLayer:NO];
        [self registerForDraggedTypes:@[
            NSPasteboardTypeString, NSPasteboardTypeFileURL, @"public.data"]];
        const bool fonts_ready = _raster.register_bundled_fonts(bundle_font_set());
        (*_model).set_text_metrics_provider(fonts_ready ? &_raster : nullptr);
        (*_model).set_framebuffer_painter(fonts_ready ? &_raster : nullptr);
        (*_model).metrics().set_renderer(
            fonts_ready
                ? "Skia CPU m152 · HarfBuzz 14.6.0 · FreeType 2.14.2 · bundled fonts (optional Unicode fallback)"
                : "Skia CPU m152 · incomplete bundled font pack",
            true);
        [[NSNotificationCenter defaultCenter]
            addObserver:self
               selector:@selector(screenParametersChanged:)
                   name:NSApplicationDidChangeScreenParametersNotification
                 object:nil];
        // Posted on the workspace's own centre, never the default one.
        [NSWorkspace.sharedWorkspace.notificationCenter
            addObserver:self
               selector:@selector(accessibilityDisplayOptionsChanged:)
                   name:NSWorkspaceAccessibilityDisplayOptionsDidChangeNotification
                 object:nil];
        // The accent and highlight colours (System Settings, Appearance).
        [[NSNotificationCenter defaultCenter]
            addObserver:self
               selector:@selector(systemColoursChanged:)
                   name:NSSystemColorsDidChangeNotification
                 object:nil];
    }
    return self;
}

- (BOOL)initializeHost {
    if (_hostAttached == YES) return YES;
    const NSRect bounds = self.bounds;
    const double scale = self.window == nil ? 1.0 : self.window.backingScaleFactor;
    const HostDispatchResult result = [self dispatchHostPayload:
        HostAttachEvent{GFSize{std::max(1.0, bounds.size.width),
                               std::max(1.0, bounds.size.height)}, scale}
                               timestampNanoseconds:host_now_nanoseconds()];
    if (!result.accepted()) return NO;
    _hostAttached = YES;
    [self reportInputSettings];
    // Initial ordering can precede attachment, when occlusion notifications
    // cannot enter the host session. Establish native state before scheduling.
    NSWindow* nativeWindow = self.window;
    const bool nativeVisible = nativeWindow != nil && nativeWindow.isVisible &&
        (nativeWindow.occlusionState & NSWindowOcclusionStateVisible) != 0;
    _initialHostOccluded = nativeVisible ? NO : YES;
    [self notifyOcclusion:_initialHostOccluded];
    [self notifyDisplaysChanged];
    [self reportCompositorState];
    [self notifyWindowState];
    [self collectDamage];
    [self startDisplayLinkIfNeeded];
    return YES;
}

- (NSView*)hostContentView {
    const bool nonOpaque = _model != nullptr && (*_model).non_opaque_declared();
    NSView* const content = gui_forms::host::detail::make_macos_content_view(self, nonOpaque);
    return content;
}

- (void)accessibilityDisplayOptionsChanged:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityDisplayOptionsChanged:",
        &run_view_accessibility_display_options_changed, self, notification);
}

- (void)answerAccessibilityDisplayOptionsChanged:(NSNotification*)notification {
    (void)notification;
    [self reportCompositorState];
    [self collectDamage];
}

- (void)systemColoursChanged:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.view.systemColoursChanged:", &run_view_system_colours_changed, self, notification);
}

- (void)answerSystemColoursChanged:(NSNotification*)notification {
    (void)notification;
    [self reportCompositorState];
    [self collectDamage];
}

// The system's Light or Dark reaches the view as a change of its effective
// appearance (the host sets no appearance of its own, so the view follows
// the application's): the state is read again, dark_scheme with it.
- (void)viewDidChangeEffectiveAppearance {
    gui_forms::detail::call_failing_fast(
        "macos.view.viewDidChangeEffectiveAppearance", &run_view_view_did_change_effective_appearance, self);
}

- (void)answerViewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    [self reportCompositorState];
    [self collectDamage];
}

// Reports the window server's state to the window (stage T5, C3): after
// attach, and whenever the accessibility display options, the system
// colours or the effective appearance (Light or Dark) change. A reading
// that differs from the window's last report only by its generation is not
// sent.
- (void)reportCompositorState {
    if (_hostAttached == NO || _model == nullptr) return;
    const gui_forms::CompositorGeneration next{_compositorGeneration.value + 1U};
    const gui_forms::CompositorState state = gui_forms::host::detail::read_macos_compositor_state(next);
    gui_forms::CompositorState unchanged = state;
    unchanged.generation = (*_model).compositor_state().generation;
    if (_compositorGeneration.value != 0U && unchanged == (*_model).compositor_state()) return;
    _compositorGeneration = next;
    static_cast<void>([self dispatchHostPayload:gui_forms::HostCompositorEvent{state}
                           timestampNanoseconds:host_now_nanoseconds()]);
}

- (void)installCloseRequestHandler:(std::function<void(HostCloseRequest&)>)handler {
    _closeRequestSubscription.disconnect();
    if (_hostSession != nullptr && handler) {
        _closeRequestSubscription = (*_hostSession).closing().subscribe(std::move(handler));
    }
}

- (HostDispatchResult)dispatchHostPayload:(HostEventPayload)payload
                         timestampNanoseconds:(std::uint64_t)timestamp {
    if (_hostSession == nullptr) {
        HostDispatchResult result;
        result.error = gui_forms::HostDispatchError::after_shutdown;
        return result;
    }
    HostEvent event;
    event.sequence = _nextHostSequence++;
    event.timestamp_nanoseconds = timestamp;
    event.payload = std::move(payload);
    return (*_hostSession).dispatch(std::move(event));
}

- (BOOL)requestCloseHiding:(BOOL)hiding reason:(HostCloseReason)reason {
    if (_hostAttached == NO) return NO;
    const HostDispatchResult result = [self dispatchHostPayload:
        HostCloseRequest{reason, false, hiding == YES}
                                           timestampNanoseconds:host_now_nanoseconds()];
    return result.accepted() && result.close_allowed;
}

- (BOOL)closeRefusedForReason:(HostCloseReason)reason {
    if (_hostAttached == NO) return NO;
    const HostDispatchResult result = [self dispatchHostPayload:HostCloseRequest{reason, false, false}
                                           timestampNanoseconds:host_now_nanoseconds()];
    const BOOL refused = result.accepted() && !result.close_allowed ? YES : NO;
    return refused;
}

- (void)installFrameCapturePath:(std::string)path {
    _frameCapturePath = std::move(path);
    _frameCaptureCompleted = NO;
    _frameCaptureSaved = NO;
}
- (BOOL)frameCaptureSaved {
    return _frameCaptureSaved;
}
- (void)completeFrameCaptureIfDue:(NSRect)dirtyRect presented:(BOOL)presented {
    if (_frameCaptureCompleted || _frameCapturePath.empty() || presented == NO ||
        !NSContainsRect(dirtyRect, self.bounds) || NSIsEmptyRect(self.bounds)) {
        return;
    }
    _frameCaptureCompleted = YES;
    // The front raster is the presented framebuffer: premultiplied BGRA8 that
    // CGContextDrawImage copied above. Encode it through the private Skia
    // adapter seam and report through the shared portable contract.
    const std::vector<std::byte> encoded = _raster.encode_png();
    _frameCaptureSaved = gui_forms::host::detail::complete_frame_capture(
        _frameCapturePath, encoded) ? YES : NO;
    post_perform_close(self.window);
}
- (void)notifyClosed {
    [self notifyClosedWithReason:HostCloseReason::user];
}
- (void)notifyClosedWithReason:(HostCloseReason)reason {
    if (_hostAttached == NO) return;
    static_cast<void>([self dispatchHostPayload:HostClosedEvent{reason}
                               timestampNanoseconds:host_now_nanoseconds()]);
}

// NSEvent.doubleClickInterval, reported after attach and again on each
// activation if it moved: AppKit posts no notification when the user changes
// it, and the user changes it in System Settings, outside this application.
- (void)reportInputSettings {
    if (_hostAttached == NO) return;
    const NSTimeInterval seconds = NSEvent.doubleClickInterval;
    const double milliseconds = std::isfinite(seconds) ? std::round(seconds * 1000.0) : 0.0;
    const double bounded = std::clamp(milliseconds, 1.0,
                                      static_cast<double>(gui_forms::maximum_double_click_milliseconds));
    const std::uint32_t interval = static_cast<std::uint32_t>(bounded);
    const gui_forms::CaretSettings caret = [self caretSettings];
    if (interval == _reportedDoubleClickMilliseconds && caret == _reportedCaret) return;
    gui_forms::HostInputSettingsEvent input{interval, gui_forms::SettingOrigin::host};
    input.caret = caret;
    const HostDispatchResult reported = [self dispatchHostPayload:input
                                             timestampNanoseconds:host_now_nanoseconds()];
    if (reported.accepted()) {
        _reportedDoubleClickMilliseconds = interval;
        _reportedCaret = caret;
    }
}

// The caret's blink phase (audit N75): the NSTextInsertionPointBlinkPeriodOn
// user default, in milliseconds, where set; a period above the maximum is
// the usual way to stop blinking and reports a caret that never blinks.
// Without the default, GUI.Forms' default with origin default_value.
- (gui_forms::CaretSettings)caretSettings {
    gui_forms::CaretSettings caret{};
    id stored = [NSUserDefaults.standardUserDefaults objectForKey:@"NSTextInsertionPointBlinkPeriodOn"];
    if (![stored isKindOfClass:NSNumber.class]) return caret;
    const double period = [(NSNumber*)stored doubleValue];
    if (!std::isfinite(period) || period <= 0.0) return caret;
    caret.origin = gui_forms::SettingOrigin::host;
    if (period > static_cast<double>(gui_forms::maximum_caret_blink_milliseconds)) {
        caret.blink_milliseconds = 0U;
        return caret;
    }
    const double rounded = std::max(1.0, std::round(period));
    caret.blink_milliseconds = static_cast<std::uint32_t>(rounded);
    return caret;
}

- (void)notifyActivation:(BOOL)active {
    if (_hostAttached == NO) return;
    if (active == YES) [self reportInputSettings];
    static_cast<void>([self dispatchHostPayload:HostActivationEvent{active == YES}
                               timestampNanoseconds:host_now_nanoseconds()]);
    [self collectDamage];
    if (active == YES) [self setNeedsDisplay:YES];
}

- (void)notifyOcclusion:(BOOL)occluded {
    if (_hostAttached == NO) return;
    static_cast<void>([self dispatchHostPayload:HostOcclusionEvent{occluded == YES}
                               timestampNanoseconds:host_now_nanoseconds()]);
    _hostOccluded = occluded;
    if (occluded == YES) {
        [self stopDisplayLink];
        [self armWakeTimer];
    } else {
        // What changed while hidden is in _pendingDamage (an occluded draw
        // keeps it, since the model declines to paint) or still in the model.
        // The raster holds the last presentation everywhere else.
        [self collectDamage];
        // AppKit may consume a display request while our model is still hidden.
        // Rearm native presentation even when no new model damage was produced;
        // with nothing owed, that draw copies the raster and paints nothing.
        [self setNeedsDisplay:YES];
        [self startDisplayLinkIfNeeded];
    }
}

- (void)notifyScaleChanged {
    if (_hostAttached == NO) return;
    const double scale = self.window == nil ? 1.0 : self.window.backingScaleFactor;
    static_cast<void>([self dispatchHostPayload:HostScaleEvent{scale}
                               timestampNanoseconds:host_now_nanoseconds()]);
    [self collectDamage];
}

// Reports the native window's full-screen state (HostWindowStateEvent;
// owner, 2026-11-08: the window outer band) after attach and when AppKit
// enters or leaves full screen. macOS reports no corners (no
// window_corners), so the session's corner request changes nothing here.
// A refused report is tried again on the next change.
- (void)notifyWindowState {
    if (_hostAttached == NO || self.window == nil) return;
    const BOOL fullScreen = ([self.window styleMask] & NSWindowStyleMaskFullScreen) != 0 ? YES : NO;
    if (_windowStateReported == YES && _reportedFullScreen == fullScreen) return;
    const HostDispatchResult reported = [self dispatchHostPayload:gui_forms::HostWindowStateEvent{fullScreen == YES}
                                              timestampNanoseconds:host_now_nanoseconds()];
    if (!reported.accepted()) return;
    _windowStateReported = YES;
    _reportedFullScreen = fullScreen;
    [self collectDamage];
}

// The surface's own element covers the content rectangle: a declared window
// outer band lies outside it (owner, 2026-11-08), as it lies outside every
// hit test.
- (NSRect)accessibilityFrame {
    return gui_forms::detail::call_failing_fast("macos.view.accessibilityFrame", &run_view_accessibility_frame, self);
}

- (NSRect)answerAccessibilityFrame {
    if (!_model) return [super accessibilityFrame];
    return [self accessibilityFrameForSemanticBounds:(*_model).content_rect()];
}

- (void)notifyDisplaysChanged {
    if (_hostAttached == NO || _hostServices == nullptr) {
        return;
    }
    HostMonitorResult monitors = (*_hostServices).query_monitors();
    if (!monitors.status.accepted()) {
        return;
    }
    static_cast<void>([self dispatchHostPayload:
        HostDisplayEvent{std::move(monitors.monitors)}
                               timestampNanoseconds:host_now_nanoseconds()]);
}

- (void)screenParametersChanged:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.view.screenParametersChanged:", &run_view_screen_parameters_changed, self, notification);
}

- (void)answerScreenParametersChanged:(NSNotification*)notification {
    [self notifyDisplaysChanged];
}

- (BOOL)isFlipped {
    return YES;
}

- (BOOL)acceptsFirstResponder {
    return YES;
}

- (BOOL)becomeFirstResponder {
    return YES;
}

- (BOOL)resignFirstResponder {
    return YES;
}

- (BOOL)isAccessibilityElement { return YES; }
- (NSString*)accessibilityRole { return NSAccessibilityGroupRole; }
- (NSString*)accessibilityLabel {
    return gui_forms::detail::call_failing_fast("macos.view.accessibilityLabel", &run_view_accessibility_label, self);
}

- (NSString*)answerAccessibilityLabel {
    return @"GUI.Forms retained surface";
}

- (NSArray*)accessibilityChildren {
    return gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityChildren", &run_view_accessibility_children, self);
}

- (NSArray*)answerAccessibilityChildren {
    if (!_model) return @[];
    // Every client query lands here; build a snapshot only when the key has
    // moved since the cached children were built.
    const MacAccessibilityTreeKey before = read_accessibility_tree_key(*_model);
    if (_semanticAccessibilityChildren != nil && _accessibilityCacheKey == before) {
        return _semanticAccessibilityChildren;
    }
    // Not const: reconciling moves each node's children out of it.
    gui_forms::SemanticSnapshot snapshot = (*_model).semantic_snapshot();
    // The snapshot lays the window out first, which can move the key; the
    // children describe the window as it is after that layout.
    const MacAccessibilityTreeKey after = read_accessibility_tree_key(*_model);
    NSMutableArray* result = [[NSMutableArray alloc]
        initWithCapacity:snapshot.roots.size()];
    bool live_region = false;
    bool text_ranges = false;
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* next =
        [[NSMutableDictionary alloc] initWithCapacity:snapshot.node_count];
    NSMutableDictionary<NSString*, GUIFormsAccessibilityElement*>* byStableId =
        [[NSMutableDictionary alloc] initWithCapacity:snapshot.node_count];
    for (SemanticNode& node : snapshot.roots) {
        // Read before reconciling, which empties the node's children.
        if (contains_live_region(node)) live_region = true;
        if (contains_text_ranges(node)) text_ranges = true;
        GUIFormsAccessibilityElement* element = reconcile_accessibility_element(
            node, self, self, 0U, _semanticAccessibilityElements, next, byStableId);
        [result addObject:element];
    }
    _publishesLiveRegion = live_region ? YES : NO;
    _publishesTextRanges = text_ranges ? YES : NO;
    _semanticAccessibilityElements = next;
    _semanticAccessibilityElementsByStableId = byStableId;
    _semanticAccessibilityChildren = [result copy];
    _accessibilityCacheKey = after;
    return _semanticAccessibilityChildren;
}

- (NSArray*)accessibilityChildrenInNavigationOrder {
    return gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityChildrenInNavigationOrder",
        &run_view_accessibility_children_in_navigation_order, self);
}

- (NSArray*)answerAccessibilityChildrenInNavigationOrder {
    return [self accessibilityChildren];
}

- (NSArray*)accessibilityVisibleChildren {
    return gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityVisibleChildren", &run_view_accessibility_visible_children, self);
}

- (NSArray*)answerAccessibilityVisibleChildren {
    return [self accessibilityChildren];
}

- (NSRect)accessibilityFrameForSemanticBounds:(GFRect)bounds {
    return gui_forms::detail::call_failing_fast(
        "macos.view.accessibilityFrameForSemanticBounds:",
        &run_view_accessibility_frame_for_semantic_bounds, self, bounds);
}

- (NSRect)answerAccessibilityFrameForSemanticBounds:(GFRect)bounds {
    if (self.window == nil) return NSZeroRect;
    const NSRect local = NSMakeRect(bounds.x, bounds.y, bounds.width, bounds.height);
    const NSRect inWindow = [self convertRect:local toView:nil];
    return [self.window convertRectToScreen:inWindow];
}

- (BOOL)readSemanticTextLayout:(std::uint64_t)control layout:(SemanticTextLayout&)layout {
    if (!_model) return NO;
    const gui_forms::RuntimeId identity{control};
    const gui_forms::SemanticTextResult result = (*_model).semantic_text_layout(identity, layout);
    return result == gui_forms::SemanticTextResult::done ? YES : NO;
}

- (BOOL)readAccessibilityTreeKey:(MacAccessibilityTreeKey&)key {
    if (!_model) return NO;
    key = read_accessibility_tree_key(*_model);
    return YES;
}

- (BOOL)selectSemanticText:(std::uint64_t)control selection:(SemanticTextSelection)selection {
    if (!_model) return NO;
    const gui_forms::RuntimeId identity{control};
    const gui_forms::SemanticTextResult result = (*_model).set_semantic_text_selection(identity, selection);
    if (result != gui_forms::SemanticTextResult::done) return NO;
    [self collectDamage];
    return YES;
}

- (NSPoint)semanticPointForScreenPoint:(NSPoint)point {
    if (self.window == nil) return NSZeroPoint;
    const NSRect on_screen = NSMakeRect(point.x, point.y, 0.0, 0.0);
    const NSRect in_window = [self.window convertRectFromScreen:on_screen];
    return [self convertPoint:in_window.origin fromView:nil];
}

// The element of the current semantic tree with this stable identity, or
// nil; read by the tab-to-panel relation.
- (id)semanticAccessibilityElementForStableId:(NSString*)stableId {
    if (stableId == nil || _semanticAccessibilityElementsByStableId == nil) return nil;
    return _semanticAccessibilityElementsByStableId[stableId];
}

- (BOOL)performSemanticAction:(SemanticAction)action
                      control:(std::uint64_t)control
                 virtualChild:(const std::string&)virtualChild
                     stableId:(const std::string&)stableId
                        value:(NSString*)value {
    if (!_model) return NO;
    const std::string actionValue = value == nil ? std::string{} : utf8_string(value);
    if (action == SemanticAction::focus) {
        [self.window makeFirstResponder:self];
    }
    bool handled = false;
    if (control == 0U) {
        handled = (*_model).perform_semantic_action(stableId, action, actionValue);
    } else {
        const gui_forms::RuntimeId identity{control};
        handled = (*_model).perform_semantic_action(identity, virtualChild, action, actionValue);
    }
    if (handled) {
        [self collectDamage];
        NSAccessibilityPostNotification(self, NSAccessibilityValueChangedNotification);
    }
    return handled ? YES : NO;
}

- (void)viewDidMoveToWindow {
    gui_forms::detail::call_failing_fast("macos.view.viewDidMoveToWindow", &run_view_view_did_move_to_window, self);
}

- (void)answerViewDidMoveToWindow {
    [super viewDidMoveToWindow];
    [self notifyScaleChanged];
    [self notifyDisplaysChanged];
    [self collectDamage];
    [self startDisplayLinkIfNeeded];
}

- (void)viewWillMoveToWindow:(NSWindow*)newWindow {
    gui_forms::detail::call_failing_fast(
        "macos.view.viewWillMoveToWindow:", &run_view_view_will_move_to_window, self, newWindow);
}

- (void)answerViewWillMoveToWindow:(NSWindow*)newWindow {
    if (newWindow == nil && _wakeSource != nil) {
        dispatch_source_set_timer(_wakeSource, DISPATCH_TIME_FOREVER,
                                  DISPATCH_TIME_FOREVER, 0);
    }
    if (newWindow == nil) {
        [_displayLink invalidate];
        _displayLink = nil;
        [self stopInvalidationOverlayTimer];
    }
    [super viewWillMoveToWindow:newWindow];
}

- (void)dealloc {
    gui_forms::detail::call_failing_fast("macos.view.dealloc", &run_view_dealloc, self);
}

- (void)answerDealloc {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
    [self stopDisplayLink];
    [_displayLink invalidate];
    _displayLink = nil;
    [self stopInvalidationOverlayTimer];
    if (_wakeSource != nil) {
        dispatch_source_cancel(_wakeSource);
        _wakeSource = nil;
    }
    if (_presentColorSpace != nullptr) {
        CGColorSpaceRelease(_presentColorSpace);
        _presentColorSpace = nullptr;
    }
    if (_hostSession != nullptr) {
        (*_hostSession).shutdown();
        _hostSession.reset();
    }
    if (_hostServices != nullptr) {
        (*_hostServices).shutdown();
        _hostServices.reset();
    }
    if (_model != nullptr) {
        (*_model).set_text_metrics_provider(nullptr);
        (*_model).set_framebuffer_painter(nullptr);
    }
    _model.reset();
}

- (HostDialogResult)showHostDialog:(const HostDialogRequest&)request {
    if (_hostServices == nullptr) {
        return {{HostServiceError::after_shutdown}, request.request_id,
                HostPathDialogResult{}};
    }
    return (*_hostServices).show_dialog(request);
}

- (HostClipboardTextResult)readHostClipboard {
    if (_hostServices == nullptr) {
        return {{HostServiceError::after_shutdown}, {}, 0, false};
    }
    return (*_hostServices).read_clipboard_text();
}

- (HostServiceStatus)writeHostClipboard:(std::string_view)text {
    if (_hostServices == nullptr) return {HostServiceError::after_shutdown};
    return (*_hostServices).write_clipboard_text(text);
}

- (void)setFrameSize:(NSSize)newSize {
    gui_forms::detail::call_failing_fast("macos.view.setFrameSize:", &run_view_set_frame_size, self, newSize);
}

- (void)answerSetFrameSize:(NSSize)newSize {
    [super setFrameSize:newSize];
    if (_hostSession) {
        static_cast<void>([self dispatchHostPayload:
            HostResizeEvent{GFSize{newSize.width, newSize.height}}
                                   timestampNanoseconds:host_now_nanoseconds()]);
        [self collectDamage];
    }
}

- (void)updateTrackingAreas {
    gui_forms::detail::call_failing_fast("macos.view.updateTrackingAreas", &run_view_update_tracking_areas, self);
}

- (void)answerUpdateTrackingAreas {
    if (_trackingArea != nil) {
        [self removeTrackingArea:_trackingArea];
    }
    const NSTrackingAreaOptions options = NSTrackingMouseEnteredAndExited |
        NSTrackingMouseMoved | NSTrackingCursorUpdate | NSTrackingActiveInKeyWindow | NSTrackingInVisibleRect;
    _trackingArea = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                                 options:options
                                                   owner:self
                                                userInfo:nil];
    [self addTrackingArea:_trackingArea];
    [super updateTrackingAreas];
}

// Takes the scroll copies the model recorded with the damage just taken.
// The damage this view still holds moves with each copy's pixels; the copy's
// clip is asked of AppKit, which copies the raster there after the draw.
- (void)takeScrollCopies {
    _takenCopies.clear();
    (*_model).take_scroll_copies(_takenCopies);
    for (const gui_forms::ScrollCopy& copy : _takenCopies) {
        if (!_pendingDamage.empty()) {
            _pendingDamage = gui_forms::damage_after_scroll_copy(_pendingDamage, copy.clip, copy.offset);
        }
        _pendingCopies.push_back(copy);
        [self setNeedsDisplayInRect:NSMakeRect(copy.clip.x, copy.clip.y, copy.clip.width, copy.clip.height)];
    }
    _takenCopies.clear();
}

- (void)collectDamage {
    std::optional<MacCpuScope> cpuScope{};
    if (_cpuAttribution.enabled) cpuScope.emplace(_cpuAttribution, 2U);
    ++_damageCollectionCount;
    // Housekeeping: nothing is caught. The native entries that call it
    // (the wake, the display link, posted work, drawing) are the fail-fast
    // boundary.
    if (!_model) {
        return;
    }
    const gui_forms::FrameTime pollTime = std::chrono::steady_clock::now();
    const gui_forms::FramePollResult pollResult = (*_model).poll_frame_schedule(pollTime);
    if (_causalTrace && (*_causalTrace).enabled) {
        (*_causalTrace).record_poll(pollTime, pollResult.deadlines_fired, pollResult.next_wake);
    }
    const std::uint64_t semanticGeneration = (*_model).semantic_generation();
    if (semanticGeneration != _lastSemanticGeneration) {
        _lastSemanticGeneration = semanticGeneration;
        _semanticAccessibilityChildren = nil;
        _accessibilityCacheKey = MacAccessibilityTreeKey{};
        // Republishing posts a changed live region's announcement, and
        // a text node's changed value or selection, now.
        if (_publishesLiveRegion == YES || _publishesTextRanges == YES) {
            static_cast<void>([self accessibilityChildren]);
        }
        NSAccessibilityPostNotification(
            self, NSAccessibilityLayoutChangedNotification);
    }
    DamageRegion damage = (*_model).take_damage();
    const double scale =
        self.window == nil ? 1.0 : self.window.backingScaleFactor;
    [self takeScrollCopies];
    for (GFRect rect : damage.rectangles()) {
        rect = gui_forms::detail::align_damage_outward(rect, scale);
        _pendingDamage.add(rect);
        [self setNeedsDisplayInRect:
            NSMakeRect(rect.x, rect.y, rect.width, rect.height)];
    }
    [self startDisplayLinkIfNeeded];
    [self armWakeTimer];
}

- (void)startDisplayLinkIfNeeded {
    if (!_model || self.window == nil || _hostOccluded == YES ||
        !(*_model).has_live_surface_presentations()) return;
    if (_displayLink == nil) {
        GUIFormsDisplayTarget* target = [[GUIFormsDisplayTarget alloc] initWithView:self];
        _displayLink = [self displayLinkWithTarget:target selector:@selector(tick:)];
        [_displayLink addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes];
    }
    (*_model).set_live_surface_idle_waiting(false);
    _displayLink.paused = NO;
}

- (void)stopDisplayLink {
    _displayLink.paused = YES;
    if (_model) (*_model).set_live_surface_idle_waiting(false);
}

- (void)setDisplayTraceEnabled:(BOOL)enabled {
    _displayTrace.reset();
    if (enabled) _displayTrace = std::make_unique<MacDisplayLinkTrace>();
}

- (MacDisplayLinkTrace)displayTraceSnapshot {
    if (_displayTrace) return *_displayTrace;
    return MacDisplayLinkTrace{};
}

// The display timing of the display link's last tick, in FrameClock time:
// its vertical blank and refresh period. Empty (source none) before the
// link first ticked or while it has no link.
- (gui_forms::LiveDisplayTiming)liveDisplayTiming {
    gui_forms::LiveDisplayTiming timing{};
    if (_displayLink == nil) return timing;
    const CFTimeInterval vblank = _displayLink.timestamp;
    const CFTimeInterval period = _displayLink.duration;
    if (vblank <= 0.0 || period <= 0.0) return timing;
    // CADisplayLink times are CACurrentMediaTime seconds; carry the age of
    // the vertical blank over to the steady clock.
    const CFTimeInterval age = std::max(0.0, CACurrentMediaTime() - vblank);
    const gui_forms::FrameTime now = gui_forms::FrameClock::now();
    timing.refresh_period = std::chrono::duration_cast<gui_forms::FrameInterval>(
        std::chrono::duration<double>(period));
    timing.last_vblank = now - std::chrono::duration_cast<gui_forms::FrameInterval>(
        std::chrono::duration<double>(age));
    timing.source = gui_forms::LiveTimingSource::display_phased;
    return timing;
}

// Reports each surface this pass presented to its LiveSurface, once per
// surface (its fragments share one frame), with the generation shown. Reads
// _pendingLivePresentations, _liveFrameSources, _liveRegions and
// _liveFrameGenerations as the pass left them.
- (void)reportLivePresentations {
    const gui_forms::LiveDisplayTiming timing = [self liveDisplayTiming];
    for (std::size_t index = 0; index < _pendingLivePresentations.size(); ++index) {
        if (index >= _liveRegions.size() || index >= _liveFrameSources.size()) break;
        if (_liveRegions[index].empty() || !_pendingLivePresentations[index].surface) continue;
        const std::size_t source = _liveFrameSources[index];
        if (source >= _liveFrameGenerations.size() || _liveFrameGenerations[source] == 0U) continue;
        bool first = true;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (_liveFrameSources[earlier] == source && !_liveRegions[earlier].empty()) first = false;
        }
        if (!first) continue;
        (*_pendingLivePresentations[index].surface).notify_presented(_liveFrameGenerations[source], timing);
    }
}

// The invalidation overlay's fade (invalidation_overlay.hpp): while outlines
// show, every tick asks AppKit to draw the areas they cover now and those
// they covered at the last tick. With nothing owed, such a draw is a copy of
// the raster (copy_only_draw), composited at present time, so the overlay
// causes no paint of its own; a faded outline leaves the raster's pixels.
// The timer stops once nothing shows. UI thread.
- (void)armInvalidationOverlayTimer {
    if (_overlayTimer != nil || self.window == nil) return;
    GUIFormsOverlayTimerTarget* proxy = [[GUIFormsOverlayTimerTarget alloc] initWithView:self];
    _overlayTimer = [NSTimer timerWithTimeInterval:(1.0 / 30.0)
                                            target:proxy
                                          selector:@selector(tick:)
                                          userInfo:nil
                                           repeats:YES];
    [[NSRunLoop currentRunLoop] addTimer:_overlayTimer forMode:NSRunLoopCommonModes];
}

- (void)stopInvalidationOverlayTimer {
    if (_overlayTimer == nil) return;
    [_overlayTimer invalidate];
    _overlayTimer = nil;
}

- (void)invalidationOverlayTick {
    gui_forms::detail::call_failing_fast(
        "macos.view.invalidationOverlayTick", &run_view_invalidation_overlay_tick, self);
}

- (void)answerInvalidationOverlayTick {
    if (!_model) {
        [self stopInvalidationOverlayTimer];
        return;
    }
    const double scale = self.window == nil ? 1.0 : self.window.backingScaleFactor;
    const double width = std::max(0.0, self.bounds.size.width) * scale;
    const double height = std::max(0.0, self.bounds.size.height) * scale;
    _overlayRegions.clear();
    gui_forms::InvalidationOverlay* const overlay = (*_model).invalidation_overlay();
    if (overlay != nullptr) {
        (*overlay).expire(gui_forms::InvalidationClock::now());
        (*overlay).fill_regions(scale, static_cast<std::uint32_t>(std::ceil(width)),
                                static_cast<std::uint32_t>(std::ceil(height)), _overlayRegions);
    }
    for (const gui_forms::DevicePixelRect& region : _overlayRegions) {
        [self setNeedsDisplayInRect:NSMakeRect(region.left / scale, region.top / scale,
                                               (region.right - region.left) / scale,
                                               (region.bottom - region.top) / scale)];
    }
    for (const gui_forms::DevicePixelRect& region : _overlayDrawn) {
        [self setNeedsDisplayInRect:NSMakeRect(region.left / scale, region.top / scale,
                                               (region.right - region.left) / scale,
                                               (region.bottom - region.top) / scale)];
    }
    _overlayDrawn.swap(_overlayRegions);
    if (_overlayDrawn.empty()) [self stopInvalidationOverlayTimer];
}

- (void)displayLinkTick {
    gui_forms::detail::call_failing_fast("macos.display_link", &run_display_link_tick, self);
}

- (void)answerDisplayLinkTick {
    if (_displayTrace) {
        MacDisplayLinkTrace& trace = *_displayTrace;
        if (trace.count < trace.timestamps.size()) {
            const std::chrono::steady_clock::duration time =
                std::chrono::steady_clock::now().time_since_epoch();
            const std::chrono::nanoseconds nanoseconds =
                std::chrono::duration_cast<std::chrono::nanoseconds>(time);
            trace.timestamps[trace.count] = static_cast<std::uint64_t>(nanoseconds.count());
            ++trace.count;
        } else trace.overflow = true;
    }
    ++_displayTickCount;
    if (!_model || self.window == nil || _hostOccluded == YES) {
        [self stopDisplayLink];
        return;
    }
    // A failed batch needs a fresh composition, including unchanged surfaces.
    // Reusing cached clips could cover a newly opened overlay or hidden control.
    const bool retry = !_pendingLivePresentations.empty();
    bool publicationChanged = false;
    std::vector<LiveSurfacePresentation> updates =
        (*_model).take_live_surface_presentations(retry, &publicationChanged);
    _pendingLivePresentations = std::move(updates);
    if (!(*_model).has_live_surface_presentations()) {
        [self stopDisplayLink];
        return;
    }
    if (_pendingLivePresentations.empty()) {
        // Publication posts CollectDamageWake, which restarts this paused link.
        // Arm after pausing, then recheck generations to avoid a lost wake.
        [self stopDisplayLink];
        (*_model).set_live_surface_idle_waiting(true);
        return;
    }
    const double scale = self.window.backingScaleFactor;
    // When the raster still holds the previous presentation, ask AppKit to
    // redraw only where the surface actually changed. Redrawing the whole
    // clip left every overlay inside it as uncovered damage, so a floating
    // control was retained-painted on each publication however far away
    // the change was. drawRetainedRect: re-requests any part of the copied
    // region the flush did not cover, so a narrower request never leaves
    // stale pixels presented.
    const bool rasterReusable = _rasterHasContent == YES && _liveRegionLost == NO &&
        (*_model).paint_lease_snapshot().state == gui_forms::PaintLeaseState::clean;
    for (const LiveSurfacePresentation& update : _pendingLivePresentations) {
        GFRect request = update.clip;
        if (rasterReusable && update.damage_limited && update.surface) {
            const gui_forms::LiveSurfaceFrame latest = (*update.surface).acquire_latest();
            if (latest && latest.opaque() && !latest.damage().empty()) {
                const GFRect changed = gui_forms::detail::live_damage_in_window(
                    latest.damage(), update.destination, latest.width(), latest.height(), scale);
                const GFRect narrowed = GFRect::intersection(update.clip, changed);
                if (!narrowed.empty()) request = narrowed;
            }
        }
        const GFRect aligned = gui_forms::detail::align_damage_outward(request, scale);
        [self setNeedsDisplayInRect:
            NSMakeRect(aligned.x, aligned.y, aligned.width, aligned.height)];
    }
    // Where a new frame changed pixels beneath an overlay, the model now
    // holds that share as damage (take_live_surface_presentations). Take
    // it into this flush so the overlay is retained-painted over the new
    // frame together with the live copy, not one wake later.
    const DamageRegion beneathOverlays = (*_model).take_damage();
    [self takeScrollCopies];
    for (GFRect rect : beneathOverlays.rectangles()) {
        rect = gui_forms::detail::align_damage_outward(rect, scale);
        _pendingDamage.add(rect);
        [self setNeedsDisplayInRect:
            NSMakeRect(rect.x, rect.y, rect.width, rect.height)];
    }
    // AppKit calls on the main run loop, following this view's screen. Keep
    // its default variable refresh policy; sample only the newest frame.
    [self displayIfNeeded];
    if (!publicationChanged && _pendingLivePresentations.empty()) {
        // Unchanged surfaces may still need one composition for an overlay
        // edit. Once presented, a static overlay must not keep us ticking.
        [self stopDisplayLink];
        (*_model).set_live_surface_idle_waiting(true);
    }
}

- (void)drainPostedWork {
    gui_forms::detail::call_failing_fast("macos.posted_work", &run_posted_work, self);
}

- (void)answerDrainPostedWork {
    if (!_model) return;
    static_cast<void>((*_model).drain_posted_work());
    [self collectDamage];
}

- (void)armWakeTimer {
    if (!_model || _wakeSource == nil) {
        return;
    }
    const std::optional<gui_forms::FrameTime> wake = (*_model).next_wake();
    if (!wake) {
        if (_causalTrace && (*_causalTrace).enabled) {
            (*_causalTrace).record_arm(gui_forms::FrameClock::now(), wake, 0);
        }
        dispatch_source_set_timer(_wakeSource, DISPATCH_TIME_FOREVER,
                                  DISPATCH_TIME_FOREVER, 0);
        return;
    }
    const std::chrono::steady_clock::time_point now =
        std::chrono::steady_clock::now();
    // Nanosecond precision, never rounded down (audit N107): a deadline
    // less than a millisecond away used to fire early and be armed again.
    // A deadline already past fires at once.
    const std::chrono::nanoseconds remaining = std::chrono::ceil<std::chrono::nanoseconds>(*wake - now);
    const std::chrono::nanoseconds::rep nanoseconds = std::max<std::chrono::nanoseconds::rep>(remaining.count(), 0);
    dispatch_source_set_timer(
        _wakeSource,
        dispatch_time(DISPATCH_TIME_NOW, static_cast<std::int64_t>(nanoseconds)),
        DISPATCH_TIME_FOREVER,
        static_cast<std::uint64_t>(std::chrono::microseconds(250).count() * 1000));
    if (_causalTrace && (*_causalTrace).enabled) {
        (*_causalTrace).record_arm(now, wake, static_cast<std::int64_t>(nanoseconds));
    }
}

- (void)scheduledWake {
    std::optional<MacCausalWakeScope> causalScope{};
    if (_causalTrace && (*_causalTrace).enabled) causalScope.emplace(*_causalTrace);
    ++_scheduledWakeCount;
    // A libdispatch source's handler: the fail-fast boundary. On the 64-bit
    // runtime it catches an Objective-C exception as well as a C++ one.
    gui_forms::detail::call_failing_fast("macos.scheduled_wake", &run_collect_damage, self);
}


- (BOOL)acceptsWindowRequest {
    if (_hostSession == nullptr) return NO;
    const gui_forms::HostLifecyclePhase phase = (*_hostSession).snapshot().phase;
    const bool live = phase == gui_forms::HostLifecyclePhase::attached ||
                      phase == gui_forms::HostLifecyclePhase::close_authorized;
    const BOOL result = live ? YES : NO;
    return result;
}
- (void)prepareForShutdown {
    [self stopDisplayLink];
    // No compositor report reaches a session that is shutting down.
    [NSWorkspace.sharedWorkspace.notificationCenter removeObserver:self];
    [[NSNotificationCenter defaultCenter] removeObserver:self name:NSSystemColorsDidChangeNotification object:nil];
    if (_wakeSource != nil) {
        dispatch_source_set_timer(_wakeSource, DISPATCH_TIME_FOREVER,
                                  DISPATCH_TIME_FOREVER, 0);
    }
    if (_model) {
        (*_model).set_live_surface_idle_wake_handler({});
        (*_model).set_paint_wake_handler({});
    }
    // The session's shutdown releases capture while the dispatcher still
    // accepts the work controls post about it, then shuts the dispatcher
    // down; without a session the dispatcher is shut down here.
    if (_hostSession) {
        static_cast<void>([self dispatchHostPayload:HostShutdownEvent{}
                                   timestampNanoseconds:host_now_nanoseconds()]);
    }
    if (_model) (*_model).shutdown_dispatcher();
}

- (DragEvent)dragEventFor:(id<NSDraggingInfo>)sender action:(DragAction)action {
    DragEvent event;
    event.action = action;
    event.session_id = std::max<std::uint64_t>(
        1, static_cast<std::uint64_t>(sender.draggingSequenceNumber));
    const NSPoint point = [self convertPoint:sender.draggingLocation fromView:nil];
    event.position = {point.x, point.y};
    event.allowed_effects = drag_effects_for(sender.draggingSourceOperationMask);
    if (action != DragAction::leave) {
        event.items = drag_items_for(sender.draggingPasteboard);
    }
    return event;
}

- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.view.draggingEntered:", &run_view_dragging_entered, self, sender);
}

- (NSDragOperation)answerDraggingEntered:(id<NSDraggingInfo>)sender {
    DragEvent event = [self dragEventFor:sender action:DragAction::enter];
    const HostDispatchResult result = [self dispatchHostPayload:std::move(event)
        timestampNanoseconds:host_now_nanoseconds()];
    [self collectDamage];
    return native_drag_operation(result.drag_effect);
}

- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.view.draggingUpdated:", &run_view_dragging_updated, self, sender);
}

- (NSDragOperation)answerDraggingUpdated:(id<NSDraggingInfo>)sender {
    DragEvent event = [self dragEventFor:sender action:DragAction::over];
    const HostDispatchResult result = [self dispatchHostPayload:std::move(event)
        timestampNanoseconds:host_now_nanoseconds()];
    [self collectDamage];
    return native_drag_operation(result.drag_effect);
}

- (void)draggingExited:(id<NSDraggingInfo>)sender {
    gui_forms::detail::call_failing_fast("macos.view.draggingExited:", &run_view_dragging_exited, self, sender);
}

- (void)answerDraggingExited:(id<NSDraggingInfo>)sender {
    DragEvent event = [self dragEventFor:sender action:DragAction::leave];
    static_cast<void>([self dispatchHostPayload:std::move(event)
        timestampNanoseconds:host_now_nanoseconds()]);
    [self collectDamage];
}

- (BOOL)prepareForDragOperation:(id<NSDraggingInfo>)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.view.prepareForDragOperation:", &run_view_prepare_for_drag_operation, self, sender);
}

- (BOOL)answerPrepareForDragOperation:(id<NSDraggingInfo>)sender {
    return _hostSession != nullptr;
}

- (BOOL)performDragOperation:(id<NSDraggingInfo>)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.view.performDragOperation:", &run_view_perform_drag_operation, self, sender);
}

- (BOOL)answerPerformDragOperation:(id<NSDraggingInfo>)sender {
    DragEvent event = [self dragEventFor:sender action:DragAction::drop];
    const HostDispatchResult result = [self dispatchHostPayload:std::move(event)
        timestampNanoseconds:host_now_nanoseconds()];
    [self collectDamage];
    return result.accepted() && result.handled;
}

- (void)drawRect:(NSRect)dirtyRect {
    ++_nativeDrawCount;
    gui_forms::detail::call_failing_fast("macos.draw", &run_draw_retained, self, dirtyRect);
}

- (void)drawRetainedRect:(NSRect)dirtyRect {
    if (!_model) {
        return;
    }
    std::optional<MacCpuScope> cpuScope{};
    if (_cpuAttribution.enabled) cpuScope.emplace(_cpuAttribution, 0U);
    const std::chrono::steady_clock::time_point started =
        std::chrono::steady_clock::now();
    if (_causalTrace && (*_causalTrace).enabled) (*_causalTrace).record_draw_entry(started);
    const double scale = self.window == nil ? 1.0 : self.window.backingScaleFactor;
    const GFSize logicalSize{self.bounds.size.width, self.bounds.size.height};
    _lastNativeDirtyArea = std::max(0.0, dirtyRect.size.width) *
        std::max(0.0, dirtyRect.size.height);
    _lastFrameDamageBoundsArea = 0.0;
    _lastCGDestinationArea = 0.0;
    _lastCGClipBoundsArea = 0.0;
    _lastCGSourceBytes = 0U;
#if !defined(GUI_FORMS_PREPARED_TEXT)
    // At an unchanged scale the raster keeps its pixels where the old and
    // new extents overlap, and the window damaged what the resize exposed
    // (Window::resize); otherwise it starts clear and is painted whole.
    if (_raster.resize(logicalSize, scale) && !_raster.carried_on_resize()) {
        _rasterHasContent = NO;
        _pendingDamage.add(GFRect{0.0, 0.0, logicalSize.width, logicalSize.height});
    }
#endif
    const GFRect nativeDamage{dirtyRect.origin.x, dirtyRect.origin.y,
                              dirtyRect.size.width, dirtyRect.size.height};
    const gui_forms::PaintLeaseSnapshot drawLease = (*_model).paint_lease_snapshot();
    gui_forms::detail::FrontReuse reuse{};
    reuse.raster_has_content = _rasterHasContent == YES;
#if defined(GUI_FORMS_PREPARED_TEXT)
    reuse.front_matches_extent = _raster.prepared_front_matches(logicalSize, scale);
    const PaintReceipt previousReceipt = _raster.prepared_front_receipt();
#else
    // The ordinary raster was resized to the view above (keeping the pixels
    // it carried, if any).
    reuse.front_matches_extent = true;
#endif
    reuse.front_surface_epoch = _frontSurfaceEpoch;
    reuse.surface_epoch = drawLease.surface_epoch;
    reuse.lease_clean = drawLease.state == gui_forms::PaintLeaseState::clean;
    reuse.live_region_lost = _liveRegionLost == YES;
    // The raster still holds the last presentation, at this size and scale,
    // in the model's current surface epoch.
    const bool frontHeld = gui_forms::detail::front_held(reuse);
    const bool reusableRaster = frontHeld && _hostOccluded == NO;
    // Scroll copies move pixels of a raster that still holds the last
    // presentation; otherwise their clips are repainted.
    if (!reusableRaster && !_pendingCopies.empty()) {
        for (const gui_forms::ScrollCopy& copy : _pendingCopies) _pendingDamage.add(copy.clip);
        _pendingCopies.clear();
    }
    const bool copiesPending = !_pendingCopies.empty();
    // While hidden the model declines to paint. AppKit's rectangles then ask
    // for a copy (an exposure or a snapshot); kept as retained damage, they
    // would be repainted by the first visible frame. Live frames still
    // compose while hidden, so a pending one keeps the ordinary path.
    const bool occludedCopy = frontHeld && _hostOccluded == YES && _pendingLivePresentations.empty();
    // Copying only a presentation region needs a raster that still holds the
    // previous presentation of that placement.
    // A scroll copy can move a live surface's previous pixels: copy whole clips.
    const bool regionsUsable = reusableRaster && _liveRegionLost == NO && !copiesPending;
    _liveFrames.clear();
    _opaqueLiveClips.clear();
    _liveFrameSources.clear();
    _liveRegions.clear();
    _liveFrames.reserve(_pendingLivePresentations.size());
    _opaqueLiveClips.reserve(_pendingLivePresentations.size());
    _liveFrameSources.reserve(_pendingLivePresentations.size());
    _liveRegions.reserve(_pendingLivePresentations.size());
    bool regionsTaken{false};
    bool narrowedAny{false};
    for (std::size_t index = 0; index < _pendingLivePresentations.size(); ++index) {
        const LiveSurfacePresentation& update = _pendingLivePresentations[index];
        // Overlay fragments of one surface share one frame and its one region.
        std::size_t source = index;
        for (std::size_t earlier = 0; earlier < index; ++earlier) {
            if (_pendingLivePresentations[earlier].surface == update.surface) {
                source = _liveFrameSources[earlier];
                break;
            }
        }
        gui_forms::LiveSurfaceFrame frame{};
        if (source == index && update.surface) {
            // Takes the frame and clears its invalid region in one step.
            frame = (*update.surface).acquire_for_presentation((__bridge const void*)self);
            if (frame) regionsTaken = true;
        }
        const gui_forms::LiveSurfaceFrame& drawn = source == index ? frame : _liveFrames[source];
        GFRect region = update.clip;
        if (regionsUsable && update.damage_limited && drawn && drawn.opaque()) {
            // Outside the region the frame equals what this raster holds.
            const GFRect changed = gui_forms::detail::live_damage_in_window(
                drawn.damage(), update.destination, drawn.width(), drawn.height(), scale);
            region = GFRect::intersection(update.clip, changed);
            narrowedAny = true;
            // The display link requested the damage it saw; a publication that
            // landed since can reach outside this flush. The raster receives
            // the copy now, so request the remainder for the next flush.
            if (!region.empty() && GFRect::intersection(nativeDamage, region) != region) {
                const GFRect remainder = gui_forms::detail::align_damage_outward(region, scale);
                [self setNeedsDisplayInRect:
                    NSMakeRect(remainder.x, remainder.y, remainder.width, remainder.height)];
            }
        }
        if (drawn && drawn.opaque()) {
            const GFRect covered = gui_forms::detail::opaque_live_clip(
                GFRect::intersection(update.clip, update.destination), scale);
            if (!covered.empty()) _opaqueLiveClips.push_back(covered);
        }
        _liveFrameSources.push_back(source);
        _liveRegions.push_back(region);
        _liveFrames.push_back(std::move(frame));
    }
    // Use the rectangles AppKit is drawing, not their bounding box. Live
    // fragments around an overlay leave only the overlay's share to retained
    // painting; a bounding box would repaint the whole surface beneath it.
    const NSRect* drawnRects = nullptr;
    NSInteger drawnCount = 0;
    [self getRectsBeingDrawn:&drawnRects count:&drawnCount];
    _uncoveredDamage.clear();
    for (NSInteger index = 0; index < drawnCount && !occludedCopy; ++index) {
        const NSRect rect = drawnRects[index];
        const GFRect dirty{rect.origin.x, rect.origin.y, rect.size.width, rect.size.height};
        if (reusableRaster) {
            gui_forms::detail::append_uncovered_live_damage(dirty, scale, _opaqueLiveClips, _uncoveredDamage);
        } else {
            _uncoveredDamage.push_back(dirty);
        }
    }
    if (drawnCount == 0 && !occludedCopy) _uncoveredDamage.push_back(nativeDamage);
    // Nothing owed and no live frame: AppKit's request is a copy of the raster
    // as it stands, and this draw prepares and paints nothing. A resize, a
    // scale change, a reallocated backing or a discarded candidate fails the
    // predicate and repaints.
    const bool copyOnly = gui_forms::detail::copy_only_draw(
        reuse, !_pendingDamage.empty() || copiesPending, !_pendingLivePresentations.empty());
    if (copyOnly) _uncoveredDamage.clear();
    if (reusableRaster && !copyOnly) {
        // Paint the gathered set, not AppKit's coalesced request: outside the
        // model's damage (already in _pendingDamage) and the live surfaces,
        // the raster holds this frame's pixels, which AppKit copies unchanged.
        _liveDestinations.clear();
        for (const LiveSurfacePresentation& update : _pendingLivePresentations) {
            _liveDestinations.push_back(update.destination);
        }
        _liveExposure.clear();
        gui_forms::detail::append_within_live_destinations(_uncoveredDamage, _liveDestinations, _liveExposure);
        bool livePending{false};
        for (const GFRect region : _liveRegions) {
            if (!region.empty()) livePending = true;
        }
        // Live frames narrowed to nothing leave nothing to paint, and empty
        // frame damage would mean the whole raster (a prepared frame clears its
        // candidate). Keep AppKit's rectangles then, as the host did before.
        if (!_pendingDamage.empty() || !_liveExposure.empty() || livePending) {
            _uncoveredDamage.swap(_liveExposure);
        }
    }
    for (const GFRect rectangle : _uncoveredDamage) _pendingDamage.add(rectangle);
    bool liveOnly = _pendingDamage.empty();
    if (narrowedAny && !liveOnly) {
        // Retained painting can reach beneath live clips; copy those pixels too.
        const GFRect retainedBounds = _pendingDamage.bounds();
        for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
            const GFRect beneath = GFRect::intersection(
                _pendingLivePresentations[index].clip, retainedBounds);
            _liveRegions[index] = GFRect::united(_liveRegions[index], beneath);
        }
    }

#if defined(GUI_FORMS_PREPARED_TEXT)
    const bool imagesReady = _raster.synchronize_images((*_model).image_resources());
#else
    static_cast<void>(_raster.synchronize_images((*_model).image_resources()));
#endif
    DamageRegion frameDamage = _pendingDamage;
    for (const GFRect region : _liveRegions) frameDamage.add(region);
    if (narrowedAny && frameDamage.empty()) {
        // Nothing changed, but empty frame damage means the whole raster.
        for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
            _liveRegions[index] = _pendingLivePresentations[index].clip;
            frameDamage.add(_liveRegions[index]);
        }
        narrowedAny = false;
    }
    // A copy-only draw begins no frame: the front is presented as it stands,
    // with no candidate, receipt or acknowledgement.
    bool frameReady{!copyOnly};
#if defined(GUI_FORMS_PREPARED_TEXT)
    gui_forms::PreparedTextStatus admission = gui_forms::PreparedTextStatus::success;
    if (frameReady) {
        admission = imagesReady
            ? _raster.begin_prepared_frame(logicalSize, scale, frameDamage)
            : gui_forms::PreparedTextStatus::resource_failure;
    }
    frameReady = frameReady && admission == gui_forms::PreparedTextStatus::success;
    if (frameReady) {
        // Admission clears the candidate instead of copying the presented
        // front when damage spans the raster (resize, revocation, full live
        // damage). Then live surfaces draw complete clips and retained
        // painting fills what they leave uncovered. Keep that damage until
        // this exact candidate reaches native presentation.
        const GFRect extent{0.0, 0.0, logicalSize.width, logicalSize.height};
        if (frameDamage.bounds().contains(extent)) {
            for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
                _liveRegions[index] = _pendingLivePresentations[index].clip;
            }
            narrowedAny = false;
            _uncoveredDamage.clear();
            if (reusableRaster) {
                gui_forms::detail::append_uncovered_live_damage(extent, scale, _opaqueLiveClips, _uncoveredDamage);
            } else {
                _uncoveredDamage.push_back(extent);
            }
            for (const GFRect rectangle : _uncoveredDamage) _pendingDamage.add(rectangle);
            liveOnly = _pendingDamage.empty();
        }
    }
#else
    if (frameReady) _raster.begin_frame(frameDamage);
#endif
    // The scroll copies move the begun frame's pixels before its damage is
    // painted. A frame not begun repaints their clips later; a copy the
    // raster refuses (it cannot, after the model checked the alignment)
    // drops this frame and repaints the clips in the next.
    _appliedCopyClips.clear();
    if (!_pendingCopies.empty()) {
        bool applied = frameReady;
        for (const gui_forms::ScrollCopy& copy : _pendingCopies) {
            if (applied) applied = _raster.scroll_copy(copy.clip, copy.offset);
            _appliedCopyClips.push_back(copy.clip);
        }
        _pendingCopies.clear();
        if (!applied) {
            for (const GFRect clip : _appliedCopyClips) _pendingDamage.add(clip);
            if (frameReady) {
                _raster.end_frame();
                frameReady = false;
                for (const GFRect clip : _appliedCopyClips) {
                    [self setNeedsDisplayInRect:NSMakeRect(clip.x, clip.y, clip.width, clip.height)];
                }
            }
            _appliedCopyClips.clear();
        }
    }
    _lastFrameDamageBoundsArea = frameDamage.bounds().area();
    const std::chrono::steady_clock::time_point preparedAt =
        std::chrono::steady_clock::now();
    _rasterPreparePhase.record(started, preparedAt);
    std::optional<PaintReceipt> receipt{};
    if (frameReady && !_pendingDamage.empty()) {
        const std::chrono::steady_clock::time_point paintStarted =
            std::chrono::steady_clock::now();
        // Clip to the damaged rectangles, not their bounds: overlays in
        // opposite corners cost their own areas, not the strip between them.
        _raster.save();
        _raster.clip_damage(_pendingDamage);
        receipt = (*_model).paint(_raster, _pendingDamage.rectangles());
        _raster.restore();
        const std::chrono::steady_clock::time_point paintEnded =
            std::chrono::steady_clock::now();
        _retainedPaintPhase.record(paintStarted, paintEnded);
    }
    const std::chrono::steady_clock::time_point finishStarted =
        std::chrono::steady_clock::now();
    bool liveDrawsSucceeded{true};
    if (frameReady) {
        for (std::size_t index = 0; index < _pendingLivePresentations.size(); ++index) {
            const LiveSurfacePresentation& update = _pendingLivePresentations[index];
            // Reconfiguration can retire the last frame before a replacement
            // is published. Its clip granted no opaque coverage above.
            const gui_forms::LiveSurfaceFrame& frame = _liveFrames[_liveFrameSources[index]];
            if (!frame) continue;
            // Empty: this fragment's region holds no published change.
            if (_liveRegions[index].empty()) continue;
            _raster.save();
            _raster.clip_rect(_liveRegions[index]);
            liveDrawsSucceeded = _raster.draw_live_surface_frame(
                frame, update.destination, 1.0);
            _raster.restore();
            if (!liveDrawsSucceeded) break;
            (*_model).note_live_surface_presented(update, _liveRegions[index]);
        }
        _raster.end_frame();
    }
#if defined(GUI_FORMS_PREPARED_TEXT)
    bool committed{false};
    if (frameReady && liveDrawsSucceeded && (receipt || liveOnly)) {
        // Live-only pixels retain the same coherent retained-tree revision.
        // Do not manufacture or acknowledge another model paint transaction.
        const PaintReceipt publicationReceipt = receipt ? *receipt : previousReceipt;
        const gui_forms::PreparedTextStatus publication = _raster.commit_prepared_frame(publicationReceipt);
        committed = publication == gui_forms::PreparedTextStatus::success;
    }
    if (!committed) {
        _raster.abort_prepared_frame();
        receipt.reset();
    }
#endif
    // A frame that moved pixels and was not presented repaints the clips.
    if (!receipt) {
        for (const GFRect clip : _appliedCopyClips) _pendingDamage.add(clip);
    }
    _appliedCopyClips.clear();
    _liveFrameGenerations.clear();
    _liveFrameGenerations.reserve(_liveFrames.size());
    for (const gui_forms::LiveSurfaceFrame& frame : _liveFrames) {
        _liveFrameGenerations.push_back(frame ? frame.generation() : 0U);
    }
    _liveFrames.clear();
    const std::chrono::steady_clock::time_point finishEnded =
        std::chrono::steady_clock::now();
    _rasterFinishPhase.record(finishStarted, finishEnded);

    if (receipt) {
        _rasterHasContent = YES;
        _frontSurfaceEpoch = (*receipt).surface_epoch;
    }
    GFSize presentationSize = logicalSize;
#if defined(GUI_FORMS_PREPARED_TEXT)
    _rasterHasContent = _raster.prepared_front_receipt() ? YES : NO;
    if (_rasterHasContent == YES) {
        const double frontScale = _raster.prepared_front_scale();
        presentationSize = {
            static_cast<double>(_raster.pixel_width()) / frontScale,
            static_cast<double>(_raster.pixel_height()) / frontScale};
    }
#endif
    bool presented = false;
    std::uint64_t copyNanoseconds = 0U;
    const void* pixels = _raster.pixels();
    // The invalidation overlay at present time: the dirty rectangle's pixels
    // are copied into a scratch frame and the outlines composited there, and
    // that frame is drawn instead of the raster. AppKit clips the draw to the
    // dirty rectangles, so the scratch frame's pixels outside them are never
    // shown. The raster is never drawn on, so the overlay damages nothing.
    // Off, this is one relaxed load.
    gui_forms::InvalidationOverlay* const overlay = (*_model).invalidation_overlay();
    if (overlay != nullptr && pixels != nullptr && _rasterHasContent == YES && presentationSize.width > 0.0) {
        const std::size_t frameBytes = _raster.byte_size();
        const std::int32_t frameWidth = static_cast<std::int32_t>(_raster.pixel_width());
        const std::int32_t frameHeight = static_cast<std::int32_t>(_raster.pixel_height());
        const std::size_t rowBytes = _raster.row_bytes();
        const double frameScale = static_cast<double>(frameWidth) / presentationSize.width;
        if (_overlayScratch.size() != frameBytes) _overlayScratch.assign(frameBytes, std::byte{0});
        // The dirty rectangle in device pixels, outward (the view is flipped,
        // so its rows are the raster's).
        gui_forms::DevicePixelRect box{};
        box.left = std::clamp<std::int32_t>(
            static_cast<std::int32_t>(std::floor(dirtyRect.origin.x * frameScale)), 0, frameWidth);
        box.top = std::clamp<std::int32_t>(
            static_cast<std::int32_t>(std::floor(dirtyRect.origin.y * frameScale)), 0, frameHeight);
        box.right = std::clamp<std::int32_t>(
            static_cast<std::int32_t>(std::ceil((dirtyRect.origin.x + dirtyRect.size.width) * frameScale)),
            box.left, frameWidth);
        box.bottom = std::clamp<std::int32_t>(
            static_cast<std::int32_t>(std::ceil((dirtyRect.origin.y + dirtyRect.size.height) * frameScale)),
            box.top, frameHeight);
        const std::byte* const source = static_cast<const std::byte*>(pixels);
        const std::size_t rowOffset = static_cast<std::size_t>(box.left) * 4U;
        const std::size_t rowLength = static_cast<std::size_t>(box.right - box.left) * 4U;
        for (std::int32_t y = box.top; y < box.bottom; ++y) {
            const std::size_t offset = static_cast<std::size_t>(y) * rowBytes + rowOffset;
            std::memcpy(_overlayScratch.data() + offset, source + offset, rowLength);
        }
        const gui_forms::InvalidationClock::time_point now = gui_forms::InvalidationClock::now();
        (*overlay).expire(now);
        gui_forms::InvalidationOverlayTarget target{};
        target.pixels = std::span<std::byte>(_overlayScratch);
        target.width = static_cast<std::uint32_t>(frameWidth);
        target.height = static_cast<std::uint32_t>(frameHeight);
        target.row_bytes = rowBytes;
        (*overlay).compose(target, frameScale, now, box);
        pixels = _overlayScratch.data();
    }
    // Native exposure/snapshot callbacks can precede unocclusion. The model
    // intentionally declines painting then; the last complete raster is still
    // valid and must be copied into AppKit's newly supplied backing context.
    if (_rasterHasContent == YES && pixels != nullptr && liveDrawsSucceeded) {
        const std::chrono::steady_clock::time_point setupStarted =
            std::chrono::steady_clock::now();
        CGDataProviderRef provider = CGDataProviderCreateWithData(
            nullptr, pixels, _raster.byte_size(), nullptr);
        CGColorSpaceRef colorSpace = _presentColorSpace;
        // An opaque window presents opaque pixels, as the other hosts do: the
        // frame's alpha reaches the window server only while a non-opaque
        // backdrop is granted (stage T5).
        const bool windowOpaque = self.window == nil || self.window.opaque == YES;
        const CGImageAlphaInfo alphaInfo =
            windowOpaque ? kCGImageAlphaNoneSkipFirst : kCGImageAlphaPremultipliedFirst;
        // The raster is premultiplied BGRA8 (SK2): read as 32-bit
        // little-endian words with alpha first, the window server's own
        // order, so the copy into the backing store needs no swizzle.
        const CGBitmapInfo bitmapInfo = static_cast<CGBitmapInfo>(
            static_cast<CGBitmapInfo>(alphaInfo) |
            static_cast<CGBitmapInfo>(kCGBitmapByteOrder32Little));
        CGImageRef image = CGImageCreate(
            _raster.pixel_width(), _raster.pixel_height(), 8, 32,
            _raster.row_bytes(), colorSpace, bitmapInfo, provider,
            nullptr, false, kCGRenderingIntentDefault);
        const std::chrono::steady_clock::time_point setupEnded =
            std::chrono::steady_clock::now();
        _cgSetupPhase.record(setupStarted, setupEnded);
        if (image != nullptr) {
            CGContextRef context = NSGraphicsContext.currentContext.CGContext;
            const CGRect clipBounds = CGContextGetClipBoundingBox(context);
            _lastCGClipBoundsArea = std::max(0.0, clipBounds.size.width) *
                std::max(0.0, clipBounds.size.height);
            _lastCGDestinationArea = presentationSize.width * presentationSize.height;
            _lastCGSourceBytes = _raster.byte_size();
            const std::chrono::steady_clock::time_point drawStarted =
                std::chrono::steady_clock::now();
            CGContextSaveGState(context);
            CGContextSetBlendMode(context, kCGBlendModeCopy);
            CGContextTranslateCTM(context, 0.0, presentationSize.height);
            CGContextScaleCTM(context, 1.0, -1.0);
            CGContextDrawImage(context,
                               CGRectMake(0.0, 0.0, presentationSize.width, presentationSize.height),
                               image);
            CGContextRestoreGState(context);
            const std::chrono::steady_clock::time_point drawEnded =
                std::chrono::steady_clock::now();
            _cgDrawPhase.record(drawStarted, drawEnded);
            copyNanoseconds = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(drawEnded - drawStarted).count());
            presented = true;
        }
        const std::chrono::steady_clock::time_point releaseStarted =
            std::chrono::steady_clock::now();
        if (image != nullptr) CGImageRelease(image);
        CGDataProviderRelease(provider);
        const std::chrono::steady_clock::time_point releaseEnded =
            std::chrono::steady_clock::now();
        _cgReleasePhase.record(releaseStarted, releaseEnded);
    }
    const std::chrono::duration<long long, std::ratio<1, 1000000000>> elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - started);
#if defined(GUI_FORMS_PREPARED_TEXT)
    const bool livePresented = presented && committed;
#else
    const bool livePresented = presented;
#endif
    // Each surface shown learns the generation it showed and the display
    // timing of this pass (threaded sub-surfaces design note sections 7 and
    // 8), once per pass, before the presentations are released.
    if (livePresented) [self reportLivePresentations];
    // Outlines recorded by this draw may reach outside its dirty rectangles
    // (labels, the legend): the fade timer's first tick presents them.
    if ((overlay != nullptr && !(*overlay).empty()) || !_overlayDrawn.empty()) {
        [self armInvalidationOverlayTimer];
    }
    if (presented) {
#if defined(GUI_FORMS_PREPARED_TEXT)
        if (committed) _pendingLivePresentations.clear();
#else
        _pendingLivePresentations.clear();
#endif
    }
    // A taken region is validated only by a presented raster, as EndPaint
    // validates an update region. Otherwise the next pass copies everything.
    if (regionsTaken) _liveRegionLost = livePresented ? NO : YES;
    // What this flush copied to the screen (stage DP). AppKit clips the copy
    // to the rectangles being drawn; the live frames' regions are reported as
    // live and the rest as retained, and the duration is split between them
    // by area. A surface's clip arrives as fragments around overlays: each
    // surface frame is one live frame, its fragments its rectangles.
    gui_forms::PresentedArea retainedCopied;
    std::uint64_t liveFramesCopied = 0U;
    double liveArea = 0.0;
    double drawnArea = 0.0;
    const double devicePixelsPerPoint = scale * scale;
    if (presented) {
        for (NSInteger index = 0; index < drawnCount; ++index) {
            drawnArea += drawnRects[index].size.width * drawnRects[index].size.height;
        }
        if (drawnCount == 0) drawnArea = nativeDamage.width * nativeDamage.height;
        if (livePresented) {
            for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
                if (_liveRegions[index].empty()) continue;
                liveArea += _liveRegions[index].width * _liveRegions[index].height;
                bool firstOfFrame = true;
                for (std::size_t earlier = 0; earlier < index; ++earlier) {
                    if (_liveFrameSources[earlier] == _liveFrameSources[index] &&
                        !_liveRegions[earlier].empty()) {
                        firstOfFrame = false;
                    }
                }
                if (firstOfFrame) ++liveFramesCopied;
            }
        }
        const double retainedArea = std::max(0.0, drawnArea - liveArea);
        retainedCopied.device_pixels =
            static_cast<std::uint64_t>(std::llround(retainedArea * devicePixelsPerPoint));
        retainedCopied.rectangles =
            drawnCount > 0 ? static_cast<std::uint64_t>(drawnCount) : 1U;
        // The window records the copy time with the retained report; a
        // live-only flush reports it with its first live frame.
        if (receipt) retainedCopied.copy_nanoseconds = copyNanoseconds;
    }
    std::uint64_t unreportedCopy = receipt ? 0U : copyNanoseconds;
    const std::uint64_t elapsedNanoseconds = static_cast<std::uint64_t>(elapsed.count());
    const double copiedArea = std::max(drawnArea, liveArea);
    const std::uint64_t liveElapsed = copiedArea <= 0.0 ? 0U :
        static_cast<std::uint64_t>(static_cast<double>(elapsedNanoseconds) * liveArea / copiedArea);
    if (presented) {
        if (receipt &&
            (*_model).notify_presented(*receipt, elapsedNanoseconds - liveElapsed,
                                       retainedCopied)) {
            _pendingDamage.clear();
        }
    }
    if (liveFramesCopied != 0U) {
        const std::uint64_t liveDuration =
            (receipt ? liveElapsed : elapsedNanoseconds) / liveFramesCopied;
        for (std::size_t index = 0; index < _liveRegions.size(); ++index) {
            // One report per surface frame, at its first fragment.
            if (_liveFrameSources[index] != index) continue;
            gui_forms::PresentedArea liveCopied;
            for (std::size_t fragment = index; fragment < _liveRegions.size(); ++fragment) {
                if (_liveFrameSources[fragment] != index || _liveRegions[fragment].empty()) continue;
                const GFRect region = _liveRegions[fragment];
                liveCopied.device_pixels += static_cast<std::uint64_t>(
                    std::llround(region.width * region.height * devicePixelsPerPoint));
                ++liveCopied.rectangles;
            }
            if (liveCopied.rectangles == 0U) continue;
            liveCopied.copy_nanoseconds = unreportedCopy;
            unreportedCopy = 0U;
            (*_model).notify_live_presented(liveDuration, liveCopied);
        }
    }
    [self completeFrameCaptureIfDue:dirtyRect presented:(presented && receipt) ? YES : NO];
    // Do not collect the next model invalidation from inside AppKit's paint
    // transaction. setNeedsDisplayInRect: may leave the view dirty without
    // enqueueing a second draw when invoked synchronously by drawRect:, which
    // lets animation callbacks advance while the last raster remains frozen.
    // The retained deadline source owns the next poll; arm it after presenting
    // and collect damage from its main-queue callback.
    [self armWakeTimer];
    if (_cpuAttribution.enabled) {
        const std::array<double, 4> extents{
            _lastNativeDirtyArea, _lastFrameDamageBoundsArea,
            _lastCGDestinationArea, _lastCGClipBoundsArea};
        const std::uint64_t wallNs = host_now_nanoseconds();
        _cpuAttribution.record_draw(wallNs, extents, _lastCGSourceBytes);
    }
    if (_causalTrace && (*_causalTrace).enabled) {
        (*_causalTrace).record_draw_exit(gui_forms::FrameClock::now());
    }
}

- (GFPoint)modelPointForEvent:(NSEvent*)event {
    const NSPoint point = [self convertPoint:event.locationInWindow fromView:nil];
    return GFPoint{point.x, point.y};
}

- (void)dispatchPointerEvent:(NSEvent*)event action:(PointerAction)action {
    PointerEvent input;
    input.action = action;
    input.button = button_for(event.buttonNumber);
    input.position = [self modelPointForEvent:event];
    input.modifiers = modifiers_for(event.modifierFlags);
    input.pointer_id = 1;
    input.click_count = static_cast<std::uint32_t>(
        std::max<NSInteger>(1, event.clickCount));
    const GFPoint position = input.position;
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self updateCursorAtPoint:position];
    [self collectDamage];
}

- (void)updateCursorAtPoint:(GFPoint)position {
    if (_hostServices != nullptr) {
        const gui_forms::Control::Ptr captured = (*_model).captured_control();
        const gui_forms::Control::Ptr target = captured ? captured : (*_model).hit_test(position);
        static_cast<void>((*_hostServices).set_custom_cursor(
            target ? (*target).effective_cursor_images() : gui_forms::CursorImagesPtr{},
            self.window.backingScaleFactor,
            target ? (*target).effective_cursor() : CursorKind::arrow));
    }
}

- (void)cursorUpdate:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.cursorUpdate:", &run_view_cursor_update, self, event);
}

- (void)answerCursorUpdate:(NSEvent*)event {
    [self updateCursorAtPoint:[self modelPointForEvent:event]];
}

- (void)mouseExited:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseExited:", &run_view_mouse_exited, self, event);
}

- (void)answerMouseExited:(NSEvent*)event {
    if (_model && (*_model).captured_control()) return;
    if (_hostServices != nullptr) {
        static_cast<void>((*_hostServices).set_cursor(CursorKind::arrow));
    }
}

- (void)mouseDown:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseDown:", &run_view_mouse_down, self, event);
}

- (void)answerMouseDown:(NSEvent*)event {
    [self.window makeFirstResponder:self];
    if ((event.modifierFlags & NSEventModifierFlagControl) != 0) {
        _controlClickInProgress = YES;
        static_cast<void>([self menuForEvent:event]);
        return;
    }
    if (event.buttonNumber == 0 && !_windowDragRegionIds.empty() && _model) {
        const GFPoint point = [self modelPointForEvent:event];
        const gui_forms::WindowChromeHit hit =
            gui_forms::resolve_window_chrome_hit(
                *_model, point, _windowDragRegionIds);
        if (hit.begins_native_drag()) {
            [self.window performWindowDragWithEvent:event];
            return;
        }
    }
    [self dispatchPointerEvent:event action:PointerAction::down];
}

- (void)mouseUp:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseUp:", &run_view_mouse_up, self, event);
}

- (void)answerMouseUp:(NSEvent*)event {
    if (_controlClickInProgress) {
        _controlClickInProgress = NO;
        return;
    }
    [self dispatchPointerEvent:event action:PointerAction::up];
}

- (void)rightMouseDown:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.rightMouseDown:", &run_view_right_mouse_down, self, event);
}

- (void)answerRightMouseDown:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::down];
    // NSView's own rightMouseDown: asks menuForEvent: for the menu, which
    // reports the context request (owner's mouse-event rule).
    [super rightMouseDown:event];
}

// AppKit's context-menu request: the right button's press, or a
// Control-click, reported to the window as a pointer HostContextRequestEvent.
// GUI.Forms shows its own menus, so AppKit is given none.
- (NSMenu*)menuForEvent:(NSEvent*)event {
    return gui_forms::detail::call_failing_fast("macos.view.menuForEvent:", &run_view_menu_for_event, self, event);
}

- (NSMenu*)answerMenuForEvent:(NSEvent*)event {
    if (!_model) return nil;
    HostContextRequestEvent request{};
    request.origin = ContextRequestOrigin::pointer;
    request.position = [self modelPointForEvent:event];
    static_cast<void>([self dispatchHostPayload:std::move(request)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self collectDamage];
    return nil;
}

- (BOOL)dispatchKeyboardContextRequest {
    if (!_model) return NO;
    HostContextRequestEvent request{};
    request.origin = ContextRequestOrigin::keyboard;
    const HostDispatchResult result = [self dispatchHostPayload:std::move(request)
                                           timestampNanoseconds:host_now_nanoseconds()];
    [self collectDamage];
    return result.handled;
}

- (void)rightMouseUp:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.rightMouseUp:", &run_view_right_mouse_up, self, event);
}

- (void)answerRightMouseUp:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::up];
}

- (void)otherMouseDown:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.otherMouseDown:", &run_view_other_mouse_down, self, event);
}

- (void)answerOtherMouseDown:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::down];
}

- (void)otherMouseUp:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.otherMouseUp:", &run_view_other_mouse_up, self, event);
}

- (void)answerOtherMouseUp:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::up];
}

- (void)mouseMoved:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseMoved:", &run_view_mouse_moved, self, event);
}

- (void)answerMouseMoved:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::move];
}

- (void)mouseDragged:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.mouseDragged:", &run_view_mouse_dragged, self, event);
}

- (void)answerMouseDragged:(NSEvent*)event {
    if (_controlClickInProgress) return;
    [self dispatchPointerEvent:event action:PointerAction::move];
}

- (void)rightMouseDragged:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.rightMouseDragged:", &run_view_right_mouse_dragged, self, event);
}

- (void)answerRightMouseDragged:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::move];
}

- (void)otherMouseDragged:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.otherMouseDragged:", &run_view_other_mouse_dragged, self, event);
}

- (void)answerOtherMouseDragged:(NSEvent*)event {
    [self dispatchPointerEvent:event action:PointerAction::move];
}

- (void)scrollWheel:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.scrollWheel:", &run_view_scroll_wheel, self, event);
}

- (void)answerScrollWheel:(NSEvent*)event {
    PointerEvent input;
    input.action = PointerAction::wheel;
    input.position = [self modelPointForEvent:event];
    // A trackpad or smooth-scrolling mouse reports precise deltas in points
    // (logical pixels), its momentum phase included, scrolled exactly; a
    // detented wheel reports lines, possibly fractional where AppKit
    // accelerates them. Every event is dispatched: none is coalesced.
    input.wheel_delta = GFPoint{event.scrollingDeltaX, event.scrollingDeltaY};
    input.wheel_unit = event.hasPreciseScrollingDeltas ? WheelDeltaUnit::pixels : WheelDeltaUnit::lines;
    input.modifiers = modifiers_for(event.modifierFlags);
    input.pointer_id = 1;
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self collectDamage];
}

- (void)keyDown:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.keyDown:", &run_view_key_down, self, event);
}

- (void)answerKeyDown:(NSEvent*)event {
    KeyEvent input;
    input.action = KeyAction::down;
    input.physical_key = physical_key_for(event.keyCode);
    input.scan_code = event.keyCode;
    input.modifiers = modifiers_for(event.modifierFlags);
    input.repeat = event.isARepeat;
    // While the input method holds marked text it owns every key: Return
    // commits, Escape cancels, arrows and Backspace edit the composition
    // (audit N105). The model sees the key only when no composition is
    // open, as before.
    if ([self hasMarkedText]) {
        [self interpretKeyEvents:@[event]];
        [self collectDamage];
        return;
    }
    const bool handled = [self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)].handled;
    if (!handled) {
        [self interpretKeyEvents:@[event]];
    }
    [self collectDamage];
}

- (void)keyUp:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.keyUp:", &run_view_key_up, self, event);
}

- (void)answerKeyUp:(NSEvent*)event {
    KeyEvent input;
    input.action = KeyAction::up;
    input.physical_key = physical_key_for(event.keyCode);
    input.scan_code = event.keyCode;
    input.modifiers = modifiers_for(event.modifierFlags);
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self collectDamage];
}

// A modifier key went down or up: AppKit reports it as a change of the
// modifier flags rather than as keyDown/keyUp. The key is down when its
// own device-dependent flag (the side's bit, IOKit's NX_DEVICE*KEYMASK) is
// set; Caps Lock toggles, so it is down when its flag turned on.
- (void)flagsChanged:(NSEvent*)event {
    gui_forms::detail::call_failing_fast("macos.view.flagsChanged:", &run_view_flags_changed, self, event);
}

- (void)answerFlagsChanged:(NSEvent*)event {
    const std::uint32_t physical = physical_key_for(event.keyCode);
    const NSUInteger flags = event.modifierFlags;
    bool down = false;
    switch (event.keyCode) {
    case 54: down = (flags & 0x10U) != 0U; break;   // Right Command
    case 55: down = (flags & 0x08U) != 0U; break;   // Left Command
    case 56: down = (flags & 0x02U) != 0U; break;   // Left Shift
    case 57: down = (flags & NSEventModifierFlagCapsLock) != 0U; break;
    case 58: down = (flags & 0x20U) != 0U; break;   // Left Option
    case 59: down = (flags & 0x01U) != 0U; break;   // Left Control
    case 60: down = (flags & 0x04U) != 0U; break;   // Right Shift
    case 61: down = (flags & 0x40U) != 0U; break;   // Right Option
    case 62: down = (flags & 0x2000U) != 0U; break; // Right Control
    default: break;
    }
    if (physical == 0U) {
        [super flagsChanged:event];
        return;
    }
    KeyEvent input;
    input.action = down ? KeyAction::down : KeyAction::up;
    input.physical_key = physical;
    input.scan_code = event.keyCode;
    input.modifiers = modifiers_for(flags);
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_event_nanoseconds(event)]);
    [self collectDamage];
}

- (void)insertText:(id)string replacementRange:(NSRange)replacementRange {
    gui_forms::detail::call_failing_fast(
        "macos.view.insertText:replacementRange:", &run_view_insert_text, self, string, replacementRange);
}

- (void)answerInsertText:(id)string replacementRange:(NSRange)replacementRange {
    TextInputEvent input;
    input.text_utf8 = utf8_string(string);
    input.composing = false;
    if (replacementRange.location != NSNotFound) {
        input.replacement_start = static_cast<std::int32_t>(replacementRange.location);
        input.replacement_length = static_cast<std::int32_t>(replacementRange.length);
    }
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_now_nanoseconds()]);
    [_markedText setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    [self collectDamage];
}

- (void)setMarkedText:(id)string
         selectedRange:(NSRange)selectedRange
       replacementRange:(NSRange)replacementRange {
    gui_forms::detail::call_failing_fast(
        "macos.view.setMarkedText:selectedRange:replacementRange:",
        &run_view_set_marked_text, self, string, selectedRange, replacementRange);
}

- (void)answerSetMarkedText:(id)string
         selectedRange:(NSRange)selectedRange
       replacementRange:(NSRange)replacementRange {
    NSString* plain = [string isKindOfClass:[NSAttributedString class]]
        ? [(NSAttributedString*)string string]
        : (NSString*)string;
    [_markedText setAttributedString:[[NSAttributedString alloc]
        initWithString:(plain == nil ? @"" : plain)]];
    _selectedRange = selectedRange;
    TextInputEvent input;
    input.text_utf8 = utf8_string(string);
    input.composing = true;
    if (replacementRange.location != NSNotFound) {
        input.replacement_start = static_cast<std::int32_t>(replacementRange.location);
        input.replacement_length = static_cast<std::int32_t>(replacementRange.length);
    }
    static_cast<void>([self dispatchHostPayload:std::move(input)
                               timestampNanoseconds:host_now_nanoseconds()]);
    [self collectDamage];
}

// The system asks to keep the marked text as typed (NSTextInputClient):
// it is committed as ordinary input, replacing the composition (N105).
- (void)unmarkText {
    gui_forms::detail::call_failing_fast("macos.view.unmarkText", &run_view_unmark_text, self);
}

- (void)answerUnmarkText {
    if (_markedText.length != 0) {
        TextInputEvent input;
        input.text_utf8 = utf8_string(_markedText.string);
        input.composing = false;
        static_cast<void>([self dispatchHostPayload:std::move(input)
                                   timestampNanoseconds:host_now_nanoseconds()]);
    }
    [_markedText setAttributedString:[[NSAttributedString alloc] initWithString:@""]];
    [self collectDamage];
}

- (BOOL)hasMarkedText {
    return gui_forms::detail::call_failing_fast("macos.view.hasMarkedText", &run_view_has_marked_text, self);
}

- (BOOL)answerHasMarkedText {
    return _markedText.length != 0;
}

- (NSRange)markedRange {
    return gui_forms::detail::call_failing_fast("macos.view.markedRange", &run_view_marked_range, self);
}

- (NSRange)answerMarkedRange {
    return [self hasMarkedText] ? NSMakeRange(0, _markedText.length)
                                : NSMakeRange(NSNotFound, 0);
}

- (NSRange)selectedRange {
    return gui_forms::detail::call_failing_fast("macos.view.selectedRange", &run_view_selected_range, self);
}

- (NSRange)answerSelectedRange {
    return _selectedRange;
}

- (nullable NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range
                                                        actualRange:(nullable NSRangePointer)actualRange {
    return gui_forms::detail::call_failing_fast(
        "macos.view.attributedSubstringForProposedRange:actualRange:",
        &run_view_attributed_substring_for_proposed_range, self, range, actualRange);
}

- (nullable NSAttributedString*)answerAttributedSubstringForProposedRange:(NSRange)range
                                                        actualRange:(nullable NSRangePointer)actualRange {
    if (actualRange != nullptr) {
        *actualRange = NSMakeRange(NSNotFound, 0);
    }
    return nil;
}

- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    return gui_forms::detail::call_failing_fast(
        "macos.view.validAttributesForMarkedText", &run_view_valid_attributes_for_marked_text, self);
}

- (NSArray<NSAttributedStringKey>*)answerValidAttributesForMarkedText {
    return @[];
}

- (NSRect)firstRectForCharacterRange:(NSRange)range
                          actualRange:(nullable NSRangePointer)actualRange {
    return gui_forms::detail::call_failing_fast(
        "macos.view.firstRectForCharacterRange:actualRange:",
        &run_view_first_rect_for_character_range, self, range, actualRange);
}

- (NSRect)answerFirstRectForCharacterRange:(NSRange)range
                          actualRange:(nullable NSRangePointer)actualRange {
    if (actualRange != nullptr) {
        *actualRange = range;
    }
    // The window's text input caret (SurfaceRoot::text_input_caret): the
    // focused control's own caret, a sub-surface host's forwarded caret, or
    // the focused control's left edge. Without one, a box at the origin.
    NSRect caret = NSMakeRect(0.0, 0.0, 1.0, 18.0);
    if (_model) {
        const std::optional<GFRect> inputCaret = (*_model).text_input_caret();
        if (inputCaret) {
            caret = NSMakeRect((*inputCaret).x, (*inputCaret).y, (*inputCaret).width, (*inputCaret).height);
        }
    }
    const NSRect inWindow = [self convertRect:caret toView:nil];
    return [self.window convertRectToScreen:inWindow];
}

- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return NSNotFound;
}

- (void)doCommandBySelector:(SEL)selector {
    gui_forms::detail::call_failing_fast(
        "macos.view.doCommandBySelector:", &run_view_do_command_by_selector, self, selector);
}

- (void)answerDoCommandBySelector:(SEL)selector {
    // Physical command keys are handled by the retained control first. The
    // initial text milestone intentionally leaves unimplemented Cocoa editing
    // selectors inert instead of mutating an invisible native text field.
}

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

namespace gui_forms::host {
// Application-scope windowing (macos_application_run.inc). The delegate
// reaches the run only through these, while the run lives.
class MacApplicationRun;
void mac_run_report_frame(MacApplicationRun* run, gui_forms::HostWindowId window);
void mac_run_retire(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept;
void mac_run_close_finished(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept;
void mac_run_report_minimized(MacApplicationRun* run, gui_forms::HostWindowId window,
                              bool minimized);
void mac_run_hidden_on_close(MacApplicationRun* run, gui_forms::HostWindowId window) noexcept;
void mac_run_report_active(MacApplicationRun* run, bool active);
} // namespace gui_forms::host

// Reports application activation to the run's floating policy
// (NSApplicationDidBecomeActive and DidResignActive). Registered for the run
// and removed before the run ends.
@interface GUIFormsActivationObserver : NSObject {
    gui_forms::host::MacApplicationRun* _run;
}
- (instancetype)initWithRun:(gui_forms::host::MacApplicationRun*)run;
- (void)applicationDidBecomeActive:(NSNotification*)notification;
- (void)applicationDidResignActive:(NSNotification*)notification;
- (void)stopObserving;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerApplicationDidBecomeActive:(NSNotification*)notification;
- (void)answerApplicationDidResignActive:(NSNotification*)notification;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsActivationObserver's native entries: each sends the entry's answer message.
void run_activation_application_did_become_active(GUIFormsActivationObserver* object, NSNotification* notification) {
    [object answerApplicationDidBecomeActive:notification];
}
void run_activation_application_did_resign_active(GUIFormsActivationObserver* object, NSNotification* notification) {
    [object answerApplicationDidResignActive:notification];
}
} // namespace

@implementation GUIFormsActivationObserver
- (instancetype)initWithRun:(gui_forms::host::MacApplicationRun*)run {
    self = [super init];
    if (self != nil) {
        _run = run;
        NSNotificationCenter* center = [NSNotificationCenter defaultCenter];
        [center addObserver:self selector:@selector(applicationDidBecomeActive:)
                       name:NSApplicationDidBecomeActiveNotification object:nil];
        [center addObserver:self selector:@selector(applicationDidResignActive:)
                       name:NSApplicationDidResignActiveNotification object:nil];
    }
    return self;
}
- (void)applicationDidBecomeActive:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.activation.applicationDidBecomeActive:",
        &run_activation_application_did_become_active, self, notification);
}

- (void)answerApplicationDidBecomeActive:(NSNotification*)notification {
    gui_forms::host::mac_run_report_active(_run, true);
}
- (void)applicationDidResignActive:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.activation.applicationDidResignActive:",
        &run_activation_application_did_resign_active, self, notification);
}

- (void)answerApplicationDidResignActive:(NSNotification*)notification {
    gui_forms::host::mac_run_report_active(_run, false);
}
- (void)stopObserving {
    [[NSNotificationCenter defaultCenter] removeObserver:self];
    _run = nullptr;
}
@end

namespace gui_forms::host {

// What a quit request did to one window (closeForTermination:).
enum class MacTerminationClose : std::uint8_t {
    // The window does not end the run; the request is for another window.
    not_ending,
    // The closing handler refused; the run goes on.
    refused,
    // The window has closed, so the run is over.
    closed,
};

// The end of a run: what it does once the window that ends it has closed
// (close metrics, final snapshots, the capture check, shutdown). It runs
// once: after [NSApp run] returns or, for a quit request, from the
// application delegate before AppKit ends the process.
struct MacRunEnd final {
    void (*finish)(void* context){};
    void* context{};
    bool finished{};
};

void finish_run(MacRunEnd& end) {
    if (end.finished || end.finish == nullptr) return;
    end.finished = true;
    (*end.finish)(end.context);
}

} // namespace gui_forms::host

namespace {
// The named targets the fail-fast boundary calls for the run's application
// callbacks (windowWillClose:, application:openURLs:).
void run_closed_handler(std::function<void()>& callback) {
    callback();
}
void run_open_files(const std::function<void(std::vector<std::string>)>& open_files,
                    std::vector<std::string>& paths) {
    open_files(std::move(paths));
}
} // namespace

@interface GUIFormsWindowDelegate : NSObject <NSWindowDelegate> {
    __weak GUIFormsView* _view;
    BOOL _stopsApplicationOnClose;
    BOOL _hideOnClose;
    std::function<void()> _closedHandler;
    // The run this window belongs to (observing; the run owns the window and
    // this delegate) and the window's identity in it. Null for run_macos.
    gui_forms::host::MacApplicationRun* _run;
    gui_forms::HostWindowId _windowId;
    // The reason the closed event reports: user for every native close,
    // application when the run destroys the window, quit or session_end
    // when a quit request closes it (closeForTermination:reason:).
    HostCloseReason _closeReason;
}
- (instancetype)initWithView:(GUIFormsView*)view
      stopsApplicationOnClose:(BOOL)stopsApplicationOnClose
                  hideOnClose:(BOOL)hideOnClose
                 closedHandler:(std::function<void()>)closedHandler;
- (void)prepareForApplicationShutdown;
- (void)attachRun:(gui_forms::host::MacApplicationRun*)run
           window:(gui_forms::HostWindowId)window;
// Closes without asking the closing handler (HostWindowing::destroy_window).
- (void)closeWindowForApplication:(NSWindow*)window;
// A quit request (Cmd-Q, the Dock, a quit Apple event, a log-out): for the
// window that ends the run, asks the closing handler with reason (quit, or
// session_end for a log-out, restart or shut-down) and closes the window
// when it agrees. A hide-on-close window closes rather than hides.
- (gui_forms::host::MacTerminationClose)closeForTermination:(NSWindow*)window
                                                     reason:(HostCloseReason)reason;
// Before a quit request closes the run-ending window, every other window's
// closing handler is asked with the same reason, without closing it (audit
// P2), so a secondary window with unsaved work can cancel a quit or a
// log-out, as each window can on Windows (WM_QUERYENDSESSION) and in the
// headless host. NO for the run-ending window, which closeForTermination:
// asks.
- (BOOL)refusesTermination:(HostCloseReason)reason;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (void)answerWindowDidMove:(NSNotification*)notification;
- (void)answerWindowDidResize:(NSNotification*)notification;
- (BOOL)answerWindowShouldClose:(NSWindow*)sender;
- (void)answerWindowDidBecomeKey:(NSNotification*)notification;
- (void)answerWindowDidResignKey:(NSNotification*)notification;
- (void)answerWindowDidChangeOcclusionState:(NSNotification*)notification;
- (void)answerWindowDidMiniaturize:(NSNotification*)notification;
- (void)answerWindowDidDeminiaturize:(NSNotification*)notification;
- (void)answerWindowDidChangeBackingProperties:(NSNotification*)notification;
- (void)answerWindowDidEnterFullScreen:(NSNotification*)notification;
- (void)answerWindowDidExitFullScreen:(NSNotification*)notification;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsWindowDelegate's native entries: each sends the entry's answer message.
void run_window_delegate_window_did_move(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidMove:notification];
}
void run_window_delegate_window_did_resize(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidResize:notification];
}
BOOL run_window_delegate_window_should_close(GUIFormsWindowDelegate* object, NSWindow* sender) {
    return [object answerWindowShouldClose:sender];
}
void run_window_delegate_window_did_become_key(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidBecomeKey:notification];
}
void run_window_delegate_window_did_resign_key(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidResignKey:notification];
}
void run_window_delegate_window_did_change_occlusion_state(
    GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidChangeOcclusionState:notification];
}
void run_window_delegate_window_did_miniaturize(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidMiniaturize:notification];
}
void run_window_delegate_window_did_deminiaturize(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidDeminiaturize:notification];
}
void run_window_delegate_window_did_change_backing_properties(
    GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidChangeBackingProperties:notification];
}
void run_window_delegate_window_did_enter_full_screen(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidEnterFullScreen:notification];
}
void run_window_delegate_window_did_exit_full_screen(GUIFormsWindowDelegate* object, NSNotification* notification) {
    [object answerWindowDidExitFullScreen:notification];
}
} // namespace

@implementation GUIFormsWindowDelegate
- (instancetype)initWithView:(GUIFormsView*)view
      stopsApplicationOnClose:(BOOL)stopsApplicationOnClose
                  hideOnClose:(BOOL)hideOnClose
                 closedHandler:(std::function<void()>)closedHandler {
    self = [super init];
    if (self != nil) {
        _view = view;
        _stopsApplicationOnClose = stopsApplicationOnClose;
        _hideOnClose = hideOnClose;
        _closedHandler = std::move(closedHandler);
        _run = nullptr;
        _windowId = gui_forms::HostWindowId{};
        _closeReason = HostCloseReason::user;
    }
    return self;
}
- (void)attachRun:(gui_forms::host::MacApplicationRun*)run
           window:(gui_forms::HostWindowId)window {
    _run = run;
    _windowId = window;
}
- (void)closeWindowForApplication:(NSWindow*)window {
    _hideOnClose = NO;
    _closeReason = HostCloseReason::application;
    // NSWindow close does not ask windowShouldClose:; windowWillClose: runs
    // before it returns.
    [window close];
}
- (void)windowDidMove:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidMove:", &run_window_delegate_window_did_move, self, notification);
}

- (void)answerWindowDidMove:(NSNotification*)notification {
    gui_forms::host::mac_run_report_frame(_run, _windowId);
}
- (void)windowDidResize:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidResize:", &run_window_delegate_window_did_resize, self, notification);
}

- (void)answerWindowDidResize:(NSNotification*)notification {
    gui_forms::host::mac_run_report_frame(_run, _windowId);
}
- (BOOL)windowShouldClose:(NSWindow*)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowShouldClose:", &run_window_delegate_window_should_close, self, sender);
}

- (BOOL)answerWindowShouldClose:(NSWindow*)sender {
    if (_view != nil && ![_view requestCloseHiding:_hideOnClose reason:HostCloseReason::user]) return NO;
    if (_hideOnClose) {
        [sender orderOut:nil];
        gui_forms::host::mac_run_hidden_on_close(_run, _windowId);
        return NO;
    }
    return YES;
}
- (void)prepareForApplicationShutdown {
    _hideOnClose = NO;
}
- (void)windowWillClose:(NSNotification*)notification {
    [_view notifyClosedWithReason:_closeReason];
    // Retired before the closed callback, so the callback may open a
    // replacement under the same stable id.
    gui_forms::host::mac_run_retire(_run, _windowId);
    if (_closedHandler) {
        std::function<void()> callback = std::move(_closedHandler);
        _closedHandler = {};
        // Application code (audit P1): through the fail-fast boundary.
        gui_forms::detail::call_failing_fast("macos.window_will_close", &run_closed_handler, callback);
    }
    gui_forms::host::mac_run_close_finished(_run, _windowId);
    if (!_stopsApplicationOnClose) return;
    [NSApp stop:nil];
    NSEvent* wake = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
                                        location:NSZeroPoint
                                   modifierFlags:0
                                       timestamp:0
                                    windowNumber:0
                                         context:nil
                                         subtype:0
                                           data1:0
                                           data2:0];
    [NSApp postEvent:wake atStart:NO];
}
- (void)windowDidBecomeKey:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidBecomeKey:", &run_window_delegate_window_did_become_key, self, notification);
}

- (void)answerWindowDidBecomeKey:(NSNotification*)notification {
    [_view notifyActivation:YES];
}
- (void)windowDidResignKey:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidResignKey:", &run_window_delegate_window_did_resign_key, self, notification);
}

- (void)answerWindowDidResignKey:(NSNotification*)notification {
    [_view notifyActivation:NO];
}
- (void)windowDidChangeOcclusionState:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidChangeOcclusionState:",
        &run_window_delegate_window_did_change_occlusion_state, self, notification);
}

- (void)answerWindowDidChangeOcclusionState:(NSNotification*)notification {
    NSWindow* window = notification.object;
    const BOOL visible = (window.occlusionState & NSWindowOcclusionStateVisible) != 0;
    [_view notifyOcclusion:!visible];
}
- (void)windowDidMiniaturize:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidMiniaturize:", &run_window_delegate_window_did_miniaturize, self, notification);
}

- (void)answerWindowDidMiniaturize:(NSNotification*)notification {
    gui_forms::host::mac_run_report_minimized(_run, _windowId, true);
}
- (void)windowDidDeminiaturize:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidDeminiaturize:",
        &run_window_delegate_window_did_deminiaturize, self, notification);
}

- (void)answerWindowDidDeminiaturize:(NSNotification*)notification {
    [_view setNeedsDisplay:YES];
    gui_forms::host::mac_run_report_minimized(_run, _windowId, false);
}
- (void)windowDidChangeBackingProperties:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidChangeBackingProperties:",
        &run_window_delegate_window_did_change_backing_properties, self, notification);
}

- (void)answerWindowDidChangeBackingProperties:(NSNotification*)notification {
    [_view notifyScaleChanged];
}
- (void)windowDidEnterFullScreen:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidEnterFullScreen:",
        &run_window_delegate_window_did_enter_full_screen, self, notification);
}

- (void)answerWindowDidEnterFullScreen:(NSNotification*)notification {
    [_view notifyWindowState];
}
- (void)windowDidExitFullScreen:(NSNotification*)notification {
    gui_forms::detail::call_failing_fast(
        "macos.window_delegate.windowDidExitFullScreen:",
        &run_window_delegate_window_did_exit_full_screen, self, notification);
}

- (void)answerWindowDidExitFullScreen:(NSNotification*)notification {
    [_view notifyWindowState];
}
- (gui_forms::host::MacTerminationClose)closeForTermination:(NSWindow*)window
                                                     reason:(HostCloseReason)reason {
    if (!_stopsApplicationOnClose) return gui_forms::host::MacTerminationClose::not_ending;
    // Asked directly rather than through performClose:, which beeps and does
    // nothing for a window without a close button, or windowShouldClose:,
    // which would report a user close.
    if (_view != nil && ![_view requestCloseHiding:NO reason:reason]) {
        return gui_forms::host::MacTerminationClose::refused;
    }
    _hideOnClose = NO;
    _closeReason = reason;
    // NSWindow close does not ask windowShouldClose:; windowWillClose:
    // reports _closeReason before it returns.
    [window close];
    return gui_forms::host::MacTerminationClose::closed;
}
- (BOOL)refusesTermination:(HostCloseReason)reason {
    if (_stopsApplicationOnClose || _view == nil) return NO;
    const BOOL refused = [_view closeRefusedForReason:reason];
    return refused;
}
@end

// The application delegate for a run. AppKit's terminate: ends the process
// before [NSApp run] returns, which would skip everything a run does after
// its window closes. A quit request therefore closes the window that ends
// the run, as its close button does, then ends the run in place (the run's
// MacRunEnd) and lets the termination proceed, so a log-out or restart goes
// on. Every other window's closing handler is asked first (audit P2). When
// a closing handler refuses, the termination is cancelled, as an
// application with unsaved work cancels it. With no such window, the
// termination proceeds. The work is synchronous, so the reply is given at
// once rather than deferred with NSTerminateLater.
// After a quit, the process ends inside terminate: with status 0: the run's
// own exit code and the program's code after the run do not apply.
//
// It also takes the system's requests to open files (application:openURLs:,
// which AppKit sends instead of application:openFiles: when implemented) and
// hands their paths to the run's open_files callback.
@interface GUIFormsTerminationDelegate : NSObject <NSApplicationDelegate> {
    // Observing: the run function owns it and clears this delegate before
    // it returns.
    gui_forms::host::MacRunEnd* _end;
    // Observing: the run's MacApplicationOptions::open_files, which outlives
    // the delegate's time as NSApp's delegate; null for a run without one.
    const std::function<void(std::vector<std::string>)>* _openFiles;
}
- (instancetype)initWithEnd:(gui_forms::host::MacRunEnd*)end;
- (instancetype)initWithEnd:(gui_forms::host::MacRunEnd*)end
                  openFiles:(const std::function<void(std::vector<std::string>)>*)openFiles;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (NSApplicationTerminateReply)answerApplicationShouldTerminate:(NSApplication*)sender;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsTerminationDelegate's native entries: each sends the entry's answer message.
NSApplicationTerminateReply run_termination_application_should_terminate(
    GUIFormsTerminationDelegate* object, NSApplication* sender) {
    return [object answerApplicationShouldTerminate:sender];
}
} // namespace

@implementation GUIFormsTerminationDelegate
- (instancetype)initWithEnd:(gui_forms::host::MacRunEnd*)end {
    self = [self initWithEnd:end openFiles:nullptr];
    return self;
}
- (instancetype)initWithEnd:(gui_forms::host::MacRunEnd*)end
                  openFiles:(const std::function<void(std::vector<std::string>)>*)openFiles {
    self = [super init];
    if (self != nil) {
        _end = end;
        _openFiles = openFiles;
    }
    return self;
}
- (void)application:(NSApplication*)application openURLs:(NSArray<NSURL*>*)urls {
    static_cast<void>(application);
    if (_openFiles == nullptr || !(*_openFiles)) return;
    std::vector<std::string> paths{};
    paths.reserve(static_cast<std::size_t>([urls count]));
    for (NSURL* url in urls) {
        if (![url isFileURL]) continue;
        // The file system's own spelling of the path, in UTF-8 on macOS.
        const char* path = [url fileSystemRepresentation];
        if (path == nullptr) continue;
        paths.emplace_back(path);
    }
    if (paths.empty()) return;
    // Application code (audit P1): through the fail-fast boundary.
    gui_forms::detail::call_failing_fast("macos.open_urls", &run_open_files, *_openFiles, paths);
}
// The reason of the quit request being handled: session_end when the quit
// Apple event says the session is ending (log out, restart, shut down, with
// or without the system's confirmation dialog), otherwise quit (Cmd-Q, the
// Dock, another application's plain quit event, or no Apple event at all).
static HostCloseReason termination_close_reason() {
    NSAppleEventDescriptor* event = [[NSAppleEventManager sharedAppleEventManager] currentAppleEvent];
    if (event == nil) return HostCloseReason::quit;
    NSAppleEventDescriptor* reason = [event attributeDescriptorForKeyword:kAEQuitReason];
    if (reason == nil) return HostCloseReason::quit;
    // The reason is an enumerated value; some senders type it as a type code.
    OSType code = [reason enumCodeValue];
    if (code == 0) code = [reason typeCodeValue];
    switch (code) {
        case kAELogOut:
        case kAEReallyLogOut:
        case kAEShowRestartDialog:
        case kAERestart:
        case kAEShowShutdownDialog:
        case kAEShutDown:
            return HostCloseReason::session_end;
        default:
            return HostCloseReason::quit;
    }
}

- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)sender {
    return gui_forms::detail::call_failing_fast(
        "macos.termination.applicationShouldTerminate:", &run_termination_application_should_terminate, self, sender);
}

- (NSApplicationTerminateReply)answerApplicationShouldTerminate:(NSApplication*)sender {
    const HostCloseReason reason = termination_close_reason();
    // First every window that does not end the run is asked, in NSApp's
    // window order, and the first refusal cancels the termination (audit
    // P2). A copy is walked: a closing handler may open or close windows.
    NSArray<NSWindow*>* asked = [NSArray arrayWithArray:[sender windows]];
    for (NSWindow* window in asked) {
        const id delegate = [window delegate];
        if (![delegate isKindOfClass:[GUIFormsWindowDelegate class]]) continue;
        GUIFormsWindowDelegate* windowDelegate = static_cast<GUIFormsWindowDelegate*>(delegate);
        if ([windowDelegate refusesTermination:reason]) return NSTerminateCancel;
    }
    for (NSWindow* window in [sender windows]) {
        const id delegate = [window delegate];
        if (![delegate isKindOfClass:[GUIFormsWindowDelegate class]]) continue;
        GUIFormsWindowDelegate* windowDelegate = static_cast<GUIFormsWindowDelegate*>(delegate);
        const gui_forms::host::MacTerminationClose outcome =
            [windowDelegate closeForTermination:window reason:reason];
        if (outcome == gui_forms::host::MacTerminationClose::not_ending) continue;
        if (outcome == gui_forms::host::MacTerminationClose::refused) return NSTerminateCancel;
        if (_end != nullptr) gui_forms::host::finish_run(*_end);
        return NSTerminateNow;
    }
    return NSTerminateNow;
}
@end

// A native popup window (HostWindowKind::popup, macos_application_run.inc):
// a borderless, non-activating panel. A borderless window cannot become key
// by default; a focus-taking popup (HostPopupFocus::takes_focus) may, so its
// own keyboard input reaches it, while a passive popup never does and its
// owner keeps the keyboard.
@interface GUIFormsPopupPanel : NSPanel {
    BOOL _takesFocus;
}
- (instancetype)initWithContentRect:(NSRect)contentRect takesFocus:(BOOL)takesFocus;
// The bodies of this class's native entries (the AppKit overrides and
// targets below): each entry calls its answer through the fail-fast
// boundary (call_failing_fast).
- (BOOL)answerCanBecomeKeyWindow;
@end

namespace {
// The named targets the fail-fast boundary (call_failing_fast) calls for
// GUIFormsPopupPanel's native entries: each sends the entry's answer message.
BOOL run_popup_panel_can_become_key_window(GUIFormsPopupPanel* object) {
    return [object answerCanBecomeKeyWindow];
}
} // namespace

@implementation GUIFormsPopupPanel
- (instancetype)initWithContentRect:(NSRect)contentRect takesFocus:(BOOL)takesFocus {
    // A Splash (no input) is made with takesFocus NO and then told to
    // ignore the mouse (MacApplicationRun::open).
    const NSWindowStyleMask style = NSWindowStyleMaskBorderless | NSWindowStyleMaskNonactivatingPanel;
    self = [super initWithContentRect:contentRect styleMask:style backing:NSBackingStoreBuffered defer:NO];
    if (self != nil) _takesFocus = takesFocus;
    return self;
}
- (BOOL)canBecomeKeyWindow {
    return gui_forms::detail::call_failing_fast(
        "macos.popup_panel.canBecomeKeyWindow", &run_popup_panel_can_become_key_window, self);
}

- (BOOL)answerCanBecomeKeyWindow {
    return _takesFocus;
}
- (BOOL)canBecomeMainWindow {
    return NO;
}
@end

namespace gui_forms::host {

// The macOS convention for the named keyboard commands (owner decision
// DK-30, interview G5-1): Cmd+W closes a document; every other command keeps
// the PC convention's keys, Ctrl among them (Ctrl+Tab, Ctrl+PageDown).
static KeyMap macos_key_map() {
    KeyMap map = pc_key_map();
    map.set_binding(KeyCommand::close_document, {{PhysicalKey::w, Modifier::meta}, {}});
    return map;
}

// accessibility: NSAccessibility publication of the semantic tree,
// including editable text attributes, verified by
// gui_forms_macos_accessibility_tests and the AXUIElement client (stage A).
HostCapabilities macos_capabilities() {
    return {HostCapabilities::current_protocol_version,
            "appkit-macos",
            HostCapability::lifecycle |
                HostCapability::accessibility |
                HostCapability::frame_capture |
                HostCapability::scale_notifications |
                HostCapability::occlusion |
                HostCapability::scheduled_wake |
                HostCapability::pointer_input |
                HostCapability::keyboard_input |
                HostCapability::text_composition |
                HostCapability::monitor_geometry |
                HostCapability::pointer_capture |
                HostCapability::cursor |
                HostCapability::clipboard |
                HostCapability::clipboard_images |
                HostCapability::typed_drag_destination |
                HostCapability::dialogs |
                HostCapability::sound_cues |
                HostCapability::notification_area |
                HostCapability::notification_area_menus |
                HostCapability::runtime_windows |
                HostCapability::window_frames |
                HostCapability::screen_pointer |
                HostCapability::window_at_point |
                HostCapability::window_ordering |
                HostCapability::transparent_windows |
                HostCapability::window_backdrops |
                HostCapability::compositor_notifications |
                HostCapability::presentation_preferences |
                HostCapability::transient_surfaces |
                HostCapability::window_state_reports,
            macos_key_map()};
}

namespace {
// The pending-dispatch callback and the view it collects damage on, both
// kept until the work runs, as the replaced block captured them.
struct DispatchAndCollectWork final {
    GUIFormsView* view;
    std::function<void()> dispatch_pending;
};

void dispatch_and_collect_on_main(void* context) {
    const std::unique_ptr<DispatchAndCollectWork> work(static_cast<DispatchAndCollectWork*>(context));
    if ((*work).dispatch_pending) (*work).dispatch_pending();
    [(*work).view collectDamage];
}

void make_key_and_activate_on_main(void* context) {
    const std::unique_ptr<WindowWork> work(static_cast<WindowWork*>(context));
    [(*work).window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void toggle_full_screen_on_main(void* context) {
    const std::unique_ptr<WindowWork> work(static_cast<WindowWork*>(context));
    [(*work).window toggleFullScreen:nil];
}

void order_out_on_main(void* context) {
    const std::unique_ptr<WindowWork> work(static_cast<WindowWork*>(context));
    [(*work).window orderOut:nil];
}

// run_macos's test close: up to `attempts` performClose: while visible.
struct RepeatedCloseWork final {
    NSWindow* window;
    std::uint32_t attempts;
};

void repeated_close_on_main(void* context) {
    const std::unique_ptr<RepeatedCloseWork> work(static_cast<RepeatedCloseWork*>(context));
    for (std::uint32_t attempt = 0; attempt < (*work).attempts && (*work).window.isVisible; ++attempt) {
        [(*work).window performClose:nil];
    }
}
} // namespace

class DispatchAndCollectWake final {
public:
    DispatchAndCollectWake(
        GUIFormsView* view,
        std::function<void()> dispatch_pending) noexcept
        : view_(view), dispatch_pending_(std::move(dispatch_pending)) {}

    void operator()() const
    {
        DispatchAndCollectWork* const work = new DispatchAndCollectWork{view_, dispatch_pending_};
        post_main_work(work, &dispatch_and_collect_on_main);
    }

private:
    GUIFormsView* view_;
    std::function<void()> dispatch_pending_;
};

class RequestNativeClose final {
public:
    explicit RequestNativeClose(NSWindow* window) noexcept : window_(window) {}

    void operator()() const
    {
        post_perform_close(window_);
    }

private:
    NSWindow* window_;
};

class RequestNativeShow final {
public:
    explicit RequestNativeShow(NSWindow* window) noexcept : window_(window) {}

    void operator()() const
    {
        WindowWork* const work = new WindowWork{window_};
        post_main_work(work, &make_key_and_activate_on_main);
    }

private:
    NSWindow* window_;
};

class RequestNativeTitle final {
public:
    RequestNativeTitle(NSWindow* const window, GUIFormsView* const view) noexcept
        : window_(window), view_(view) {}
    HostServiceStatus operator()(const std::string_view title) const {
        if (![NSThread isMainThread]) return {HostServiceError::wrong_thread};
        try {
            @try {
                NSWindow* const window = window_;
                GUIFormsView* const view = view_;
                if (window == nil || view == nil || ![view acceptsWindowRequest])
                    return {HostServiceError::after_shutdown};
                if (title.size() > 65536U || title.find('\0') != std::string_view::npos ||
                    !validate_utf8(title).valid()) return {HostServiceError::invalid_argument};
                NSString* const text = native_string(title);
                if (text == nil) return {HostServiceError::backend_failure};
                [window setTitle:text];
                return {};
            } @catch (NSException* failure) {
                static_cast<void>(failure);
                return {HostServiceError::backend_failure};
            }
        } catch (...) { return {HostServiceError::backend_failure}; }
    }
private:
    __weak NSWindow* window_;
    __weak GUIFormsView* view_;
};
// Asks for a content size in points (GUI.Forms logical units). AppKit resizes
// the view before setContentSize: returns, and the model is resized through
// the ordinary resize event (deferred while a host callback is running).
class RequestNativeClientSize final {
public:
    RequestNativeClientSize(NSWindow* const window, GUIFormsView* const view) noexcept
        : window_(window), view_(view) {}
    HostServiceStatus operator()(const GFSize clientSize) const {
        if (![NSThread isMainThread]) return {HostServiceError::wrong_thread};
        try {
            @try {
                NSWindow* const window = window_;
                GUIFormsView* const view = view_;
                if (window == nil || view == nil || ![view acceptsWindowRequest])
                    return {HostServiceError::after_shutdown};
                if (!std::isfinite(clientSize.width) || !std::isfinite(clientSize.height) ||
                    clientSize.width > 32768.0 || clientSize.height > 32768.0)
                    return {HostServiceError::invalid_argument};
                const NSSize minimum = [window contentMinSize];
                if (clientSize.width < std::max(1.0, static_cast<double>(minimum.width)) ||
                    clientSize.height < std::max(1.0, static_cast<double>(minimum.height)))
                    return {HostServiceError::invalid_argument};
                const bool fullScreen = ([window styleMask] & NSWindowStyleMaskFullScreen) != 0;
                if (fullScreen || [window isMiniaturized] || [window isZoomed])
                    return {HostServiceError::unsupported};
                [window setContentSize:NSMakeSize(clientSize.width, clientSize.height)];
                return {};
            } @catch (NSException* failure) {
                static_cast<void>(failure);
                return {HostServiceError::backend_failure};
            }
        } catch (...) { return {HostServiceError::backend_failure}; }
    }
private:
    __weak NSWindow* window_;
    __weak GUIFormsView* view_;
};
class RequestNativeFullScreen final {
public:
    explicit RequestNativeFullScreen(NSWindow* window) noexcept : window_(window) {}
    void operator()() const {
        WindowWork* const work = new WindowWork{window_};
        post_main_work(work, &toggle_full_screen_on_main);
    }
private:
    NSWindow* window_;
};

class RequestNativeHide final {
public:
    explicit RequestNativeHide(NSWindow* window) noexcept : window_(window) {}

    void operator()() const
    {
        WindowWork* const work = new WindowWork{window_};
        post_main_work(work, &order_out_on_main);
    }

private:
    NSWindow* window_;
};

class ShowNativeDialog final {
public:
    explicit ShowNativeDialog(GUIFormsView* view) noexcept : view_(view) {}

    HostDialogResult operator()(const HostDialogRequest& request) const
    {
        return [view_ showHostDialog:request];
    }

private:
    GUIFormsView* view_;
};

class ReadNativeClipboard final {
public:
    explicit ReadNativeClipboard(GUIFormsView* view) noexcept : view_(view) {}

    HostClipboardTextResult operator()() const
    {
        return [view_ readHostClipboard];
    }

private:
    GUIFormsView* view_;
};

class WriteNativeClipboard final {
public:
    explicit WriteNativeClipboard(GUIFormsView* view) noexcept : view_(view) {}

    HostServiceStatus operator()(std::string_view text) const
    {
        return [view_ writeHostClipboard:text];
    }

private:
    GUIFormsView* view_;
};

class StableWindowIdEquals final {
public:
    explicit StableWindowIdEquals(std::string_view stable_id) noexcept
        : stable_id_(stable_id) {}

    bool operator()(const MacApplicationWindow& candidate) const noexcept
    {
        return candidate.stable_id == stable_id_;
    }

private:
    std::string_view stable_id_;
};


namespace {

// A single-window run's end (MacRunEnd's context): borrowed from run_macos,
// which outlives every use.
struct SingleWindowRunEnd final {
    GUIFormsView* view{};
    NSWindow* window{};
    MacHostOptions* options{};
    int* exit_code{};
};

void end_single_window_run(void* context) {
    SingleWindowRunEnd& end = *static_cast<SingleWindowRunEnd*>(context);
    MacHostOptions& options = *end.options;
    [NSApp setDelegate:nil];
    const std::string metrics = [end.view metricsJSON];
    const std::string host = [end.view hostJSON];
    if (options.print_metrics_on_close) {
        std::fprintf(stdout, "{\"window\":%s,\"host\":%s}\n",
                     metrics.c_str(), host.c_str());
        std::fflush(stdout);
    }
    if (options.final_snapshot) {
        options.final_snapshot(metrics, host);
    }
    // Portable contract: a requested capture that was never saved (no
    // covering frame before close, or an encode/write failure) fails the
    // process.
    if (!options.capture_path.empty() && [end.view frameCaptureSaved] == NO) {
        *end.exit_code = 6;
    }
    [end.view prepareForShutdown];
    [end.window setDelegate:nil];
    [end.window setContentView:nil];
}

} // namespace

int run_macos(std::unique_ptr<Window> model, MacHostOptions options) {
    if (!model || !options.initially_visible || options.hide_on_close) {
        return 2;
    }
    std::vector<std::string> windowDragRegionIds;
    if (!options.window_drag_region_id.empty()) {
        windowDragRegionIds.push_back(options.window_drag_region_id);
    }
    windowDragRegionIds.insert(windowDragRegionIds.end(),
                               options.window_drag_region_ids.begin(),
                               options.window_drag_region_ids.end());
    if (!validate_window_chrome_drag_regions(
            *model, windowDragRegionIds).accepted()) {
        return 2;
    }
    int exitCode = 0;
    @autoreleasepool {
        NSApplication* application = [NSApplication sharedApplication];
        [application setActivationPolicy:NSApplicationActivationPolicyRegular];
        detail::install_application_menu();

        const Size initialSize = options.capture_size.has_value()
            ? *options.capture_size : options.initial_size;
        const NSRect frame = NSMakeRect(0.0, 0.0,
                                        initialSize.width,
                                        initialSize.height);
        const bool transparentFullSizeContent =
            options.titlebar_presentation ==
            MacTitlebarPresentation::transparent_full_size_content;
        NSWindowStyleMask style = NSWindowStyleMaskTitled |
            NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable |
            NSWindowStyleMaskResizable;
        if (transparentFullSizeContent) {
            style |= NSWindowStyleMaskFullSizeContentView;
        }
        NSWindow* nativeWindow = [[NSWindow alloc]
            initWithContentRect:frame
                      styleMask:style
                        backing:NSBackingStoreBuffered
                          defer:NO];
        // ARC owns this local for the complete run loop. AppKit's historical
        // self-release-on-close policy would otherwise release the same window
        // a second time when the autorelease pool drains.
        [nativeWindow setReleasedWhenClosed:NO];
        GUIFormsView* view = [[GUIFormsView alloc]
            initWithModel:std::move(model)
            transparentFullSizeContent:transparentFullSizeContent
            windowDragRegionIds:std::move(windowDragRegionIds)];
        [view installCloseRequestHandler:std::move(options.close_request)];
        if (!options.capture_path.empty()) {
            [view installFrameCapturePath:options.capture_path];
        }
        GUIFormsWindowDelegate* delegate =
            [[GUIFormsWindowDelegate alloc] initWithView:view
                                 stopsApplicationOnClose:YES
                                             hideOnClose:NO
                                            closedHandler:std::move(options.closed)];
        [nativeWindow setDelegate:delegate];
        [nativeWindow setTitle:[NSString stringWithUTF8String:options.title.c_str()]];
        if (transparentFullSizeContent) {
            [nativeWindow setTitlebarAppearsTransparent:YES];
            [nativeWindow setTitleVisibility:NSWindowTitleHidden];
        }
        [nativeWindow setContentMinSize:NSMakeSize(options.minimum_size.width,
                                            options.minimum_size.height)];
        [nativeWindow setContentView:[view hostContentView]];
        [nativeWindow center];
        if ([view initializeHost] == NO) {
            [view prepareForShutdown];
            [nativeWindow setDelegate:nil];
            [nativeWindow setContentView:nil];
            return 5;
        }
        if (options.host_ready) {
            const std::function<void()> dispatchPending = options.dispatch_pending;
            options.host_ready(
                DispatchAndCollectWake(view, dispatchPending),
                RequestNativeClose(nativeWindow),
                ShowNativeDialog(view),
                ReadNativeClipboard(view),
                WriteNativeClipboard(view));
        }
        if (options.full_screen_ready) options.full_screen_ready(RequestNativeFullScreen(nativeWindow));
        if (options.title_ready) options.title_ready(RequestNativeTitle(nativeWindow, view));
        if (options.client_size_ready)
            options.client_size_ready(RequestNativeClientSize(nativeWindow, view));
        if (options.visibility_ready) {
            options.visibility_ready(RequestNativeShow(nativeWindow),
                                     RequestNativeHide(nativeWindow));
        }
        // Match every host: one FIFO initialization turn runs after portable
        // attach/service publication and before first visible presentation.
        [view drainPostedWork];
        if (options.dispatch_pending) options.dispatch_pending();
        [view collectDamage];
        [nativeWindow makeKeyAndOrderFront:nil];
        [application activateIgnoringOtherApps:YES];
        if (options.close_after_launch_for_testing) {
            const std::uint32_t closeAttempts =
                std::max<std::uint32_t>(1, options.close_attempts_for_testing);
            RepeatedCloseWork* const work = new RepeatedCloseWork{nativeWindow, closeAttempts};
            post_main_work(work, &repeated_close_on_main);
        }
        SingleWindowRunEnd ending{view, nativeWindow, &options, &exitCode};
        MacRunEnd end{&end_single_window_run, &ending, false};
        // NSApp holds its delegate weakly; this local keeps it to the end,
        // and end_single_window_run clears it.
        GUIFormsTerminationDelegate* termination = [[GUIFormsTerminationDelegate alloc] initWithEnd:&end];
        [application setDelegate:termination];
        [application run];
        finish_run(end);
    }
    return exitCode;
}

#include "macos_application_run.inc"

} // namespace gui_forms::host

#include "macos_splashscreen.inc"
