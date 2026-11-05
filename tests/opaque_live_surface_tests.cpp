#include "gui_forms/live_surface.hpp"
#include "gui_forms/window.hpp"
#include "../src/host/macos/application/front_reuse.hpp"
#include "../src/host/macos/application/live_surface_damage.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vector>

namespace {
using namespace gui_forms;

void require(const bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

void description_and_frame_lifetime() {
    const LiveSurfaceDescription defaults{};
    require(!defaults.opaque, "opaque defaults false");
    const std::shared_ptr<LiveSurface> surface = LiveSurface::create(
        {.width = 2U, .height = 2U, .opaque = true});
    require(surface && (*surface).snapshot().description.opaque, "create preserves opaque");
    LiveSurfaceWriteLease write = (*surface).try_acquire_write();
    require(static_cast<bool>(write), "write lease acquired");
    for (std::byte& pixel : write.pixels()) pixel = std::byte{255};
    require(write.publish() != 0U, "opaque frame published");
    const LiveSurfaceFrame old_frame = (*surface).acquire_latest();
    require(old_frame.opaque(), "frame holds opaque promise");
    require(!(*surface).reconfigure({.opaque = false}), "invalid reconfigure refused");
    require((*surface).snapshot().description.opaque, "refusal preserves promise");
    require((*surface).reconfigure({.width = 3U, .height = 2U}), "reconfigure to translucent");
    require(!(*surface).snapshot().description.opaque, "snapshot follows reconfigure");
    require(old_frame.opaque() && old_frame.width() == 2U, "old frame keeps its own description");
    write = (*surface).try_acquire_write();
    require(static_cast<bool>(write), "replacement write lease acquired");
    require(write.publish() != 0U, "translucent frame published");
    const LiveSurfaceFrame new_frame = (*surface).acquire_latest();
    require(!new_frame.opaque(), "replacement frame is translucent");
    require((*surface).reconfigure({.width = 2U, .height = 3U, .opaque = true}), "reconfigure to opaque");
    require((*surface).snapshot().description.opaque, "reconfigure preserves true");
    const LiveSurfaceFrame empty{};
    require(!empty.opaque(), "empty frame grants no coverage");
}

void format_and_frame_lifetime() {
    // One format on every host (SK2): premultiplied BGRA8.
    constexpr LiveSurfacePixelFormat format = LiveSurfacePixelFormat::bgra32_premultiplied_srgb;
    const LiveSurfaceDescription defaults{};
    require(defaults.pixel_format == format, "BGRA is the default");
    require(native_live_surface_pixel_format() == format, "native query before attachment");
    const std::shared_ptr<LiveSurface> surface = LiveSurface::create(
        {.width = 2U, .height = 2U, .pixel_format = format, .opaque = true});
    require(surface && (*surface).snapshot().description.pixel_format == format, "create round trips format");
    LiveSurfaceWriteLease write = (*surface).try_acquire_write();
    require(static_cast<bool>(write), "format write acquired");
    const LiveSurfaceDescription next{.width = 2U, .height = 2U, .pixel_format = format};
    require(!(*surface).reconfigure(next), "active writer prevents redescribing");
    for (std::byte& value : write.pixels()) value = std::byte{255};
    require(write.publish() != 0U, "format frame published");
    const LiveSurfaceFrame old = (*surface).acquire_latest();
    require(old.pixel_format() == format, "frame has producer format");
    require((*surface).reconfigure(next), "same size description change accepted");
    require(old.pixel_format() == format && old.opaque(), "old lease keeps format and alpha promise");
    write = (*surface).try_acquire_write();
    require(static_cast<bool>(write), "replacement write acquired");
    require(write.publish() != 0U, "replacement published");
    const LiveSurfaceFrame current = (*surface).acquire_latest();
    require(current.pixel_format() == format && !current.opaque(), "new frame has new description");
    const LiveSurfaceDescription invalid{.width = 2U, .height = 2U,
        .pixel_format = static_cast<LiveSurfacePixelFormat>(1)};
    require(!LiveSurface::create(invalid) && !(*surface).reconfigure(invalid),
            "the former RGBA value and any other unknown format are rejected");
    require((*surface).snapshot().description.pixel_format == format && !(*surface).snapshot().description.opaque,
            "invalid format preserves state");
    const std::shared_ptr<Control> root = make_control<Control>(StableId("native-query"));
    Window window(root, {2.0, 2.0});
    window.perform_layout();
    require(window.queue_live_surface_presentation(root, surface), "format surface attached");
    require(native_live_surface_pixel_format() == format, "native query after attachment");
}

void damage_coverage() {
    const Rect dirty{0.0, 0.0, 10.0, 8.0};
    const std::array<Rect, 2> adjacent{{{0.0, 0.0, 4.0, 8.0}, {4.0, 0.0, 6.0, 8.0}}};
    require(detail::opaque_live_damage_covers(dirty, 1.0, adjacent, true, false), "union covers live-only damage");
    require(!detail::opaque_live_damage_covers(dirty, 1.0, adjacent, true, true), "retained damage requires paint");
    require(!detail::opaque_live_damage_covers(dirty, 1.0, adjacent, false, false), "first frame/resize/recovery requires paint");
    require(!detail::opaque_live_damage_covers(dirty, 1.0, {}, true, false), "translucent/absent frames cannot cover damage");
    const std::array<Rect, 4> hole{{{0.0, 0.0, 10.0, 2.0}, {0.0, 6.0, 10.0, 2.0},
                                  {0.0, 2.0, 2.0, 4.0}, {8.0, 2.0, 2.0, 4.0}}};
    require(!detail::opaque_live_damage_covers(dirty, 1.0, hole, true, false), "overlay hole is not bounding-box coverage");
    require(detail::opaque_live_damage_covers({0.0, 0.0, 10.0, 2.0}, 1.0, hole, true, false), "damage outside overlay remains covered");
    const std::array<Rect, 3> overlapping{{{0.0, 0.0, 6.0, 8.0}, {3.0, 0.0, 7.0, 4.0}, {3.0, 4.0, 7.0, 4.0}}};
    require(detail::opaque_live_damage_covers(dirty, 1.0, overlapping, true, false), "overlap and vertical union cover");
    const double scale = 1.25;
    const Rect fraction{0.1, 0.1, 7.8, 7.8};
    const std::array<Rect, 1> insufficient{{detail::opaque_live_clip(fraction, scale)}};
    require(!detail::opaque_live_damage_covers(fraction, scale, insufficient, true, false), "fractional boundary cannot promise outward pixels");
    const std::array<Rect, 1> complete{{detail::opaque_live_clip({0.0, 0.0, 8.0, 8.0}, scale)}};
    require(detail::opaque_live_damage_covers(fraction, scale, complete, true, false), "fractional damage rounds outward to covered device pixels");
    const std::array<Rect, 2> fractional_gap{{detail::opaque_live_clip({0.0, 0.0, 3.9, 8.0}, scale),
                                           detail::opaque_live_clip({4.1, 0.0, 3.9, 8.0}, scale)}};
    require(!detail::opaque_live_damage_covers({0.0, 0.0, 8.0, 8.0}, scale, fractional_gap, true, false), "subpixel overlay gap is not rounded away");
    require(!detail::opaque_live_damage_covers(dirty, 0.0, adjacent, true, false), "invalid scale refused");
    require(detail::opaque_live_clip(dirty, std::numeric_limits<double>::infinity()).empty(), "nonfinite scale refused");
}

void refreshed_presentations(const LiveSurfacePixelFormat format) {
    const std::shared_ptr<Control> root = make_control<Control>(StableId("root"));
    const std::shared_ptr<Control> first = make_control<Control>(StableId("first"));
    const std::shared_ptr<Control> second = make_control<Control>(StableId("second"));
    const std::shared_ptr<Control> overlay = make_control<Control>(StableId("overlay"));
    (*first).set_requested_bounds({0.0, 0.0, 4.0, 8.0});
    (*second).set_requested_bounds({4.0, 0.0, 4.0, 8.0});
    (*overlay).set_requested_bounds({0.0, 0.0, 4.0, 8.0});
    (*overlay).set_paint_plane(PaintPlane::overlay);
    (*overlay).visible = false;
    (*root).add_child(first);
    (*root).add_child(second);
    (*root).add_child(overlay);
    Window window(root, {8.0, 8.0});
    window.perform_layout();
    const std::shared_ptr<LiveSurface> surface_a = LiveSurface::create({.width = 4U, .height = 8U, .pixel_format = format, .opaque = true});
    const std::shared_ptr<LiveSurface> surface_b = LiveSurface::create({.width = 4U, .height = 8U, .pixel_format = format, .opaque = true});
    require(surface_a && surface_b, "retry surfaces created");
    LiveSurfaceWriteLease write_a = (*surface_a).try_acquire_write();
    LiveSurfaceWriteLease write_b = (*surface_b).try_acquire_write();
    require(write_a && write_b, "retry write leases acquired");
    for (std::byte& value : write_a.pixels()) value = std::byte{255};
    for (std::byte& value : write_b.pixels()) value = std::byte{255};
    const std::uint64_t generation_a = write_a.publish();
    const std::uint64_t generation_b = write_b.publish();
    require(generation_a != 0U && generation_b != 0U, "initial generations published");
    require(window.queue_live_surface_presentation(first, surface_a), "first registered");
    require(window.queue_live_surface_presentation(second, surface_b), "second registered");
    require(window.take_live_surface_presentations().size() == 2U, "initial batch sampled");
    require(window.take_live_surface_presentations().empty(), "unchanged drain stays empty by default");
    // A failed to present; only B changes before the retry.
    write_b = (*surface_b).try_acquire_write(true);
    require(static_cast<bool>(write_b), "second replacement acquired");
    require(write_b.publish() != 0U, "only second advances");
    const std::vector<LiveSurfacePresentation> retry = window.take_live_surface_presentations(true);
    require(retry.size() == 2U, "refresh retains unchanged failed surface A");
    (*overlay).visible = true;
    (*second).visible = false;
    window.perform_layout();
    require(window.take_live_surface_presentations(true).empty(), "refresh discards newly covered and hidden surfaces");
}
void publish_damage(LiveSurface& surface, const Rect damage) {
    LiveSurfaceWriteLease write = surface.try_acquire_write(true);
    require(static_cast<bool>(write), "damage write acquired");
    require(write.publish(damage) != 0U, "damage published");
}

void presentation_region() {
    const std::shared_ptr<LiveSurface> surface = LiveSurface::create(
        {.width = 100U, .height = 50U, .opaque = true});
    require(static_cast<bool>(surface), "region surface created");
    const Rect full{0.0, 0.0, 100.0, 50.0};
    const int presenter{};
    const int other{};
    publish_damage(*surface, {10.0, 10.0, 5.0, 5.0});
    require((*surface).acquire_for_presentation(&presenter).damage() == full, "first take is the whole surface");
    require((*surface).acquire_for_presentation(&presenter).damage().empty(), "nothing published since the take");

    publish_damage(*surface, {10.0, 10.0, 5.0, 5.0});
    publish_damage(*surface, {40.0, 20.0, 2.0, 3.0});
    require((*surface).acquire_latest().damage() == Rect(40.0, 20.0, 2.0, 3.0), "acquire_latest reads the newest publication");
    require((*surface).acquire_latest().damage() == Rect(40.0, 20.0, 2.0, 3.0), "acquire_latest never consumes");
    const LiveSurfaceFrame merged = (*surface).acquire_for_presentation(&presenter);
    require(merged.damage() == Rect(10.0, 10.0, 32.0, 13.0), "unpresented publications merge into one region");
    require(merged.generation() == 3U, "region and frame are taken together");
    require((*surface).acquire_for_presentation(&presenter).damage().empty(), "taking validates the region");

    publish_damage(*surface, {1.0, 1.0, 1.0, 1.0});
    require((*surface).acquire_for_presentation(&other).damage() == full, "another presenter copies everything");
    require((*surface).acquire_for_presentation(&presenter).damage() == full, "so does the presenter it displaced");
    publish_damage(*surface, {200.0, 0.0, 5.0, 5.0});
    require((*surface).acquire_for_presentation(&presenter).damage() == full, "outside damage is the whole surface");

    require((*surface).reconfigure({.width = 100U, .height = 50U, .opaque = true}), "reconfigured");
    // The previous epoch's frame carries over, whole, until the new epoch
    // publishes; a presenter scales it rather than showing nothing.
    const LiveSurfaceFrame carried = (*surface).acquire_for_presentation(&presenter);
    require(carried && carried.damage() == full && (*surface).snapshot().carries_previous_frame,
            "the previous frame carries over whole before the new epoch publishes");
    publish_damage(*surface, {1.0, 1.0, 1.0, 1.0});
    require((*surface).acquire_for_presentation(&presenter).damage() == full, "reconfigured surface is whole");
}

bool all_damage_limited(const std::vector<LiveSurfacePresentation>& presentations, const bool expected) {
    for (const LiveSurfacePresentation& presentation : presentations) {
        if (presentation.damage_limited != expected) return false;
    }
    return !presentations.empty();
}

void overlay_presentations() {
    const std::shared_ptr<Control> root = make_control<Control>(StableId("scene.root"));
    const std::shared_ptr<Control> scene = make_control<Control>(StableId("scene"));
    const std::shared_ptr<Control> capsule = make_control<Control>(StableId("capsule"));
    (*scene).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    (*capsule).set_requested_bounds({10.0, 10.0, 20.0, 10.0});
    (*capsule).set_paint_plane(PaintPlane::overlay);
    (*root).add_child(scene);
    (*root).add_child(capsule);
    Window window(root, {100.0, 80.0});
    window.perform_layout();
    const std::shared_ptr<LiveSurface> surface = LiveSurface::create(
        {.width = 100U, .height = 80U, .opaque = true});
    require(static_cast<bool>(surface), "scene surface created");
    publish_damage(*surface, {});
    require(window.queue_live_surface_presentation(scene, surface), "scene registered");

    const std::vector<LiveSurfacePresentation> placed = window.take_live_surface_presentations();
    require(placed.size() == 4U && all_damage_limited(placed, false), "first placement copies complete fragments");
    require(window.take_live_surface_presentations().empty(), "a static overlay does not re-present an unchanged surface");

    publish_damage(*surface, {50.0, 50.0, 5.0, 5.0});
    const std::vector<LiveSurfacePresentation> changed = window.take_live_surface_presentations();
    require(changed.size() == 4U && all_damage_limited(changed, true), "same placement permits region copies");

    (*capsule).set_requested_bounds({30.0, 10.0, 20.0, 10.0});
    window.perform_layout();
    const std::vector<LiveSurfacePresentation> moved = window.take_live_surface_presentations();
    require(moved.size() == 4U && all_damage_limited(moved, false), "a moved overlay re-presents completely");
    require(window.take_live_surface_presentations().empty(), "the moved overlay settles");
    require(all_damage_limited(window.take_live_surface_presentations(true), false), "retries copy complete clips");

    (*capsule).visible = false;
    window.perform_layout();
    const std::vector<LiveSurfacePresentation> uncovered = window.take_live_surface_presentations();
    require(uncovered.size() == 1U && all_damage_limited(uncovered, false), "a removed overlay re-presents completely");

    (*scene).visible = false;
    require(window.take_live_surface_presentations().empty(), "hidden surface is not presented");
    (*scene).visible = true;
    const std::vector<LiveSurfacePresentation> shown = window.take_live_surface_presentations();
    require(shown.size() == 1U && all_damage_limited(shown, false), "a re-shown surface copies completely");
}

// The live lane never draws beneath an overlay, so each new frame must leave
// the share of the overlay it changed as retained damage, and nothing more.
void overlay_share_of_new_frames() {
    const std::shared_ptr<Control> root = make_control<Control>(StableId("tank.root"));
    const std::shared_ptr<Control> scene = make_control<Control>(StableId("tank"));
    const std::shared_ptr<Control> capsule = make_control<Control>(StableId("tank.capsule"));
    (*scene).set_requested_bounds({0.0, 0.0, 100.0, 80.0});
    (*capsule).set_requested_bounds({10.0, 10.0, 20.0, 10.0});
    (*capsule).set_paint_plane(PaintPlane::overlay);
    (*root).add_child(scene);
    (*root).add_child(capsule);
    Window window(root, {100.0, 80.0});
    window.perform_layout();
    static_cast<void>(window.take_damage());
    // One surface pixel per window point.
    const std::shared_ptr<LiveSurface> surface = LiveSurface::create(
        {.width = 100U, .height = 80U, .opaque = true});
    require(static_cast<bool>(surface), "tank surface created");
    publish_damage(*surface, {});
    require(window.queue_live_surface_presentation(scene, surface), "tank registered");

    require(!window.take_live_surface_presentations().empty(), "first frame presented");
    require(window.take_damage().bounds() == Rect(10.0, 10.0, 20.0, 10.0),
            "a first frame damages the whole share beneath the overlay");

    publish_damage(*surface, {50.0, 50.0, 5.0, 5.0});
    require(!window.take_live_surface_presentations().empty(), "distant change presented");
    require(window.take_damage().empty(), "a change away from the overlay leaves it retained");

    publish_damage(*surface, {12.0, 12.0, 4.0, 3.0});
    require(!window.take_live_surface_presentations().empty(), "change beneath presented");
    require(window.take_damage().bounds() == Rect(11.0, 11.0, 6.0, 5.0),
            "a change beneath the overlay damages that share, widened one pixel");

    publish_damage(*surface, {50.0, 50.0, 5.0, 5.0});
    publish_damage(*surface, {60.0, 60.0, 5.0, 5.0});
    require(!window.take_live_surface_presentations().empty(), "skipped generation presented");
    require(window.take_damage().bounds() == Rect(10.0, 10.0, 20.0, 10.0),
            "a skipped generation's unknown damage covers the whole share");

    require(window.take_live_surface_presentations().empty(), "an unchanged frame is not presented");
    require(window.take_damage().empty(), "an unchanged frame damages nothing");
}

void host_region_helpers() {
    // 1:1 at scale 2: a 400-pixel surface in a 200-point destination.
    const Rect destination{10.0, 20.0, 200.0, 100.0};
    require(detail::live_damage_in_window({100.0, 50.0, 10.0, 4.0}, destination, 400U, 200U, 2.0) ==
                Rect(59.5, 44.5, 6.0, 3.0), "region maps through destination with one-pixel widening");
    require(detail::live_damage_in_window({0.0, 0.0, 400.0, 200.0}, destination, 400U, 200U, 2.0) == destination,
            "whole region is clamped to the destination");
    require(detail::live_damage_in_window({}, destination, 400U, 200U, 2.0).empty(), "empty region maps to nothing");

    // Live fragments around an overlay leave exactly the overlay uncovered.
    const std::array<Rect, 4> fragments{{{0.0, 0.0, 10.0, 2.0}, {0.0, 6.0, 10.0, 2.0},
                                         {0.0, 2.0, 2.0, 4.0}, {8.0, 2.0, 2.0, 4.0}}};
    std::vector<Rect> uncovered;
    detail::append_uncovered_live_damage({0.0, 0.0, 10.0, 8.0}, 1.0, fragments, uncovered);
    require(uncovered.size() == 1U && uncovered[0] == Rect(2.0, 2.0, 6.0, 4.0), "bounding box retains only the overlay");
    uncovered.clear();
    detail::append_uncovered_live_damage({0.0, 0.0, 10.0, 2.0}, 1.0, fragments, uncovered);
    require(uncovered.empty(), "covered dirty rectangle needs no retained painting");
    detail::append_uncovered_live_damage({0.0, 0.0, 3.0, 3.0}, 1.0, {}, uncovered);
    require(uncovered.size() == 1U && uncovered[0] == Rect(0.0, 0.0, 3.0, 3.0), "no live clips leave everything");
    std::vector<Rect> scattered;
    for (int index = 0; index < 40; ++index) {
        const double offset = static_cast<double>(index * 2 + 1);
        scattered.push_back({offset, offset, 1.0, 1.0});
    }
    uncovered.clear();
    detail::append_uncovered_live_damage({0.0, 0.0, 100.0, 100.0}, 1.0, scattered, uncovered);
    require(uncovered.size() == 1U && uncovered[0] == Rect(0.0, 0.0, 100.0, 100.0),
            "fragment overflow falls back to the whole dirty rectangle");
    // AppKit's one coalesced rectangle over two distant changes: on a reusable
    // raster only the parts beneath live surfaces stay retained damage.
    const std::array<Rect, 1> coalesced{{{10.0, 10.0, 370.0, 270.0}}};
    std::vector<Rect> kept;
    detail::append_within_live_destinations(coalesced, {}, kept);
    require(kept.empty(), "without live surfaces AppKit's request is a copy only");
    const std::array<Rect, 2> destinations{{{0.0, 0.0, 100.0, 80.0}, {500.0, 500.0, 10.0, 10.0}}};
    detail::append_within_live_destinations(coalesced, destinations, kept);
    require(kept.size() == 1U && kept[0] == Rect(10.0, 10.0, 90.0, 70.0),
            "the request is kept only beneath a live surface");
}
} // namespace

// The macOS host's copy-only draw (a reveal or exposure with nothing owed)
// needs the front it copies to be the last presentation at the view's size
// and scale in the model's current epoch, with nothing pending.
void front_reuse_rule() {
    detail::FrontReuse held{};
    held.raster_has_content = true;
    held.front_matches_extent = true;
    held.front_surface_epoch = 3U;
    held.surface_epoch = 3U;
    held.lease_clean = true;
    require(detail::front_held(held), "a presented front at the view's extent is held");
    require(detail::copy_only_draw(held, false, false), "with nothing pending a draw is a copy");
    require(!detail::copy_only_draw(held, true, false),
            "pending damage (a failed or discarded candidate keeps it) repaints");
    require(!detail::copy_only_draw(held, false, true), "a pending live frame composes");
    detail::FrontReuse lost = held;
    lost.live_region_lost = true;
    require(detail::front_held(lost) && !detail::copy_only_draw(lost, false, false),
            "a lost live region is not copied as it stands");
    detail::FrontReuse resized = held;
    resized.front_matches_extent = false;
    require(!detail::front_held(resized) && !detail::copy_only_draw(resized, false, false),
            "a front of another size or scale repaints");
    detail::FrontReuse epoch = held;
    epoch.surface_epoch = 4U;
    require(!detail::front_held(epoch) && !detail::copy_only_draw(epoch, false, false),
            "a front of an earlier surface epoch repaints");
    detail::FrontReuse unpresented = held;
    unpresented.front_surface_epoch = 0U;
    unpresented.surface_epoch = 0U;
    require(!detail::front_held(unpresented), "a front with no presented receipt is not held");
    detail::FrontReuse empty = held;
    empty.raster_has_content = false;
    require(!detail::front_held(empty), "a reallocated backing is not held");
    detail::FrontReuse owed = held;
    owed.lease_clean = false;
    require(!detail::front_held(owed) && !detail::copy_only_draw(owed, false, false),
            "damage the model still owes repaints");
}

int main() {
    description_and_frame_lifetime();
    front_reuse_rule();
    presentation_region();
    overlay_presentations();
    overlay_share_of_new_frames();
    host_region_helpers();
    format_and_frame_lifetime();
    damage_coverage();
    refreshed_presentations(LiveSurfacePixelFormat::bgra32_premultiplied_srgb);
    return 0;
}
