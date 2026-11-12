#pragma once

// GUIFormsView's private opt-in diagnostics (macos_view_diagnostics.mm):
// the causal wake trace, the CPU attribution and the paint phases. Each is a
// field of the view, used on the main executor only, and read by the native
// tests through the view's snapshot messages.

#include "gui_forms/scheduler/types/scheduler_types.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace gui_forms::host::macos {

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

    [[nodiscard]] std::uint64_t timestamp(const gui_forms::FrameTime value) noexcept;
    void advance(std::uint64_t& serial) noexcept;
    void seed(const std::optional<gui_forms::FrameTime>& deadline) noexcept;
    [[nodiscard]] MacCausalRow make_row(const MacCausalKind kind,
                                       const gui_forms::FrameTime time) noexcept;
    void append(MacCausalRow row) noexcept;
    void record_arm(const gui_forms::FrameTime now,
                    const std::optional<gui_forms::FrameTime>& deadline,
                    const std::int64_t requested_delay_ns) noexcept;
    void record_wake(const gui_forms::FrameTime now) noexcept;
    void record_poll(const gui_forms::FrameTime now, const std::uint64_t fired,
                     const std::optional<gui_forms::FrameTime>& next) noexcept;
    void record_draw_entry(const gui_forms::FrameTime now) noexcept;
    void record_draw_exit(const gui_forms::FrameTime now) noexcept;
    [[nodiscard]] MacCausalSnapshot snapshot() const noexcept;
};

class MacCausalWakeScope final {
public:
    explicit MacCausalWakeScope(MacCausalTrace& trace) noexcept;
    ~MacCausalWakeScope();
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

    void add(const std::size_t index, const std::uint64_t amount) noexcept;
    void record_draw(const std::uint64_t wall_ns,
                     const std::array<double, 4>& current,
                     const std::size_t bytes) noexcept;
};

// This thread's CPU time in nanoseconds; false, leaving `output` unchanged,
// when the clock fails or its value does not fit.
bool read_mac_thread_cpu(std::uint64_t& output) noexcept;

class MacCpuScope final {
public:
    explicit MacCpuScope(MacCpuAttribution& owner, const std::size_t index) noexcept;
    ~MacCpuScope();
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
                const std::chrono::steady_clock::time_point end) noexcept;
    [[nodiscard]] std::string json_fields(const std::string_view name) const;
};

} // namespace gui_forms::host::macos

using gui_forms::host::macos::MacCausalSnapshot;
using gui_forms::host::macos::MacCausalTrace;
using gui_forms::host::macos::MacCausalWakeScope;
using gui_forms::host::macos::MacCpuAttribution;
using gui_forms::host::macos::MacCpuScope;
using gui_forms::host::macos::MacPaintPhase;
