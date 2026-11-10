#include "gui_forms/diagnostics/shadow_frames/shadow_frames.hpp"
#include "gui_forms/live_surface.hpp"

#include "../../scheduler/request/scheduled_frame_request.hpp"
#include "gui_forms/paint_framebuffer.hpp"
#include "gui_forms/window.hpp"

#include "../../display/chunk/display_chunk.hpp"
#include "../../display/recording_painter/recording_painter.hpp"
#include "../../display/replay/replay_display_chunk.hpp"
#include "../../engine/primitives/material/material_raster.hpp"
#include "../../theme_language/diagnostics/theme_diagnostics.hpp"
#include "../overlay/overlay_attachment.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <atomic>
#include <cstddef>
#include <cstdio>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms {
namespace detail {
struct LiveSurfaceIdleWake final {
    explicit LiveSurfaceIdleWake(std::function<void()> callback) : handler(std::move(callback)) {}
    std::atomic<bool> waiting{false};
    const std::function<void()> handler;
};

class WakeIdleLiveSurface final {
public:
    explicit WakeIdleLiveSurface(const std::shared_ptr<LiveSurfaceIdleWake>& state) : state_(state) {}
    void operator()() const noexcept {
        const std::shared_ptr<LiveSurfaceIdleWake> state = state_.lock();
        if (!state || !(*state).waiting.exchange(false, std::memory_order_acq_rel)) return;
        try { (*state).handler(); }
        catch (...) { (*state).waiting.store(true, std::memory_order_release); }
    }
private:
    std::weak_ptr<LiveSurfaceIdleWake> state_{};
};

// Posted to the window's dispatcher when a live surface's frame was
// presented unpaired: tells the control's listener on the window's thread,
// after the present, unless the control released it first.
class NoticePairingMissed final {
public:
    NoticePairingMissed(std::weak_ptr<LiveSurfacePairingListener> listener, std::uint64_t content_tag) noexcept
        : listener_(std::move(listener)), content_tag_(content_tag) {}
    void operator()() const {
        const std::shared_ptr<LiveSurfacePairingListener> listener = listener_.lock();
        if (!listener) return;
        (*listener).live_surface_pairing_missed(content_tag_);
    }

private:
    std::weak_ptr<LiveSurfacePairingListener> listener_;
    std::uint64_t content_tag_{};
};

// The window rectangle in which a live surface's newest frame may differ
// from the frame its presenter last sampled. Only the next generation of the
// sampled epoch can be narrowed to the frame's own damage, because the damage
// of skipped generations is not retained here; anything else is the whole
// destination. The damage widens by one surface pixel, as a filtered, scaled
// draw also changes neighbours that sample a changed pixel.
Rect live_change_in_window(const LiveSurface& surface,
                           const std::uint64_t next_epoch,
                           const std::uint64_t next_generation,
                           const bool next_of_sampled,
                           const Rect destination) {
    if (!next_of_sampled) return destination;
    const LiveSurfaceFrame frame = surface.acquire_latest();
    if (!frame || frame.epoch() != next_epoch ||
        frame.generation() != next_generation || frame.damage().empty() ||
        frame.width() == 0U || frame.height() == 0U) {
        return destination;
    }
    const double width = static_cast<double>(frame.width());
    const double height = static_cast<double>(frame.height());
    const Rect damage = frame.damage();
    const Rect widened = Rect::intersection(
        {damage.x - 1.0, damage.y - 1.0, damage.width + 2.0, damage.height + 2.0},
        {0.0, 0.0, width, height});
    const double horizontal = destination.width / width;
    const double vertical = destination.height / height;
    const Rect changed{destination.x + widened.x * horizontal,
                       destination.y + widened.y * vertical,
                       widened.width * horizontal, widened.height * vertical};
    return changed;
}
// Replaces `bgra` with the framebuffer's pixels as tightly packed
// premultiplied BGRA rows of width * 4 bytes, the image registry's layout,
// row by row: the framebuffer is BGRA already. False, leaving
// `bgra` empty, for an empty framebuffer or one whose pixel span is shorter
// than its rows. The framebuffer is only read.
bool copy_framebuffer_bgra(PaintFramebuffer& framebuffer, std::vector<std::byte>& bgra) {
    bgra.clear();
    const std::size_t width = framebuffer.width();
    const std::size_t height = framebuffer.height();
    const std::size_t source_row = framebuffer.row_bytes();
    const std::span<std::byte> pixels = framebuffer.pixels();
    const std::size_t row = width * 4U;
    if (width == 0U || height == 0U || source_row < row || pixels.size() < source_row * height) return false;
    bgra.resize(row * height);
    for (std::size_t y = 0; y < height; ++y) {
        const std::byte* const from = pixels.data() + y * source_row;
        std::copy(from, from + row, bgra.data() + y * row);
    }
    return true;
}
} // namespace detail

void SurfaceRoot::set_framebuffer_painter(Painter* const painter) {
    require_root_thread("set_framebuffer_painter");
    framebuffer_painter_ = painter;
    // The backplane replay's framebuffer belongs to the previous renderer.
    backplane_framebuffer_.reset();
    backplane_framebuffer_scale_ = 0.0;
}

// Four stages: validate the request; record the subtree's planes into one
// display chunk; replay that chunk into an offscreen framebuffer; copy the
// pixels into the image registry as premultiplied BGRA.
std::optional<ControlSnapshot> SurfaceRoot::snapshot_control(const Control::Ptr& control, const double scale) {
    require_root_thread("snapshot_control");
    if (in_paint_ || framebuffer_painter_ == nullptr || !control) return std::nullopt;
    if (!(*control).is_alive() || (*control).surface_root_ != this || !(*control).effectively_visible()) return std::nullopt;
    if (!std::isfinite(scale) || scale <= 0.0 || scale > 8.0) return std::nullopt;
    const Rect bounds = absolute_bounds_of(*control);
    if (bounds.empty() || !bounds.finite()) return std::nullopt;
    const Size logical{bounds.width, bounds.height};
    const std::unique_ptr<PaintFramebuffer> framebuffer = create_framebuffer(logical, scale);
    if (!framebuffer) return std::nullopt;
    // Record: the subtree's planes, each in order, into one command list in
    // the subtree's own coordinates, its materials baked at the snapshot's
    // scale.
    detail::RecordingPainter candidate(text_metrics_provider_, DeviceScale{scale});
    std::uint64_t visited_nodes = 0;
    std::uint64_t painted_controls = 0;
    std::uint64_t consumed_invalidations = 0;
    std::uint64_t chunks_rebuilt = 0;
    std::uint64_t chunks_reused = 0;
    std::uint64_t commands_replayed = 0;
    candidate.save();
    candidate.translate({-bounds.x, -bounds.y});
    for (std::size_t index = 0; index < paint_plane_count; ++index) {
        const PaintPlane plane = static_cast<PaintPlane>(index);
        paint_recursive(control, candidate, bounds, plane, visited_nodes, painted_controls, consumed_invalidations,
                        chunks_rebuilt, chunks_reused, commands_replayed);
    }
    candidate.restore();
    const Rect local{0.0, 0.0, logical.width, logical.height};
    const std::shared_ptr<const detail::DisplayChunk> recorded =
        candidate.finish(display_generation_, PaintPlane::control, local);
    // Rebuilt chunks consumed their paint dirt without reaching the screen.
    if (consumed_invalidations > 0U && (*control).is_alive()) mark_subtree_dirty(*control, Dirty::paint);
    // Replay into the offscreen raster.
    if (!(*framebuffer).begin(image_resources(), local)) return std::nullopt;
    static_cast<void>(detail::replay_display_chunk(*recorded, (*framebuffer).painter()));
    (*framebuffer).end();
    // Register the pixels in the registry's layout.
    std::vector<std::byte> bgra;
    if (!detail::copy_framebuffer_bgra(*framebuffer, bgra)) return std::nullopt;
    const std::uint32_t width = (*framebuffer).width();
    const std::uint32_t height = (*framebuffer).height();
    const std::uint64_t row = static_cast<std::uint64_t>(width) * 4U;
    const ImageLoadResult loaded = (*resources_).registry().load_bgra32_premultiplied(width, height, row, bgra);
    if (!loaded) return std::nullopt;
    ControlSnapshot snapshot{};
    snapshot.image = loaded.image;
    snapshot.logical_size = logical;
    snapshot.scale = scale;
    snapshot.pixel_width = width;
    snapshot.pixel_height = height;
    return snapshot;
}

bool SurfaceRoot::release_control_snapshot(ImageId image) {
    require_root_thread("release_control_snapshot");
    // The repainting removal: a stamp that showed the picture may still be
    // in a retained chunk.
    const bool removed = (*resources_).remove_image(image);
    return removed;
}

std::unique_ptr<PaintFramebuffer> SurfaceRoot::create_framebuffer(const Size logical_size, const double scale) {
    require_root_thread("create_framebuffer");
    std::unique_ptr<PaintFramebuffer> result{};
    if (framebuffer_painter_ != nullptr) {
        result = (*framebuffer_painter_).create_framebuffer(logical_size, scale);
    }
    return result;
}

void SurfaceRoot::collect_paint_checkpoints(
    const Control::Ptr& control, PaintControlCheckpointList& checkpoints) {
    if (!control) return;
    checkpoints.push_back({control, (*control).display_chunk_});
    for (const Control::Ptr& child : (*control).children_) {
        collect_paint_checkpoints(child, checkpoints);
    }
}

void SurfaceRoot::abandon_paint(std::uint64_t generation_before) {
    for (const PaintControlCheckpoint& checkpoint : paint_checkpoints_) {
        if (!checkpoint.control || !(*checkpoint.control).is_alive()) continue;
        (*checkpoint.control).display_chunk_ = checkpoint.chunk;
        (*checkpoint.control).dirty_ |= Dirty::paint;
    }
    paint_checkpoints_.clear();
    const std::span<const Rect> painted_rectangles = painted_rectangles_;
    if (root_ && (*root_).is_alive()) {
        static_cast<void>(recompute_subtree_dirty(root_));
    }
    display_generation_ = generation_before;
    for (std::size_t index = 0; index < painted_rectangles.size(); ++index) {
        add_damage_all_planes(painted_rectangles[index]);
    }
    dirty_after_render_ = true;
    in_paint_ = false;
    ++paint_leases_abandoned_;
    update_display_cache_metrics();
    if (!root_ || !(*root_).is_alive()) {
        abandon_deferred_input();
    } else {
        schedule_deferred_input_drain();
    }
}

namespace {

// `rect` grown outward to whole device pixels at `scale`. A painted
// rectangle covers whole pixels, so every drawing under its clip repaints
// each pixel it touches entirely: a part raster's composite and an
// anti-aliased fill agree there, and no partly covered pixel blends the new
// frame with the old one.
Rect device_aligned(Rect rect, double scale) noexcept {
    if (rect.empty() || !std::isfinite(scale) || scale <= 0.0) return rect;
    const double left = std::floor(rect.x * scale) / scale;
    const double top = std::floor(rect.y * scale) / scale;
    const double right = std::ceil((rect.x + rect.width) * scale) / scale;
    const double bottom = std::ceil((rect.y + rect.height) * scale) / scale;
    const Rect aligned{left, top, right - left, bottom - top};
    return aligned;
}

} // namespace

Rect SurfaceRoot::plane_paint_bounds(std::size_t index, Rect rectangle,
                                bool explicit_request) const {
    if (explicit_request) return rectangle;
    // Plane damage grows to whole device pixels like the painted rectangle
    // it is clipped to, so no plane paints part of a pixel it covers.
    const Rect damage = device_aligned(plane_damage_[index].bounds(), scale_);
    const Rect plane = Rect::intersection(damage, rectangle);
    return plane;
}

bool SurfaceRoot::has_plane_damage() const noexcept {
    for (const DamageRegion& damage : plane_damage_) {
        if (!damage.empty()) return true;
    }
    return false;
}

bool SurfaceRoot::paint_pending() const noexcept {
    if (dirty_after_render_) return true;
    if (control_table_.pending(detail::DirtyKind::paint) || !pending_local_damage_.empty() ||
        !pending_content_gathers_.empty()) {
        return true;
    }
    const bool damaged = has_plane_damage();
    return damaged;
}

namespace {

// Rectangles closer than this, in logical units, paint as one: replaying a
// hairline gap costs less than replaying the chunks that straddle it twice.
constexpr double damage_merge_distance = 2.0;

bool nearly_touch(Rect left, Rect right) noexcept {
    const Rect grown{left.x - damage_merge_distance, left.y - damage_merge_distance,
                     left.width + damage_merge_distance * 2.0,
                     left.height + damage_merge_distance * 2.0};
    const bool touching = !Rect::intersection(grown, right).empty();
    return touching;
}

// Clips `input` to `window_bounds`, each grown to whole device pixels at
// `scale`, merges rectangles that overlap or nearly touch until none do,
// and fills `merged`. A set larger than the damage region's capacity
// collapses to its bounds, as DamageRegion does.
void merge_paint_rectangles(std::span<const Rect> input, Rect window_bounds, double scale,
                            std::vector<Rect>& merged) {
    merged.clear();
    merged.reserve(input.size());
    for (std::size_t index = 0; index < input.size(); ++index) {
        const Rect clipped = Rect::intersection(input[index], window_bounds);
        if (clipped.empty()) continue;
        const Rect aligned = Rect::intersection(device_aligned(clipped, scale), window_bounds);
        merged.push_back(aligned);
    }
    bool changed = true;
    while (changed) {
        changed = false;
        for (std::size_t first = 0; first < merged.size() && !changed; ++first) {
            for (std::size_t second = first + 1U; second < merged.size(); ++second) {
                if (!nearly_touch(merged[first], merged[second])) continue;
                merged[first] = Rect::united(merged[first], merged[second]);
                merged.erase(merged.begin() + static_cast<std::ptrdiff_t>(second));
                changed = true;
                break;
            }
        }
    }
    if (merged.size() > DamageRegion::maximum_rectangles) {
        Rect bounds = merged.front();
        for (std::size_t index = 1; index < merged.size(); ++index) {
            bounds = Rect::united(bounds, merged[index]);
        }
        merged.clear();
        merged.push_back(bounds);
    }
}

bool covered_by(Rect pending, std::span<const Rect> painted) noexcept {
    for (std::size_t index = 0; index < painted.size(); ++index) {
        if (painted[index].contains(pending)) return true;
    }
    return false;
}

} // namespace

void SurfaceRoot::note_inline_with_theme(const Control& control) {
    if (inline_with_theme_reported_) return;
    inline_with_theme_reported_ = true;
    theme_language::ThemeDiagnostic warning{};
    warning.code = theme_language::DiagnosticCode::inline_with_theme;
    warning.file = "inline:" + std::string(control.stable_id().value());
    theme_language::ThemeDiagnosticList list{};
    list.add(std::move(warning));
    theme_language::write_theme_diagnostics(list, stderr);
}

std::optional<PartRecordHandle> SurfaceRoot::note_engine_part(const Control& control, const PartKey& key,
                                                         AnatomyStateSet states) {
    const PartRecordAcquisition acquired = engine_parts_.acquire(control.stable_id().value(), key);
    if (acquired.created) metrics_.record_engine_part_created(key.index_fallback(), engine_parts_.record_count());
    if (acquired.evicted > 0U) metrics_.record_engine_parts_evicted(acquired.evicted, engine_parts_.record_count());
    EnginePartRecord* const record = engine_parts_.record(acquired.handle);
    if (record == nullptr) return std::nullopt;
    // time_in_state counts from the paint that first shows new states.
    if (acquired.created || !((*record).states == states)) (*record).state_entered = motion_clock_.now();
    (*record).states = states;
    // The paint in progress, counted as the paints completed before it.
    (*record).last_painted = paint_leases_completed_;
    return acquired.handle;
}

PartRasterResult SurfaceRoot::engine_part_raster(PartRecordHandle handle, const PartRasterRequest& request) {
    const PartRasterResult result = engine_parts_.raster_for(handle, request, paint_leases_completed_);
    // A procedural pattern it asked for is generated in an idle frame (E10).
    if (engine_parts_.patterns_waiting()) arm_engine_idle();
    return result;
}

void SurfaceRoot::engine_queue_keyframe(const PartRasterRequest& request) {
    engine_parts_.queue_idle_keyframe(request);
    if (engine_parts_.idle_keyframes_waiting()) arm_engine_idle();
}

void SurfaceRoot::arm_engine_idle() {
    for (const std::shared_ptr<gui_forms::detail::ScheduledFrameRequest>& scheduled : frame_requests_) {
        if ((*scheduled).connected() && (*scheduled).kind == detail::FrameRequestKind::engine_idle) return;
    }
    compact_frame_requests();
    if (frame_requests_.size() >= maximum_scheduled_frame_requests) return;
    const FrameTime first = motion_clock_.now() + motion_frame_interval;
    std::shared_ptr<gui_forms::detail::ScheduledFrameRequest> idle = std::make_shared<detail::ScheduledFrameRequest>(
        detail::FrameRequestKind::engine_idle, Control::WeakPtr{}, first, motion_frame_interval);
    frame_requests_.push_back(std::move(idle));
    metrics_.record_frame_request();
}

PartRasterResult SurfaceRoot::engine_unkeyed_raster(const PartRasterRequest& request) {
    const PartRasterResult result = engine_parts_.raster_without_record(request, paint_leases_completed_);
    if (engine_parts_.patterns_waiting()) arm_engine_idle();
    return result;
}

void SurfaceRoot::paint_backplane_rectangle(detail::RecordingPainter& candidate, const Rect rectangle,
                                       const SurfaceMaterial& backplane, const bool starts_clear) {
    const Rect window_bounds{0.0, 0.0, client_size_.width, client_size_.height};
    candidate.save();
    candidate.clip_rect(rectangle);
    if (starts_clear) {
        PaintGroup cleared{};
        cleared.bounds = rectangle;
        cleared.composite = PaintGroupComposite::copy;
        cleared.isolated = true;
        candidate.begin_group(cleared);
        paint_backplane_material(candidate, window_bounds, backplane);
        candidate.end_group();
    } else {
        paint_backplane_material(candidate, window_bounds, backplane);
    }
    candidate.restore();
}

void SurfaceRoot::paint_backplane_material(detail::RecordingPainter& candidate, const Rect window_bounds,
                                           const SurfaceMaterial& backplane) {
    const bool bakes = !window_bounds.empty() && valid_surface_material(backplane) && backplane.finish == 0.0 &&
                       detail::material_painting(backplane) == detail::MaterialPainting::kernel;
    if (!bakes) {
        paint_surface_material(candidate, window_bounds, backplane);
        return;
    }
    const std::optional<SurfaceMaterial> stripped = detail::material_as_painted(backplane);
    const SurfaceMaterial& material = stripped ? *stripped : backplane;
    const Size size{window_bounds.width, window_bounds.height};
    const bool kept = backplane_baked_raster_ && *backplane_baked_material_ == material &&
                      backplane_baked_size_ == size && (*backplane_baked_raster_).scale() == DeviceScale{scale_};
    if (!kept) {
        backplane_baked_raster_.reset();
        backplane_baked_material_.reset();
        const double outset = detail::material_raster_outset(material);
        std::shared_ptr<const PartRaster> raster =
            detail::bake_material_raster(material, size, DeviceScale{scale_}, outset);
        if (!raster) {
            paint_surface_material(candidate, window_bounds, backplane);
            return;
        }
        backplane_baked_raster_ = std::move(raster);
        backplane_baked_material_ = std::make_shared<const SurfaceMaterial>(material);
        backplane_baked_size_ = size;
        backplane_baked_outset_ = outset;
    }
    candidate.draw_baked_surface_material(window_bounds, backplane_baked_material_, backplane_baked_raster_,
                                          backplane_baked_outset_);
}

std::size_t SurfaceRoot::covering_plane(const Rect rectangle, const bool explicit_request,
                                   std::vector<Control::Ptr>& path) const {
    for (std::size_t index = paint_plane_count; index > 0U; --index) {
        const std::size_t plane_index = index - 1U;
        // A plane paints the whole rectangle when asked to or when its
        // damage holds it (plane_paint_bounds).
        const bool whole =
            explicit_request || device_aligned(plane_damage_[plane_index].bounds(), scale_).contains(rectangle);
        if (!whole) continue;
        path.clear();
        if (find_paint_cover(root_, rectangle, static_cast<PaintPlane>(plane_index), path)) return plane_index;
    }
    return paint_plane_count;
}

void SurfaceRoot::paint_plane_rectangle(Painter& candidate, const std::size_t index, const Rect rectangle,
                                   const bool explicit_request, std::vector<Control::Ptr>& path,
                                   std::uint64_t& visited_nodes, std::uint64_t& painted_controls,
                                   std::uint64_t& consumed_invalidations, std::uint64_t& chunks_rebuilt,
                                   std::uint64_t& chunks_reused, std::uint64_t& commands_replayed) {
    const Rect plane_bounds = plane_paint_bounds(index, rectangle, explicit_request);
    if (plane_bounds.empty()) return;
    const PaintPlane plane = static_cast<PaintPlane>(index);
    // From the control that covers what the plane paints here, when one does.
    path.clear();
    if (find_paint_cover(root_, plane_bounds, plane, path)) {
        paint_from_cover(path, candidate, plane_bounds, plane, visited_nodes, painted_controls,
                         consumed_invalidations, chunks_rebuilt, chunks_reused, commands_replayed);
        return;
    }
    paint_recursive(root_, candidate, plane_bounds, plane, visited_nodes, painted_controls,
                    consumed_invalidations, chunks_rebuilt, chunks_reused, commands_replayed);
}

void SurfaceRoot::note_opaque_cover(const Control& control, const Painter& painter, const Rect local) noexcept {
    const Painter* const recorder = recording_painter_;
    if (recording_control_ != &control || recorder != &painter) return;
    (*recording_painter_).mark_opaque_cover(local);
}

std::optional<PaintReceipt> SurfaceRoot::paint(Painter& painter,
                                          Rect requested_damage) {
    settle_dirty();
    // One rectangle: the request, or else the bounds of all damage.
    if (!requested_damage.empty()) {
        const std::array<Rect, 1> request{requested_damage};
        return paint_rectangles(painter, request, true);
    }
    DamageRegion aggregate;
    for (const DamageRegion& plane : plane_damage_) {
        for (const Rect rect : plane.rectangles()) {
            aggregate.add(rect);
        }
    }
    const std::array<Rect, 1> bounds{aggregate.bounds()};
    return paint_rectangles(painter, bounds, false);
}

std::optional<PaintReceipt> SurfaceRoot::paint(Painter& painter,
                                          std::span<const Rect> requested_damage) {
    settle_dirty();
    if (!requested_damage.empty()) {
        return paint_rectangles(painter, requested_damage, true);
    }
    std::vector<Rect> pending;
    for (const DamageRegion& plane : plane_damage_) {
        const std::span<const Rect> rectangles = plane.rectangles();
        pending.insert(pending.end(), rectangles.begin(), rectangles.end());
    }
    return paint_rectangles(painter, pending, false);
}

std::optional<PaintReceipt> SurfaceRoot::paint_rectangles(
    Painter& painter, std::span<const Rect> rectangles, bool explicit_request) {
    require_root_thread("paint");
    if (in_paint_) {
        // Refresh/Update/native paint recursion requests a later pass. It must
        // never re-enter application painting while the exclusive lease is held.
        ++reentrant_paint_requests_deferred_;
        dirty_after_render_ = true;
        add_damage_all_planes({0.0, 0.0, client_size_.width, client_size_.height});
        request_paint_wake();
        return std::nullopt;
    }
    if (occluded_) return std::nullopt;
    // The lease this paint starts, when it paints anything.
    const ShadowFrameScope shadow_frame(ShadowFrameKind::paint_pass, (*root_).stable_id().value(), std::string_view{},
                                        paint_leases_started_ + 1U);
    ensure_layout(true);

    if (!(*root_).is_alive()) {
        return std::nullopt;
    }

    const Rect window_bounds{0.0, 0.0, client_size_.width, client_size_.height};
    std::vector<Rect>& painted = painted_rectangles_;
    merge_paint_rectangles(rectangles, window_bounds, scale_, painted);
    if (painted.empty()) {
        return std::nullopt;
    }
    Rect paint_bounds = painted.front();
    double painted_area = 0.0;
    for (std::size_t index = 0; index < painted.size(); ++index) {
        paint_bounds = Rect::united(paint_bounds, painted[index]);
        painted_area += painted[index].area();
    }

    paint_checkpoints_.clear();
    collect_paint_checkpoints(root_, paint_checkpoints_);
    for (const std::shared_ptr<detail::OverlayAttachment>& overlay : overlays_) {
        collect_paint_checkpoints((*overlay).overlay(), paint_checkpoints_);
    }

    const std::uint64_t lease_revision = content_revision_;
    const std::uint64_t lease_epoch = surface_epoch_;
    const std::uint64_t generation_before = display_generation_;
    ++paint_leases_started_;
    in_paint_ = true;
    dirty_after_render_ = false;
    std::uint64_t visited_nodes = 0;
    std::uint64_t painted_controls = 0;
    std::uint64_t consumed_invalidations = 0;
    std::uint64_t chunks_rebuilt = 0;
    std::uint64_t chunks_reused = 0;
    std::uint64_t commands_replayed = 0;
    try {
        // Application callbacks and retained chunk rebuilding record into a
        // complete candidate command list. No candidate command reaches the
        // host raster until every callback has returned successfully.
        detail::RecordingPainter candidate(text_metrics_provider_, DeviceScale{scale()});
        // A top-level retained surface owns an explicit backplane even when
        // its application root is a transparent layout container. Replaying
        // only children cannot erase pixels formerly occupied by an overlay
        // in the gaps between those children. Resolve the immutable Window
        // recipe in full-window coordinates, then clip it to each painted
        // rectangle so partial gradient repainting never shifts its authored
        // geometry.
        ControlVisualContext backplane_context;
        backplane_context.surface = active_
            ? ControlSurfaceState::normal
            : ControlSurfaceState::deactivated;
        backplane_context.high_contrast = presentation_settings_.high_contrast;
        backplane_context.dark = presentation_settings_.colour_scheme == ColourScheme::dark;
        // Over a non-opaque grant, the theme's base for it (stage T5).
        const SurfaceMaterial backplane = backplane_base(backplane_context);
        // A window granted a non-opaque backdrop (stage T5) starts each
        // painted rectangle clear: its backplane is drawn in a copy group,
        // which replaces the previous frame's pixels, transparent included,
        // instead of blending over them. An opaque window draws exactly as
        // before, so its presented bytes are unchanged.
        const bool starts_clear = backdrop_grant_.granted != WindowBackdrop::opaque;
        // The opaque cover hint (owner, 2026-10-28). Where a control's
        // current display chunk leaves a whole painted rectangle opaque in a
        // plane that paints all of it, the backplane and the earlier planes
        // lie beneath it there: that rectangle starts at that plane, its
        // `first_plane`, and paint_plane_count means none covers it. Only
        // an opaque window: a clear one starts each rectangle transparent.
        std::vector<std::size_t> first_plane(painted.size(), paint_plane_count);
        std::vector<Control::Ptr> cover_path;
        for (std::size_t index = 0; index < painted.size(); ++index) {
            if (!starts_clear) first_plane[index] = covering_plane(painted[index], explicit_request, cover_path);
            if (first_plane[index] == paint_plane_count) {
                paint_backplane_rectangle(candidate, painted[index], backplane, starts_clear);
            }
        }
        std::vector<Control::Ptr> overlay_roots;
        overlay_roots.reserve(overlays_.size());
        for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) overlay_roots.push_back((*overlay).overlay());
        // Complete the application root across every paint plane before any
        // overlay root. An overlay is one composited retained surface; interleaving
        // its backplane with later application planes lets ordinary content
        // paint over the overlay root, leaving visible only those of its pixels
        // that lie in the overlay paint plane.
        // Painted rectangles are disjoint, so each is a separate clip; a
        // chunk that touches several is replayed once per rectangle and
        // rebuilt at most once.
        for (std::size_t index = 0; index < paint_plane_count; ++index) {
            const PaintPlane plane = static_cast<PaintPlane>(index);
            for (std::size_t rectangle = 0; rectangle < painted.size(); ++rectangle) {
                const std::size_t first = first_plane[rectangle];
                if (first != paint_plane_count && index < first) continue;
                if (index == first) {
                    // A callback since may have changed the cover: then
                    // what it skipped is painted first.
                    cover_path.clear();
                    if (!find_paint_cover(root_, painted[rectangle], plane, cover_path)) {
                        paint_backplane_rectangle(candidate, painted[rectangle], backplane, starts_clear);
                        for (std::size_t earlier = 0; earlier < index; ++earlier) {
                            paint_plane_rectangle(candidate, earlier, painted[rectangle], explicit_request,
                                                  cover_path, visited_nodes, painted_controls,
                                                  consumed_invalidations, chunks_rebuilt, chunks_reused,
                                                  commands_replayed);
                        }
                    }
                }
                paint_plane_rectangle(candidate, index, painted[rectangle], explicit_request, cover_path,
                                      visited_nodes, painted_controls, consumed_invalidations, chunks_rebuilt,
                                      chunks_reused, commands_replayed);
            }
        }
        // Window-owned overlay roots are composited after all application
        // content, in opening order and independently of consumer layout.
        for (const Control::Ptr& overlay : overlay_roots) {
            if (!overlay || !(*overlay).is_alive() || (*overlay).surface_root_ != this) {
                continue;
            }
            for (std::size_t index = 0; index < paint_plane_count; ++index) {
                const PaintPlane plane = static_cast<PaintPlane>(index);
                for (std::size_t rectangle = 0; rectangle < painted.size(); ++rectangle) {
                    const Rect plane_bounds =
                        plane_paint_bounds(index, painted[rectangle], explicit_request);
                    if (plane_bounds.empty()) continue;
                    paint_recursive(overlay, candidate, plane_bounds, plane,
                                    visited_nodes, painted_controls,
                                    consumed_invalidations, chunks_rebuilt,
                                    chunks_reused, commands_replayed);
                }
            }
        }
        // The outer band over every plane, overlays included (owner,
        // 2026-11-08): what lies beneath it in the band shows only where
        // its colour lets it.
        paint_band(candidate, painted);
        const std::shared_ptr<const detail::DisplayChunk> transaction =
            candidate.finish(
            display_generation_, PaintPlane::control, window_bounds);
        if (lease_epoch != surface_epoch_ || !(*root_).is_alive()) {
            abandon_paint(generation_before);
            return std::nullopt;
        }
        // The frame's recording is replayed into the terminal painter, which
        // alone can say whether an opaque live surface hides what lies
        // beneath it (a panned canvas's sheet over its surround).
        static_cast<void>(detail::replay_display_chunk_occluded(*transaction, painter, window_bounds));
        // A backend painter may enter a native callback while replaying. A
        // resize, scale transition, or owner retirement at that boundary
        // invalidates the candidate even though every draw command returned.
        // The host receives no receipt and therefore cannot publish it.
        if (lease_epoch != surface_epoch_ || !(*root_).is_alive()) {
            abandon_paint(generation_before);
            return std::nullopt;
        }
    } catch (...) {
        abandon_paint(generation_before);
        throw;
    }

    // The pass is complete: the checkpoints release their controls.
    paint_checkpoints_.clear();
    in_paint_ = false;
    ++paint_leases_completed_;
    rendered_revision_ = lease_revision;

    // Damage that arrived while callbacks were running belongs to the next
    // revision. Never consume it as though it were part of this lease. A
    // plane's damage is consumed only when a painted rectangle covers each
    // of its pending rectangles.
    const bool changed_while_rendering =
        dirty_after_render_ || content_revision_ != lease_revision;
    if (!changed_while_rendering) {
        for (std::size_t index = 0; index < paint_plane_count; ++index) {
            const std::span<const Rect> pending = plane_damage_[index].rectangles();
            bool covered = true;
            for (std::size_t rectangle = 0; rectangle < pending.size(); ++rectangle) {
                const Rect visible = Rect::intersection(pending[rectangle], window_bounds);
                if (!visible.empty() && !covered_by(visible, painted)) covered = false;
            }
            if (covered) plane_damage_[index].clear();
        }
    }

    const bool full_window = painted_area >= window_bounds.area();
    metrics_.record_paint(visited_nodes, painted_controls, consumed_invalidations,
                          chunks_rebuilt, chunks_reused, commands_replayed,
                          painted_area, paint_bounds.area(),
                          static_cast<std::uint64_t>(painted.size()), full_window);
    update_display_cache_metrics();

    schedule_deferred_input_drain();
    return PaintReceipt{lease_revision, lease_epoch};
}

bool SurfaceRoot::notify_presented(PaintReceipt receipt,
                              std::uint64_t duration_nanoseconds) {
    const PresentedArea unreported;
    return notify_presented(receipt, duration_nanoseconds, unreported);
}

void Window::notify_live_presented(std::uint64_t duration_nanoseconds,
                                   PresentedArea presented) {
    require_root_thread("live surface presentation report");
    metrics_.record_screen_copy(presented.copy_nanoseconds);
    metrics_.record_live_present(duration_nanoseconds, presented.device_pixels,
                                 presented.rectangles);
}

bool SurfaceRoot::notify_presented(PaintReceipt receipt,
                              std::uint64_t duration_nanoseconds,
                              PresentedArea presented) {
    require_root_thread("paint presentation release");
    // The copy happened whether or not the receipt is newer (a tick that only
    // presents live frames repeats the presented revision).
    metrics_.record_screen_copy(presented.copy_nanoseconds);
    const bool current_epoch = receipt.surface_epoch == surface_epoch_;
    const bool complete_revision = receipt.rendered_revision != 0U &&
        receipt.rendered_revision <= rendered_revision_;
    const bool monotonic = receipt.rendered_revision > presented_revision_;
    if (!current_epoch || !complete_revision || !monotonic) {
        ++presentation_receipts_rejected_;
        return false;
    }
    presented_revision_ = receipt.rendered_revision;
    ++presentation_receipts_accepted_;
    metrics_.record_present(duration_nanoseconds, presented.device_pixels,
                            presented.rectangles);
    return true;
}

void SurfaceRoot::notify_presented(std::uint64_t duration_nanoseconds) {
    static_cast<void>(notify_presented(
        PaintReceipt{rendered_revision_, surface_epoch_}, duration_nanoseconds));
}

void Window::declare_presentation(PresentationDeclaration declaration) {
    if (declaration.contract_version < paint_contract_version) {
        throw std::invalid_argument(
            "GUI.Forms host does not meet the paint contract and cannot attach");
    }
    // Contract version 2 includes damage-preserving partial present.
    if (declaration.contract_version >= 2U && !declaration.extensions.partial_present) {
        throw std::invalid_argument(
            "GUI.Forms paint contract 2 requires presenting rectangle sets");
    }
    presentation_declaration_ = declaration;
}

PresentationAvailability Window::presentation_availability() const noexcept {
    PresentationAvailability availability;
    availability.contract_version = presentation_declaration_.contract_version;
    availability.extensions = presentation_declaration_.extensions;
    availability.lease = paint_lease_snapshot();
    return availability;
}

std::string PresentationAvailability::to_json() const {
    const char* const flag_true = "true";
    const char* const flag_false = "false";
    std::string json = "{\"contract_version\":" + std::to_string(contract_version) +
        ",\"extensions\":{\"partial_present\":" +
        (extensions.partial_present ? flag_true : flag_false) +
        ",\"zero_copy_import\":" + (extensions.zero_copy_import ? flag_true : flag_false) +
        ",\"presentation_fence\":" + (extensions.presentation_fence ? flag_true : flag_false) +
        ",\"native_surface_lease\":" + (extensions.native_surface_lease ? flag_true : flag_false) +
        "},\"lease\":{\"content_revision\":" + std::to_string(lease.content_revision) +
        ",\"rendered_revision\":" + std::to_string(lease.rendered_revision) +
        ",\"presented_revision\":" + std::to_string(lease.presented_revision) +
        ",\"surface_epoch\":" + std::to_string(lease.surface_epoch) +
        ",\"presentation_receipts_accepted\":" +
        std::to_string(lease.presentation_receipts_accepted) +
        ",\"presentation_receipts_rejected\":" +
        std::to_string(lease.presentation_receipts_rejected) + "}}";
    return json;
}

PaintLeaseSnapshot SurfaceRoot::paint_lease_snapshot() const noexcept {
    return {paint_lease_state(), content_revision_, rendered_revision_,
            presented_revision_, surface_epoch_, paint_leases_started_,
            paint_leases_completed_, paint_leases_abandoned_,
            reentrant_paint_requests_deferred_, paint_wakes_queued_,
            paint_wakes_coalesced_, presentation_receipts_accepted_,
            presentation_receipts_rejected_, paint_wake_pending_,
            dirty_after_render_};
}

bool Window::queue_live_surface_presentation(
    const Control::Ptr& control, std::shared_ptr<LiveSurface> surface) {
    require_root_thread("live-surface presentation");
    if (!control || !surface || !(*control).is_alive() ||
        (*control).surface_root_ != this) {
        return false;
    }
    std::pair<
        std::unordered_map<std::uint64_t, LiveSurfaceRegistration>::iterator,
        bool> insertion = live_surface_registrations_.try_emplace(
            (*control).runtime_id().value,
            LiveSurfaceRegistration{control, surface});
    std::unordered_map<std::uint64_t, LiveSurfaceRegistration>::iterator entry =
        insertion.first;
    const bool inserted = insertion.second;
    if (!inserted && (*entry).second.surface != surface) {
        (*entry).second =
            LiveSurfaceRegistration{control, std::move(surface)};
    } else {
        (*entry).second.control = control;
        (*entry).second.surface = std::move(surface);
    }
    LiveSurfaceRegistration& registration = (*entry).second;
    if (live_surface_idle_wake_ && !registration.idle_wake.connected()) {
        registration.idle_wake = (*registration.surface).connect_presentation_wake(
            detail::WakeIdleLiveSurface(live_surface_idle_wake_));
        const detail::WakeIdleLiveSurface wake(live_surface_idle_wake_);
        wake();
    }
    return true;
}

bool Window::require_live_surface_content(const Control::Ptr& control,
                                          const std::uint64_t content_tag) {
    require_root_thread("live-surface content pairing");
    if (!control) return false;
    const LiveSurfaceRegistrationMap::iterator entry =
        live_surface_registrations_.find((*control).runtime_id().value);
    if (entry == live_surface_registrations_.end()) return false;
    LiveSurfaceRegistration& registration = (*entry).second;
    if (content_tag > registration.required_content_tag) {
        registration.required_content_tag = content_tag;
    }
    return true;
}

bool Window::set_live_surface_pairing_listener(const Control::Ptr& control,
                                               std::weak_ptr<LiveSurfacePairingListener> listener) {
    require_root_thread("live-surface pairing listener");
    if (!control) return false;
    const LiveSurfaceRegistrationMap::iterator entry =
        live_surface_registrations_.find((*control).runtime_id().value);
    if (entry == live_surface_registrations_.end()) return false;
    // The previous listener is released after the swap.
    std::weak_ptr<LiveSurfacePairingListener> previous = std::move(listener);
    std::swap((*entry).second.pairing_listener, previous);
    return true;
}

void Window::set_live_surface_idle_wake_handler(std::function<void()> wake) {
    require_root_thread("live-surface idle handler");
    std::shared_ptr<detail::LiveSurfaceIdleWake> replacement{};
    if (wake) replacement = std::make_shared<detail::LiveSurfaceIdleWake>(std::move(wake));
    if (live_surface_idle_wake_)
        (*live_surface_idle_wake_).waiting.store(false, std::memory_order_release);
    live_surface_idle_wake_ = std::move(replacement);
    for (LiveSurfaceRegistrationMap::value_type& entry : live_surface_registrations_) {
        LiveSurfaceRegistration& registration = entry.second;
        registration.idle_wake.disconnect();
        if (live_surface_idle_wake_) {
            registration.idle_wake = (*registration.surface).connect_presentation_wake(
                detail::WakeIdleLiveSurface(live_surface_idle_wake_));
        }
    }
}

void Window::set_live_surface_idle_waiting(const bool waiting) {
    require_root_thread("live-surface idle handshake");
    if (!live_surface_idle_wake_) return;
    (*live_surface_idle_wake_).waiting.store(waiting, std::memory_order_release);
    if (!waiting) return;
    for (const LiveSurfaceRegistrationMap::value_type& entry : live_surface_registrations_) {
        const LiveSurfaceRegistration& registration = entry.second;
        const LiveSurfaceSnapshot snapshot = (*registration.surface).snapshot();
        if (snapshot.has_frame && (snapshot.epoch != registration.observed_epoch ||
            snapshot.published_generation != registration.observed_generation)) {
            const detail::WakeIdleLiveSurface wake(live_surface_idle_wake_);
            wake();
            return;
        }
    }
}

void Window::collect_visible_overlay_rectangles(
    const Control::Ptr& control, std::vector<Rect>& rectangles) const {
    if (!control || !(*control).is_alive() || (*control).surface_root_ != this ||
        !(*control).effectively_visible()) {
        return;
    }
    if ((*control).paint_plane_ == PaintPlane::overlay) {
        const Rect bounds = Rect::intersection(
            absolute_bounds_of(*control),
            {0.0, 0.0, client_size_.width, client_size_.height});
        if (!bounds.empty()) rectangles.push_back(bounds);
    }
    for (const Control::Ptr& child : (*control).children_) {
        collect_visible_overlay_rectangles(child, rectangles);
    }
}

std::vector<Rect> Window::subtract_rectangle(Rect source, Rect cover) {
    std::vector<Rect> fragments;
    const Rect overlap = Rect::intersection(source, cover);
    if (overlap.empty()) {
        fragments.push_back(source);
        return fragments;
    }
    const double source_right = source.x + source.width;
    const double source_bottom = source.y + source.height;
    const double overlap_right = overlap.x + overlap.width;
    const double overlap_bottom = overlap.y + overlap.height;
    const Rect candidates[] = {
        {source.x, source.y, source.width, overlap.y - source.y},
        {source.x, overlap_bottom, source.width,
         source_bottom - overlap_bottom},
        {source.x, overlap.y, overlap.x - source.x, overlap.height},
        {overlap_right, overlap.y, source_right - overlap_right,
         overlap.height},
    };
    for (const Rect& candidate : candidates) {
        if (!candidate.empty()) fragments.push_back(candidate);
    }
    return fragments;
}

std::vector<LiveSurfacePresentation>
Window::take_live_surface_presentations(const bool include_unchanged,
                                        bool* const publication_changed) {
    require_root_thread("live-surface presentation drain");
    if (publication_changed != nullptr) *publication_changed = false;
    std::vector<LiveSurfacePresentation> result;
    result.reserve(live_surface_registrations_.size());

    // Direct live presentation is a fast path through retained composition,
    // not an always-on-top plane. Gather overlay ownership once, then subtract
    // those pixels from each live presentation. Uncovered spectrum/video pixels
    // therefore keep advancing while a menu is open without allowing the live
    // lane to paint over the menu.
    std::vector<Rect> overlay_rectangles;
    collect_visible_overlay_rectangles(root_, overlay_rectangles);
    for (const std::shared_ptr<gui_forms::detail::OverlayAttachment>& overlay : overlays_) {
        if (overlay) {
            collect_visible_overlay_rectangles(
                (*overlay).overlay(), overlay_rectangles);
        }
    }
    for (LiveSurfaceRegistrationMap::iterator iterator =
             live_surface_registrations_.begin();
         iterator != live_surface_registrations_.end();) {
        const Control::Ptr control = (*iterator).second.control.lock();
        if (!control || !(*control).is_alive() || (*control).surface_root_ != this ||
            !(*iterator).second.surface) {
            iterator = live_surface_registrations_.erase(iterator);
            continue;
        }
        const LiveSurfaceSnapshot snapshot = (*(*iterator).second.surface).snapshot();
        LiveSurfaceRegistration& registration = (*iterator).second;
        if (publication_changed != nullptr && snapshot.has_frame &&
            (registration.observed_epoch != snapshot.epoch ||
             registration.observed_generation != snapshot.published_generation)) {
            *publication_changed = true;
        }
        // Observation is independent of visibility and successful sampling.
        // Hidden/covered generations must not cause an endless idle rearm.
        registration.observed_epoch = snapshot.epoch;
        registration.observed_generation = snapshot.published_generation;
        if (occluded_ || !overlays_.empty() || !(*control).visible.read() ||
            !(*control).effectively_visible()) {
            // Retained painting owns these pixels until the next presentation.
            registration.placed = false;
            ++iterator;
            continue;
        }

        if (!snapshot.has_frame) {
            registration.placed = false;
            ++iterator;
            continue;
        }
        // Pairing (design note section 6, decision 6): a frame older than the
        // required content waits one present tick, keeping the previous
        // frame on screen, and is presented unpaired at the next.
        const bool content_behind = snapshot.content_tag < registration.required_content_tag;
        if (!content_behind) {
            registration.pairing_waited = false;
        } else if (!registration.pairing_waited && registration.placed) {
            registration.pairing_waited = true;
            metrics_.record_live_pairing_wait();
            ++iterator;
            continue;
        }

        const Rect destination = absolute_bounds_of(*control);
        // A host-composed live frame never covers the outer band: it is
        // clipped to the content rectangle.
        Rect clip = Rect::intersection(destination, content_rect());
        bool valid = !destination.empty() && !clip.empty();
        for (Control::Ptr ancestor = (*control).parent();
             ancestor && valid && !clip.empty();
             ancestor = (*ancestor).parent()) {
            if (!(*ancestor).is_alive() || (*ancestor).surface_root_ != this ||
                !(*ancestor).visible.read()) {
                valid = false;
                break;
            }
            const Rect ancestor_bounds = absolute_bounds_of(*ancestor);
            const Rect viewport = (*ancestor).child_viewport_rectangle();
            clip = Rect::intersection(
                clip, {ancestor_bounds.x + viewport.x,
                       ancestor_bounds.y + viewport.y,
                       viewport.width, viewport.height});
        }
        if (!valid || clip.empty()) registration.placed = false;
        if (valid && !clip.empty()) {
            // Overlays crossing this clip, in tree order. Their pixels belong
            // to retained painting, which also draws the live frame beneath;
            // a new generation damages the share of them it changed (below).
            std::vector<Rect> overlays;
            for (const Rect& overlay : overlay_rectangles) {
                if (!Rect::intersection(clip, overlay).empty()) {
                    overlays.push_back(overlay);
                }
            }
            std::vector<Rect> clips{clip};
            for (const Rect& overlay : overlays) {
                std::vector<Rect> remaining;
                for (const Rect& candidate : clips) {
                    std::vector<Rect> fragments = Window::subtract_rectangle(
                        candidate, overlay);
                    remaining.insert(remaining.end(), fragments.begin(),
                                     fragments.end());
                }
                clips = std::move(remaining);
                if (clips.empty()) break;
            }
            const bool same_generation =
                registration.sampled_epoch == snapshot.epoch &&
                registration.sampled_generation == snapshot.published_generation;
            const bool same_placement = registration.placed &&
                registration.placed_destination == destination &&
                registration.placed_clip == clip &&
                registration.placed_overlays == overlays;
            // The live lane never draws beneath an overlay, so a new frame
            // reaches those pixels only through retained painting. Damage the
            // share of each overlay the frame changed; an overlay the change
            // does not reach keeps its retained pixels and is not repainted.
            if (!same_generation && !overlays.empty()) {
                const bool next_of_sampled = registration.placed &&
                    registration.sampled_epoch == snapshot.epoch &&
                    snapshot.published_generation ==
                        registration.sampled_generation + 1U;
                const Rect changed = Rect::intersection(
                    clip, detail::live_change_in_window(
                              *registration.surface, snapshot.epoch,
                              snapshot.published_generation, next_of_sampled,
                              destination));
                bool damaged = false;
                for (const Rect& overlay : overlays) {
                    const Rect beneath = Rect::intersection(overlay, changed);
                    if (beneath.empty()) continue;
                    add_damage_all_planes(beneath);
                    damaged = true;
                }
                if (damaged) {
                    touch_paint();
                }
            }
            // An unchanged generation is presented again only when its
            // placement changed; a static overlay does not keep it ticking.
            if (include_unchanged || !same_generation || !same_placement) {
                const bool damage_limited = same_placement && !include_unchanged;
                for (const Rect& visible_clip : clips) {
                    result.push_back(LiveSurfacePresentation{
                        (*control).runtime_id(), registration.surface,
                        destination, visible_clip, damage_limited, !content_behind});
                }
                if (content_behind && !clips.empty()) {
                    metrics_.record_live_unpaired_presentation();
                    // Told once per required tag, by a task posted to this
                    // thread: the listener runs after this present, never
                    // inside it (a closed dispatcher refuses the post).
                    const bool untold = registration.required_content_tag > registration.pairing_missed_tag;
                    if (untold && !registration.pairing_listener.expired()) {
                        registration.pairing_missed_tag = registration.required_content_tag;
                        static_cast<void>(dispatcher().begin_invoke(detail::NoticePairingMissed(
                            registration.pairing_listener, registration.required_content_tag)));
                    }
                }
            }
            registration.sampled_epoch = snapshot.epoch;
            registration.sampled_generation = snapshot.published_generation;
            registration.placed = true;
            registration.placed_destination = destination;
            registration.placed_clip = clip;
            registration.placed_overlays = std::move(overlays);
        }
        ++iterator;
    }
    return result;
}

void SurfaceRoot::set_paint_wake_handler(std::function<void()> wake) {
    require_root_thread("paint wake-handler mutation");
    paint_wake_handler_ = std::move(wake);
    paint_wake_pending_ = false;
    if (paint_wake_handler_ && paint_pending() && !occluded_) {
        request_paint_wake();
    }
}

DamageRegion SurfaceRoot::take_damage() {
    require_root_thread("damage mutation");
    // Hosts request damage before entering their native paint transaction.
    // Commit layout first so geometry changes discovered during arrange are
    // included in that same transaction instead of becoming stranded after
    // the host has already consumed the old region.
    ensure_layout(true);
    DamageRegion result;
    for (DamageRegion& plane : plane_damage_) {
        for (const Rect rect : plane.rectangles()) {
            result.add(rect);
        }
        plane = {};
    }
    acknowledge_paint_wake_if_damage_drained();
    return result;
}

DamageRegion SurfaceRoot::take_damage(PaintPlane plane) {
    require_root_thread("plane damage mutation");
    if (!is_valid_paint_plane(plane)) {
        throw std::invalid_argument("invalid paint plane");
    }
    ensure_layout(true);
    const std::size_t index = paint_plane_index(plane);
    DamageRegion result = std::move(plane_damage_[index]);
    plane_damage_[index] = {};
    acknowledge_paint_wake_if_damage_drained();
    return result;
}

} // namespace gui_forms
