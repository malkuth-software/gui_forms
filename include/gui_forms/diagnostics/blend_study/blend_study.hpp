#pragma once

// The blend and opaque study (presentation and blending programme,
// 2026-11-16): per-window, per-frame counters of how a frame's device pixels
// were written, and a heat map of them for the diagnostic overlay.
//
// What is counted. After each paint the window walks the frame's recorded
// commands (the transaction replayed into the host's painter) with the
// painter's own answers about opacity, and charges every draw to the device
// pixels its bounds cover inside the clip in force:
// - copied: written from an opaque source with nothing beneath showing
//   through (an opaque rectangle fill, gradient, image or part raster, the
//   opaque part of a live surface, a copy group's composite);
// - blended: written over what lay beneath (partial alpha, coverage
//   antialiasing, text, shadows, translucent images and groups);
// - overdraw: device pixels a copy replaced that an earlier draw of the same
//   frame had already written, and the writes it so hid;
// - the overdraw depth histogram: for each painted device pixel at the end
//   of the frame, the writes since the last copy that hid what lay beneath
//   (bin 0: none; bin 15: 15 or more);
// - opaque fills issued with a blend mode: fills whose colour has alpha 255,
//   which every painter call issues as source-over;
// - opaque images drawn blended: images whose decoded pixels are all opaque
//   but which the painter draws as a premultiplied (blended) image.
//
// The counts are by device bounding box, not exact coverage: a shape other
// than a rectangle is charged its bounds as blended (its edge is
// antialiased), except for the interior a rounded rectangle proves; text is
// charged its measured line box. Pixels a host moves with a scroll copy are
// not counted. Draws inside an isolated group are charged where the group
// lands, and the group's composite is charged again when it ends.
//
// Cost. Off (the default), a paint pays one relaxed atomic load. On, every
// painted frame pays: one byte of depth and one byte of class per window
// device pixel, kept and reused (allocated when the study first counts and
// when the window grows); clearing both inside the painted rectangles; one
// pass over the frame's commands, measuring text runs again; and, per draw,
// one byte read and written per covered device pixel of each plane. That is
// of the order of the painting itself, so frame timings are taken with the
// study off (tools/blend_study.py runs each workload both ways).
//
// Switched at run time: the process switch (set_blend_study_default, read
// at first from the environment variable GUI_FORMS_DEBUG_BLEND) and a
// window's own setting (Window::set_blend_study). GUI_FORMS_DEBUG_BLEND
// unset, empty or "0" is off; "depth" shows the overdraw heat map, "blend"
// the blended-against-copied heat map, and any other value counts only.

#include "gui_forms/diagnostics/invalidation_overlay/invalidation_overlay.hpp"
#include "gui_forms/export/application_export.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gui_forms {

// What the study does for a window: nothing, count, or count and show one
// heat map at present time.
enum class BlendStudyMode : std::uint8_t {
    off,
    counters,
    // Each device pixel tinted by its depth: none for one write, then blue,
    // green, yellow, orange and red from two writes to six or more.
    overdraw_heat_map,
    // Each written device pixel tinted green where only copies wrote it,
    // red where a blended write was its last, orange where a copy was laid
    // and something blended over it.
    blend_heat_map
};

// The depth histogram's bins: bin d counts device pixels written d times
// since the last copy that hid what lay beneath; the last bin also counts
// every deeper pixel.
inline constexpr std::size_t blend_depth_bins = 16U;

// The largest window the study counts, in device pixels (a 16384 x 16384
// client): a larger one counts nothing (BlendFrameCounters::refused).
inline constexpr std::uint64_t maximum_blend_study_pixels = 16384ULL * 16384ULL;

// One painted frame's counts, or a sum of frames' (add_blend_frame). Pixel
// counts are device pixels; a pixel written twice counts twice.
struct GUI_FORMS_API BlendFrameCounters final {
    std::uint64_t copied_pixels{};
    std::uint64_t blended_pixels{};
    // Device pixels a copy replaced after an earlier draw of the frame had
    // written them, and the earlier writes those copies hid.
    std::uint64_t overdraw_pixels{};
    std::uint64_t overdraw_writes{};
    // Over the painted rectangles at the end of the frame.
    std::array<std::uint64_t, blend_depth_bins> depth_histogram{};
    std::uint64_t opaque_fills_blended{};
    std::uint64_t opaque_images_blended{};
    // Drawing commands charged, and text runs charged no area because their
    // extent was unknown (prepared text).
    std::uint64_t draws_counted{};
    std::uint64_t text_runs_unmeasured{};
    // Frames not counted: the window was larger than
    // maximum_blend_study_pixels, or the planes could not be allocated.
    std::uint64_t refused{};
    // The paint pass that drew the frame, counting included.
    std::uint64_t paint_nanoseconds{};
};

// Adds `frame` to `totals`, field by field (the histogram bin by bin).
GUI_FORMS_API void add_blend_frame(BlendFrameCounters& totals, const BlendFrameCounters& frame) noexcept;

// The counters as one JSON object, field names as declared, the histogram
// as an array.
[[nodiscard]] GUI_FORMS_API std::string blend_frame_counters_json(const BlendFrameCounters& counters);

// The study's log, for programs measured without code of their own (the
// product procedure of tools/blend_study.py): when the environment variable
// GUI_FORMS_BLEND_STUDY_LOG names a file, every frame any window counts is
// appended to it as one JSON line, {"window":<its root's stable id>,
// "frame":<the window's frame number>,"counters":<blend_frame_counters_json>}.
// The file is opened on the first counted frame (append mode) and each line
// is flushed; lines from windows on several threads are serialized. Unset,
// empty or not openable, nothing is written. Any thread.
GUI_FORMS_API void append_blend_study_log(std::string_view window, std::uint64_t frame,
                                          const BlendFrameCounters& counters) noexcept;

// A window's study: its mode, the frames counted since the window was made
// or reset_blend_study, the last of them and their sum.
struct GUI_FORMS_API BlendStudySnapshot final {
    BlendStudyMode mode{BlendStudyMode::off};
    std::uint64_t frames{};
    BlendFrameCounters last{};
    BlendFrameCounters totals{};
};

// GUI_FORMS_DEBUG_BLEND's request (see the top of this file).
[[nodiscard]] GUI_FORMS_API BlendStudyMode blend_study_requested_by_environment() noexcept;
// The process switch every window without a setting of its own follows; on
// the first read it is the environment's request. Any thread.
GUI_FORMS_API void set_blend_study_default(BlendStudyMode mode) noexcept;
[[nodiscard]] GUI_FORMS_API BlendStudyMode blend_study_default() noexcept;

// The class plane's bits: a pixel's last copy, and any blended write since.
inline constexpr std::uint8_t blend_class_copied = 1U;
inline constexpr std::uint8_t blend_class_blended = 2U;

// A window's planes as the last counted frames left them, borrowed from the
// window until its next paint: `height` rows of `width` bytes each, depth
// (saturating at 255) and class (blend_class_* bits). A pixel outside every
// painted rectangle keeps what the frame that last painted it left.
struct GUI_FORMS_API BlendHeatMapView final {
    BlendStudyMode mode{BlendStudyMode::off};
    std::span<const std::uint8_t> depth{};
    std::span<const std::uint8_t> classes{};
    std::uint32_t width{};
    std::uint32_t height{};
};

// Tints `target` (a copy of the presented frame, as the invalidation overlay
// composites onto) with the view's heat map, half and half, inside `clip`
// and the smaller of the two sizes only; pixels written by nothing are
// left as they are, and every tinted pixel is opaque. A view in neither
// heat-map mode changes nothing.
GUI_FORMS_API void compose_blend_heat_map(const InvalidationOverlayTarget& target, const BlendHeatMapView& map,
                                          DevicePixelRect clip) noexcept;

// How a draw that is not a copy over everything beneath writes its pixels.
enum class BlendWrite : std::uint8_t { copied, blended };

// The planes and the frame's counts (owned by the window while its study is
// on). The kernels take device rectangles already inside the planes; the
// window's walk of the frame's commands clips them (count_display_blend).
// UI thread, as the window.
class GUI_FORMS_API BlendStudyPlanes final {
public:
    // Starts a frame of `width` by `height` device pixels: sizes the planes
    // (reset to zero when the size changes; storage only grows), clears both
    // inside each painted rectangle and zeroes the frame's counts. False,
    // counting nothing until the next begin_frame, when the size is zero or
    // over maximum_blend_study_pixels or the storage cannot be had; the
    // frame is then counted as refused.
    [[nodiscard]] bool begin_frame(std::uint32_t width, std::uint32_t height,
                                   std::span<const DevicePixelRect> painted) noexcept;
    [[nodiscard]] bool active() const noexcept { return active_; }
    [[nodiscard]] std::uint32_t width() const noexcept { return width_; }
    [[nodiscard]] std::uint32_t height() const noexcept { return height_; }

    // An opaque write that hides everything beneath it in `area`: each
    // pixel's earlier writes count as overdraw, its depth becomes one and
    // its class copied. `area` lies inside the planes (clip_to_planes).
    void replace(DevicePixelRect area) noexcept;
    // A write over what lies beneath it in `area`: each pixel's depth grows
    // by one (saturating) and its class gains the write's bit.
    void layer(DevicePixelRect area, BlendWrite write) noexcept;
    // `area` within the planes, or empty.
    [[nodiscard]] DevicePixelRect clip_to_planes(DevicePixelRect area) const noexcept;

    void note_draw() noexcept { counters_.draws_counted += 1U; }
    void note_opaque_fill_blended() noexcept { counters_.opaque_fills_blended += 1U; }
    void note_opaque_image_blended() noexcept { counters_.opaque_images_blended += 1U; }
    void note_unmeasured_text() noexcept { counters_.text_runs_unmeasured += 1U; }

    // Ends the frame: fills the depth histogram over the painted rectangles
    // and returns the frame's counts. A frame begin_frame refused returns
    // only refused = 1.
    [[nodiscard]] BlendFrameCounters end_frame(std::span<const DevicePixelRect> painted) noexcept;

    // The planes for the heat map (BlendHeatMapView), in `mode`.
    [[nodiscard]] BlendHeatMapView heat_map(BlendStudyMode mode) const& noexcept;
    BlendHeatMapView heat_map(BlendStudyMode mode) const&& = delete;

    // Frees both planes; the next begin_frame allocates again.
    void release() noexcept;

private:
    // Row-major, width_ bytes a row; width_ * height_ live bytes, storage
    // possibly larger.
    std::vector<std::uint8_t> depth_{};
    std::vector<std::uint8_t> classes_{};
    std::uint32_t width_{};
    std::uint32_t height_{};
    bool active_{};
    BlendFrameCounters counters_{};
};

} // namespace gui_forms
