#pragma once
// The presentation shim (owner, 2026-11-16; docs/user/presentation.md):
// how a host shows a frame GUI.Forms painted on the CPU.
//
// Two paths:
// - the copy path, the safe fallback every host has: the host copies the
//   frame's damaged rectangles into the window system's own storage (a
//   CoreGraphics draw, XPutImage or MIT-SHM, a DIB blit);
// - the direct path, where the window system has one: it gives the program
//   a buffer to write CPU pixels into, and presents that buffer itself
//   (an IOSurface as layer contents, an X11 Present pixmap, a wl_shm buffer,
//   the window manager's surface). The buffer may be GPU-backed; GUI.Forms
//   treats it as a blind interface: it locks, writes CPU pixels, unlocks
//   and presents, and never issues a GPU command, compiles a shader, or
//   creates a GPU device or context.
//
// Every direct path is capability-reported (HostCapability::direct_buffers)
// and any refusal or loss falls back to the copy path with the same pixels:
// the shim then copies the whole frame, so nothing the direct path showed
// survives. Host scroll (HostCapability::host_scroll) moves pixels already
// presented and then repaints only what the move exposed.
//
// Execution context: a shim, its paths and its trace belong to one thread,
// the one that paints its window (the host's UI thread). Every call is made
// there, the window system's loss and release notices included (the host
// forwards them from its event loop). The source frame is borrowed for one
// call; a direct buffer is borrowed between acquire and present.
//
// Plain portable C++: no AppKit, Win32, X11 or Wayland type or assumption.
// Platform paths implement the two interfaces below in their host
// directories.

#include "presentation_geometry.hpp"
#include "presentation_trace.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string_view>
#include <variant>

namespace gui_forms::host::presentation {

// A buffer the direct path granted: `extent.height` rows of
// `extent.width` premultiplied BGRA8 pixels, `row_bytes` apart, writable by
// the CPU until the present (or release) that ends the grant. `age` is the
// number of presents since this buffer was last presented (1: it holds the
// frame presented last), or 0 when its contents are unknown.
struct DirectBuffer final {
    std::byte* pixels{};
    std::size_t row_bytes{};
    PresentExtent extent{};
    std::uint32_t age{};
};

// The direct path's answer to an acquire: a buffer, or why not. Built only
// through granted() and refused(); read through buffer() and refusal().
class DirectAcquire final {
public:
    [[nodiscard]] static DirectAcquire granted(DirectBuffer buffer) noexcept;
    [[nodiscard]] static DirectAcquire refused(DirectRefusal refusal) noexcept;

    // The granted buffer; null when refused.
    [[nodiscard]] const DirectBuffer* buffer() const noexcept;
    // The refusal; nullopt when granted.
    [[nodiscard]] std::optional<DirectRefusal> refusal() const noexcept;

private:
    explicit DirectAcquire(std::variant<DirectBuffer, DirectRefusal> state) noexcept : state_(state) {}
    std::variant<DirectBuffer, DirectRefusal> state_;
};

// The safe fallback, which every host implements over its window system's
// own copy (the existing paths, unchanged).
class PresentationCopyPath {
public:
    PresentationCopyPath() = default;
    PresentationCopyPath(const PresentationCopyPath&) = delete;
    PresentationCopyPath& operator=(const PresentationCopyPath&) = delete;
    virtual ~PresentationCopyPath();

    // Whether scroll() can move presented pixels (an X11 window's
    // XCopyArea). Fixed for the path's life.
    [[nodiscard]] virtual bool scrolls() const noexcept = 0;
    // Moves the presented pixels plan.source to plan.destination in the
    // window system's storage, before any rectangle of the frame is copied.
    // Called only when scrolls() and only while what the window shows is
    // the frame before (the shim tracks it). Returns false, moving nothing,
    // when the window system declines now; the shim then copies the area.
    virtual bool scroll(const PresentScrollPlan& plan) = 0;
    // Copies `rects` (within source.extent, at least one) of `source` to the
    // window. Returns presented, inactive or failed (the shim treats any
    // other outcome as failed).
    virtual PresentOutcome copy(const PresentSourcePixels& source, std::span<const PresentRect> rects) = 0;
};

// A window system's own buffers (the blind interface).
class PresentationDirectPath {
public:
    PresentationDirectPath() = default;
    PresentationDirectPath(const PresentationDirectPath&) = delete;
    PresentationDirectPath& operator=(const PresentationDirectPath&) = delete;
    virtual ~PresentationDirectPath();

    // A CPU-writable buffer of `extent`, granted until the next present or
    // release; or the refusal. Never called while a grant is held.
    virtual DirectAcquire acquire(PresentExtent extent) = 0;
    // Presents the granted buffer, telling the window system that `damage`
    // (at least one rectangle, within the buffer) changed since the frame
    // presented before. Ends the grant whatever the outcome.
    virtual PresentOutcome present(std::span<const PresentRect> damage) = 0;
    // Gives every buffer back and stops showing them (a layer's contents
    // cleared, a pixmap freed), so that the copy path's pixels show. Ends
    // any grant. Safe to call when nothing is held.
    virtual void release() noexcept = 0;
};

// What a host's shim may use beyond the copy path. Both off is today's
// behaviour, and the default until each platform's paths are verified
// (the owner's to flip). A host reports direct_buffers and host_scroll
// only where its policy turns them on and it implements them.
struct PresentPolicy final {
    // The direct path where the host has one, the copy path as its
    // fallback.
    bool direct{};
    // Host scroll where the host's path can move presented pixels.
    bool host_scroll{};
};

// The policy GUI_FORMS_PRESENT names: a comma-separated list of "direct"
// and "scroll" (unknown words are ignored); unset or empty, both off. Read
// at each call: a host reads it once when it makes a window.
[[nodiscard]] PresentPolicy present_policy_from_environment() noexcept;
// Whether the comma-separated `list` names `word` exactly.
[[nodiscard]] bool present_list_names(std::string_view list, std::string_view word) noexcept;
// Whether GUI_FORMS_PRESENT names `word`: a host adapter's own route
// selection (a buffer route it is built with, under a name it documents),
// read when it makes a window. False when the variable is unset.
[[nodiscard]] bool present_policy_names(std::string_view word) noexcept;

// One frame for present(): the complete frame (the host's raster, which
// keeps every pixel between frames), what changed in it since the frame
// before, and the host scrolls that moved its pixels, in order. The frame's
// own pixels already show each scroll's result; the damage already holds
// what each scroll exposed.
struct PresentFrame final {
    PresentSourcePixels source{};
    std::span<const PresentRect> damage{};
    std::span<const PresentScroll> scrolls{};
};

// What present() did.
struct PresentReport final {
    PresentOutcome outcome{PresentOutcome::failed};
    // The path that presented the frame (copy after a fallback).
    PresentPath path{PresentPath::none};
    // The rectangles and pixels written into a direct buffer, or handed to
    // the copy.
    std::uint32_t rects{};
    std::uint64_t pixels{};
};

// The shim's counters, for the host's metrics and the diagnostics overlay.
struct PresentShimSnapshot final {
    std::uint64_t frames{};
    std::uint64_t direct_frames{};
    std::uint64_t copy_frames{};
    std::uint64_t refusals{};
    std::uint64_t losses{};
    std::uint64_t fallbacks{};
    std::uint64_t scrolls_moved{};
    std::uint64_t scrolls_refused{};
    std::uint64_t pixels_written{};
    std::uint64_t pixels_moved{};
    // The path the next frame tries.
    PresentPath next_path{PresentPath::copy};
    // The direct path is off for the window's life (refused as
    // unsupported, or lost too often).
    bool direct_disabled{};
};

// A held direct grant in producer mode, and what a scroll on it did.
struct PresentScrollResult final {
    PresentScrollPlan plan{};
    // The pixels moved inside the held buffer; false when the area must be
    // written instead.
    bool moved{};
};

class PresentationShim final {
public:
    // Frames written past a refusal for want of resources, or a busy one,
    // before the direct path is tried again.
    static constexpr std::uint32_t retry_frames = 120U;
    // Losses in a row after which the direct path stays off.
    static constexpr std::uint32_t maximum_consecutive_losses = 3U;
    // The rectangles one frame writes; more are written as their bounding
    // rectangle (DamageRegion keeps at most 64, plus the scrolls' areas).
    static constexpr std::size_t maximum_frame_rects = 128U;

    // `copy` is borrowed and must outlive the shim (declare it first);
    // null for a host with no copy path (the window manager, whose surface
    // is its only path: present() is not used there). `direct` is owned and
    // may be null (no direct path, or a policy without direct). `trace` is
    // borrowed, may be null, and must outlive the shim.
    PresentationShim(PresentationCopyPath* copy, std::unique_ptr<PresentationDirectPath> direct,
                     PresentTrace* trace) noexcept;
    PresentationShim(const PresentationShim&) = delete;
    PresentationShim& operator=(const PresentationShim&) = delete;
    // Releases the direct path's buffers.
    ~PresentationShim();

    // Presents one frame: on the direct path when it is usable (scrolls
    // moved inside the buffer where its age allows, the damage written,
    // presented), otherwise, or after a refusal or loss, on the copy path
    // (scrolls moved by the window system where it can, the damage
    // copied). After a path change the whole frame is written. A frame
    // with no damage and no scroll presents nothing and reports presented.
    // The caller keeps its damage when the outcome is inactive or failed.
    PresentReport present(const PresentFrame& frame);

    // Producer mode, for a host that composes straight into the buffer
    // (the window manager): acquire, then scroll_written for each scroll,
    // then the caller writes, then present_written. No copy path is
    // involved; a refusal or loss is the caller's to handle (it keeps its
    // damage and tries again).
    DirectAcquire acquire_for_writing(PresentExtent extent);
    // Applies one host scroll to the held buffer: moved in place when the
    // buffer holds the frame before (age 1), else the caller writes the
    // area. Requires a held grant.
    PresentScrollResult scroll_written(const PresentScroll& scroll);
    // Records a host scroll the caller does not move (something covers its
    // area, or it would read pixels not yet written): its area is written
    // instead. Requires a held grant.
    void decline_scroll(const PresentScroll& scroll);
    // Presents the held buffer with `damage` (what the caller wrote, plus
    // the area of every scroll, since the screen changed there). Requires a
    // held grant.
    PresentOutcome present_written(std::span<const PresentRect> damage);

    // The window system's notices, forwarded on the shim's thread:
    // the direct buffers are gone (released, and tried again later);
    void notify_lost();
    // a pending present has been read (the buffer may be written again);
    void notify_released();
    // what the copy path shows is no longer the frame before (an expose
    // of a window without backing store): the next frame's scrolls are
    // copied instead of moved.
    void notify_copy_contents_lost() noexcept { copy_in_sync_ = false; }

    [[nodiscard]] PresentShimSnapshot snapshot() const noexcept;
    // Whether a direct path exists and is not off for good.
    [[nodiscard]] bool direct_available() const noexcept;

private:
    enum class DirectState : std::uint8_t {
        // No direct path.
        absent,
        // Tried on the next frame.
        ready,
        // Paused for pause_left_ frames after a refusal or loss.
        paused,
        // Refused at refused_extent_: tried again at another size.
        waiting_resize,
        // Off for the window's life.
        disabled,
    };

    void record(PresentTraceEvent event, PresentPath path, std::uint8_t detail, std::uint32_t rects,
                std::uint64_t pixels) noexcept;
    // Moves a paused or size-refused path back to ready when its condition
    // has passed (recording resume).
    void consider_resume(PresentExtent extent) noexcept;
    // The state after a refusal or loss.
    void apply_refusal(DirectRefusal refusal, PresentExtent extent) noexcept;
    // Releases the direct path's buffers if it holds any, recording it.
    void release_direct() noexcept;
    // The direct attempt of present(); nullopt when the frame must go to
    // the copy path (a fallback recorded where one happened).
    std::optional<PresentReport> present_direct(const PresentFrame& frame);
    PresentReport present_copy(const PresentFrame& frame, bool whole_frame);

    PresentationCopyPath* copy_;
    std::unique_ptr<PresentationDirectPath> direct_;
    PresentTrace* trace_;
    DirectState state_{DirectState::absent};
    std::uint32_t pause_left_{};
    PresentExtent refused_extent_{};
    std::uint32_t consecutive_losses_{};
    // The direct path holds buffers that may show (released on a fallback).
    bool direct_holds_{};
    // A grant is held (producer mode) and its age.
    bool holding_{};
    DirectBuffer held_{};
    // A present is being read by the window system.
    bool pending_{};
    // A loss arrived between frames (notify_lost): the first copy frame
    // after it records the fallback.
    bool loss_fallback_pending_{};
    // The path that presented last; a change writes the whole frame.
    PresentPath last_path_{PresentPath::none};
    // What the copy path shows is the frame it presented last.
    bool copy_in_sync_{};
    std::uint64_t frame_{};
    PresentDamageHistory history_{};
    // The extent of the last direct present: a buffer granted at another
    // extent has unknown contents whatever age its path reports, and the
    // history of the old size is forgotten (A-gf-present-01-F3).
    PresentExtent direct_extent_{};
    PresentShimSnapshot counters_{};
    // Reused per frame: the clipped damage, the rectangles written and
    // those presented.
    std::array<PresentRect, maximum_frame_rects> frame_rects_{};
    std::array<PresentRect, maximum_frame_rects> write_rects_{};
    std::array<PresentRect, maximum_frame_rects> present_rects_{};
};

} // namespace gui_forms::host::presentation
