#pragma once
// The presentation shim's trace (docs/user/presentation.md): one record per
// step of a frame's presentation, kept in a fixed ring, and the normative
// order every path's trace must follow. GUI.Forms' own window manager
// (src/host/surface/) is the reference: its traces define the behaviour the
// platform paths' traces are compared against (check_present_trace for the
// order, present_traces_equivalent for two traces of the same scenario).
//
// One execution context: the shim's (the window's UI thread). Plain
// portable C++.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace gui_forms::host::presentation {

// Which path a step ran on.
enum class PresentPath : std::uint8_t {
    // No path yet (the shim's own steps: a fallback decision, a release).
    none,
    // The safe fallback: the host copies the frame into the window system's
    // own storage (a CoreGraphics draw, XPutImage, a DIB blit).
    copy,
    // A buffer the window system gives the program to write, possibly
    // GPU-backed, written blind with CPU pixels (IOSurface, an X11 Present
    // pixmap, a wl_shm buffer, the window manager's surface).
    direct,
};

// The steps of one frame, in the order check_present_trace admits.
enum class PresentTraceEvent : std::uint8_t {
    // The direct path granted a buffer. detail: the buffer's age, capped at
    // 255 (0: contents unknown).
    acquire,
    // The direct path refused a buffer. detail: the DirectRefusal.
    refuse,
    // A host scroll moved presented pixels. rect_count 1, pixel_count: the
    // pixels moved.
    scroll,
    // A host scroll was declined (the path cannot move pixels, or what it
    // shows is not the frame before); its area joins the damage.
    // pixel_count: the area's pixels.
    scroll_refused,
    // The rectangles written (direct) or handed to the copy (copy).
    // rect_count and pixel_count count them.
    damage,
    // The frame was handed to the window system. detail: the PresentOutcome.
    present,
    // The direct path's buffers were lost (a present reported it, or the
    // window system said so between frames).
    lost,
    // The shim moved to the copy path. detail: the FallbackReason.
    fallback,
    // The shim tries the direct path again after a fallback.
    resume,
    // The direct path released its buffers.
    release,
    // The window system finished reading a pending present (the buffer may
    // be written again).
    released_by_host,
};

// What handing a frame to the window system did (the detail of a present
// record).
enum class PresentOutcome : std::uint8_t {
    // On screen, or copied out; the buffer may be acquired again at once.
    presented,
    // The window system reads the buffer later: no pixel of it is written
    // until a released_by_host notice (a browser's next animation frame, a
    // compositor's buffer release).
    pending,
    // Another surface holds the screen (a virtual terminal switch, an
    // occluded window that declines updates): nothing was shown and the
    // damage is kept.
    inactive,
    // The present failed; the damage is kept and tried again.
    failed,
    // The direct path's buffers are gone (a lost IOSurface, a destroyed
    // pixmap, a compositor that dropped the buffer): the shim falls back.
    lost,
};

// Why the direct path refused a buffer (the detail of a refuse record).
enum class DirectRefusal : std::uint8_t {
    // The path cannot work here at all (a remote X display, a visual it
    // cannot write, a window system without the facility): the shim stays
    // on the copy path for the window's life.
    unsupported,
    // Not at this size (too large, or a size the window system declined):
    // the shim tries again after the size changes.
    extent,
    // The window system could not supply the memory now: tried again after
    // a pause of retry_frames frames.
    resources,
    // Every buffer is still held by the window system: tried again after a
    // pause of retry_frames frames.
    busy,
    // The buffers were lost while acquiring: as a loss.
    lost,
};

// Why the shim fell back to the copy path.
enum class FallbackReason : std::uint8_t {
    // The direct path refused (detail of the refuse record before it).
    refused,
    // The direct path's buffers were lost.
    lost,
    // The present on the direct path failed.
    failed,
};

struct PresentTraceRecord final {
    // The shim's frame counter: every record of one frame shares it, and it
    // never decreases.
    std::uint64_t frame{};
    PresentTraceEvent event{PresentTraceEvent::acquire};
    PresentPath path{PresentPath::none};
    std::uint8_t detail{};
    std::uint32_t rect_count{};
    std::uint64_t pixel_count{};

    friend bool operator==(const PresentTraceRecord& a, const PresentTraceRecord& b) noexcept {
        const bool equal = a.frame == b.frame && a.event == b.event && a.path == b.path && a.detail == b.detail &&
                           a.rect_count == b.rect_count && a.pixel_count == b.pixel_count;
        return equal;
    }
};

// The last `capacity` records, oldest dropped first. Recording is off until
// enabled (a test, a probe, the diagnostics overlay); off, record() is one
// branch. Fixed storage, no allocation.
class PresentTrace final {
public:
    static constexpr std::size_t capacity = 256U;

    void set_enabled(bool enabled) noexcept { enabled_ = enabled; }
    [[nodiscard]] bool enabled() const noexcept { return enabled_; }
    void record(const PresentTraceRecord& record) noexcept;
    // Forgets every record; the enabled state stays.
    void clear() noexcept;
    // The records kept, at most `capacity`.
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    // Every record recorded since the last clear, including those dropped.
    [[nodiscard]] std::uint64_t total() const noexcept { return total_; }
    // Copies the kept records, oldest first, into `out`, and returns the
    // count copied (at most out.size(); the newest are dropped when it is
    // smaller).
    std::size_t fill(std::span<PresentTraceRecord> out) const noexcept;

private:
    std::array<PresentTraceRecord, capacity> ring_{};
    // The slot the next record goes to; size_ slots before it are live.
    std::size_t next_{};
    std::size_t size_{};
    std::uint64_t total_{};
    bool enabled_{};
};

// What check_present_trace found.
enum class PresentTraceViolation : std::uint8_t {
    none,
    // A record's frame is smaller than the one before it.
    frame_decreased,
    // A step came where the frame's order does not admit it.
    out_of_order,
    // A frame ended (a new frame began, or the trace ended) between a
    // direct acquire and its present, or between a fallback and its copy.
    incomplete_frame,
    // A fallback without a refusal, a loss or a failed present before it
    // (in its frame, or a loss between frames).
    fallback_without_cause,
    // A record names a path its step cannot run on.
    wrong_path,
};

struct PresentTraceCheck final {
    PresentTraceViolation violation{PresentTraceViolation::none};
    // The index of the offending record (the trace's size for an
    // incomplete last frame).
    std::size_t index{};

    [[nodiscard]] bool passed() const noexcept { return violation == PresentTraceViolation::none; }
};

// Checks `records` against the normative order. Per frame:
//
//   frame    := [resume] [direct | refused] [copy] [release]
//               (at least one of direct, refused, copy; a frame may also be
//                only lost, release or released_by_host records, which
//                arrive between frames)
//   direct   := acquire(direct) scroll-step* damage(direct) present(direct)
//               [lost [release] fallback copy]   (after a lost outcome)
//               [fallback copy]                  (may follow a failed one)
//   refused  := refuse(direct) [release] [fallback copy]   (a frame may end
//               after the refusal where no copy path exists: the window
//               manager)
//   copy     := scroll-step(copy)* damage(copy) present(copy)
//   scroll-step := scroll | scroll_refused
//
// The path of acquire, refuse and the direct damage and present is direct;
// those of the copy are copy; fallback, resume, release and
// released_by_host are none or direct.
[[nodiscard]] PresentTraceCheck check_present_trace(std::span<const PresentTraceRecord> records) noexcept;

// Whether two traces of one scenario took the same steps: the same events
// on the same paths with the same details (a buffer's age excepted: a
// platform may keep more buffers), in the same order. Frame numbers and
// counts may differ (a platform merges damage rectangles differently).
[[nodiscard]] bool present_traces_equivalent(std::span<const PresentTraceRecord> a,
                                             std::span<const PresentTraceRecord> b) noexcept;

} // namespace gui_forms::host::presentation
