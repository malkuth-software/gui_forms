// The graph-space scene pieces (node graph gap G8 and the canvas geometry
// of NODE_GRAPH_SPEC sections 3 to 5), without a window: the view
// transform and its inverse, zoom about a point, panning and framing; the
// level of detail and its hysteresis; the spatial index against a brute
// force oracle in both query modes, through in-place moves, displacement,
// removal, oversize entries, clamping outside the built box, storage that
// is too short and its refusals; the soft link curve, its bounds,
// flattening, distances and crossings. Coordinates are chosen so the
// transform's sums are exact where equality is required.

#include "gui_forms/scene/scene_view/scene_view.hpp"
#include "gui_forms/scene/spatial_index/spatial_index.hpp"
#include "gui_forms/scene/world_geometry/world_geometry.hpp"
#include "gui_forms/types/geometry/geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_scene_tests: " << message << '\n';
    std::exit(1);
}

// A fixed linear congruential sequence, so a failure reproduces.
struct Sequence final {
    std::uint64_t state{0x9E3779B97F4A7C15ULL};

    double unit() {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        const std::uint64_t high = state >> 11U;
        const double value = static_cast<double>(high) / static_cast<double>(std::uint64_t{1U} << 53U);
        return value;
    }
    double between(double low, double high) {
        const double value = low + (high - low) * unit();
        return value;
    }
};

bool near(double left, double right, double tolerance) {
    const double scale = std::max(1.0, std::max(std::fabs(left), std::fabs(right)));
    const bool close = std::fabs(left - right) <= tolerance * scale;
    return close;
}

// -- the view transform ------------------------------------------------------------

void transform_and_inverse() {
    Sequence sequence{};
    for (std::size_t trial = 0U; trial < 2000U; ++trial) {
        SceneViewTransform view{};
        view.origin = WorldPoint{sequence.between(-1.0e6, 1.0e6), sequence.between(-1.0e6, 1.0e6)};
        view.zoom = std::pow(2.0, sequence.between(-6.0, 3.0));
        const WorldPoint world{sequence.between(-1.0e6, 1.0e6), sequence.between(-1.0e6, 1.0e6)};
        const WorldPoint back = view.to_world(view.to_view(world));
        require(near(back.x, world.x, 1.0e-12) && near(back.y, world.y, 1.0e-12),
                "to_world inverts to_view within rounding");
        const WorldRect rect{world.x, world.y, sequence.between(0.0, 500.0), sequence.between(0.0, 500.0)};
        const WorldRect rect_back = view.rect_to_world(view.rect_to_view(rect));
        require(near(rect_back.x, rect.x, 1.0e-12) && near(rect_back.width, rect.width, 1.0e-12) &&
                    near(rect_back.height, rect.height, 1.0e-12),
                "rect_to_world inverts rect_to_view");
    }
    // Exact values: origin (100, 50), zoom 2.
    const SceneViewTransform view{WorldPoint{100.0, 50.0}, 2.0};
    const Point point = view.to_view(WorldPoint{110.0, 60.0});
    require(point.x == 20.0 && point.y == 20.0, "view = (world - origin) * zoom");
    const Rect shown = view.rect_to_view(WorldRect{100.0, 50.0, 8.0, 4.0});
    require(shown.x == 0.0 && shown.y == 0.0 && shown.width == 16.0 && shown.height == 8.0,
            "a rectangle scales by the zoom");
    require(valid_scene_view(view) && !valid_scene_view(SceneViewTransform{WorldPoint{}, 0.0}) &&
                !valid_scene_view(SceneViewTransform{WorldPoint{std::numeric_limits<double>::infinity(), 0.0}, 1.0}),
            "a view needs a finite origin and a positive zoom");
}

void zoom_pan_and_frame() {
    const SceneZoomRange range{};
    const SceneViewTransform start{WorldPoint{0.0, 0.0}, 1.0};
    const Point anchor{120.0, 80.0};
    const WorldPoint fixed = start.to_world(anchor);
    const SceneViewTransform zoomed = scene_zoomed_about(start, anchor, 4.0, range);
    require(zoomed.zoom == 4.0, "the zoom multiplies");
    const WorldPoint still = zoomed.to_world(anchor);
    require(still.x == fixed.x && still.y == fixed.y, "the anchor's world point stays under it");
    require(scene_zoomed_about(start, anchor, 1000.0, range).zoom == range.maximum, "the zoom is held to 8");
    require(scene_zoomed_about(start, anchor, 1.0e-6, range).zoom == range.minimum, "and to 1/64");
    require(scene_zoomed_about(start, anchor, 0.0, range) == start &&
                scene_zoomed_about(start, anchor, std::numeric_limits<double>::quiet_NaN(), range) == start,
            "a non-positive or NaN factor changes nothing");
    require(clamp_scene_zoom(std::numeric_limits<double>::quiet_NaN(), range) == range.minimum,
            "a NaN zoom clamps to the minimum");

    const SceneViewTransform panned = scene_panned(zoomed, Point{40.0, -20.0});
    const Point moved = panned.to_view(WorldPoint{30.0, 20.0});
    const Point before = zoomed.to_view(WorldPoint{30.0, 20.0});
    require(moved.x == before.x + 40.0 && moved.y == before.y - 20.0, "panning moves the content with the pointer");

    const WorldRect box{1000.0, 2000.0, 400.0, 100.0};
    const SceneViewTransform framed = scene_framed(start, box, Size{480.0, 360.0}, 40.0, range);
    require(framed.zoom == 1.0, "the box fits the usable width at zoom 1");
    const Point centre = framed.to_view(WorldPoint{1200.0, 2050.0});
    require(centre.x == 240.0 && centre.y == 180.0, "the box is centred");
    require(scene_framed(start, box, Size{60.0, 60.0}, 40.0, range) == start,
            "a viewport smaller than its margins changes nothing");
    const SceneViewTransform point_frame = scene_framed(zoomed, WorldRect{10.0, 10.0, 0.0, 0.0}, Size{100.0, 100.0},
                                                        10.0, range);
    require(point_frame.zoom == zoomed.zoom && point_frame.to_view(WorldPoint{10.0, 10.0}).x == 50.0,
            "an empty box keeps the zoom and is centred");
}

// -- level of detail -----------------------------------------------------------------

void detail_levels() {
    const SceneDetailThresholds thresholds{};
    require(valid_scene_detail_thresholds(thresholds), "the default thresholds are valid");
    require(!valid_scene_detail_thresholds(SceneDetailThresholds{0.3, 0.1, 0.6, 1.1}) &&
                !valid_scene_detail_thresholds(SceneDetailThresholds{0.1, 0.3, 0.6, 0.9}),
            "thresholds must ascend and the hysteresis be at least 1");
    require(scene_detail_level_for(1.0, thresholds) == SceneDetailLevel::full &&
                scene_detail_level_for(0.4, thresholds) == SceneDetailLevel::compact &&
                scene_detail_level_for(0.2, thresholds) == SceneDetailLevel::silhouette &&
                scene_detail_level_for(0.05, thresholds) == SceneDetailLevel::overview,
            "the plain levels follow the thresholds");
    // Hysteresis about compact/full (0.6, factor 1.1): up at 0.66 or above,
    // down below 0.6 / 1.1.
    SceneDetailLevel level = SceneDetailLevel::compact;
    level = next_scene_detail_level(level, 0.62, thresholds);
    require(level == SceneDetailLevel::compact, "just past the threshold the level holds");
    level = next_scene_detail_level(level, 0.66, thresholds);
    require(level == SceneDetailLevel::full, "past threshold times hysteresis the level rises");
    level = next_scene_detail_level(level, 0.58, thresholds);
    require(level == SceneDetailLevel::full, "just below the threshold the level holds");
    level = next_scene_detail_level(level, 0.54, thresholds);
    require(level == SceneDetailLevel::compact, "below threshold over hysteresis the level falls");
    // A resting zoom near a threshold never flickers.
    std::size_t changes = 0U;
    for (std::size_t step = 0U; step < 100U; ++step) {
        const double zoom = 0.6 + 0.02 * std::sin(static_cast<double>(step));
        const SceneDetailLevel next = next_scene_detail_level(level, zoom, thresholds);
        if (next != level) ++changes;
        level = next;
    }
    require(changes == 0U, "a zoom wobbling inside the hysteresis band keeps its level");
    // Several levels at once.
    require(next_scene_detail_level(SceneDetailLevel::full, 0.01, thresholds) == SceneDetailLevel::overview &&
                next_scene_detail_level(SceneDetailLevel::overview, 4.0, thresholds) == SceneDetailLevel::full,
            "a jump crosses several levels");
    require(next_scene_detail_level(SceneDetailLevel::compact, std::numeric_limits<double>::quiet_NaN(), thresholds) ==
                SceneDetailLevel::compact,
            "a NaN zoom keeps the level");
    require(scene_detail_level_name(SceneDetailLevel::silhouette) == "silhouette", "levels have names");
}

// -- the spatial index ---------------------------------------------------------------

std::vector<std::uint32_t> brute_force(std::span<const SpatialEntry> entries, const WorldRect& area,
                                       SpatialQueryMode mode) {
    std::vector<std::uint32_t> found{};
    for (std::size_t id = 0U; id < entries.size(); ++id) {
        if (!entries[id].present) continue;
        const bool hit = mode == SpatialQueryMode::contained ? world_rect_contains(area, entries[id].bounds)
                                                             : world_rects_intersect(area, entries[id].bounds);
        if (hit) found.push_back(static_cast<std::uint32_t>(id));
    }
    return found;
}

bool same_set(std::vector<std::uint32_t> left, std::span<const std::uint32_t> right) {
    std::vector<std::uint32_t> sorted(right.begin(), right.end());
    std::sort(left.begin(), left.end());
    std::sort(sorted.begin(), sorted.end());
    const bool same = left == sorted;
    return same;
}

std::vector<SpatialEntry> random_entries(Sequence& sequence, std::size_t count) {
    std::vector<SpatialEntry> entries(count);
    for (SpatialEntry& entry : entries) {
        entry.bounds = WorldRect{sequence.between(-5000.0, 5000.0), sequence.between(-3000.0, 3000.0),
                                 sequence.between(20.0, 300.0), sequence.between(20.0, 200.0)};
        entry.present = sequence.unit() > 0.1;
    }
    return entries;
}

void check_queries(SpatialIndex& index, std::span<const SpatialEntry> entries, Sequence& sequence,
                   std::vector<std::uint32_t>& storage, const char* message) {
    for (std::size_t trial = 0U; trial < 200U; ++trial) {
        const WorldRect area{sequence.between(-7000.0, 6000.0), sequence.between(-4000.0, 4000.0),
                             sequence.between(0.0, 3000.0), sequence.between(0.0, 2000.0)};
        const std::array<SpatialQueryMode, 2> modes{SpatialQueryMode::intersecting, SpatialQueryMode::contained};
        for (const SpatialQueryMode mode : modes) {
            const SpatialQueryResult found = index.query(area, mode, std::span<std::uint32_t>(storage));
            require(found.written == found.matched, "storage of the entry count never truncates");
            require(same_set(brute_force(entries, area, mode),
                             std::span<const std::uint32_t>(storage.data(), found.written)),
                    message);
        }
    }
}

void spatial_index_against_brute_force() {
    Sequence sequence{};
    std::vector<SpatialEntry> entries = random_entries(sequence, 3000U);
    SpatialIndex index{};
    SpatialIndexOptions options{};
    options.cell_size = 128.0;
    options.maximum_displaced = 64U;
    index.build(entries, options);
    std::vector<std::uint32_t> storage(entries.size());
    require(index.entry_count() == entries.size() && index.statistics().builds == 1U, "the index is built");
    check_queries(index, entries, sequence, storage, "queries match the brute force after a build");

    // Moves: small ones stay in their cells, large ones are displaced.
    std::size_t moved = 0U;
    for (std::uint32_t id = 0U; id < entries.size() && moved < 40U; ++id) {
        if (!entries[id].present) continue;
        WorldRect bounds = entries[id].bounds;
        bounds.x += moved % 2U == 0U ? 1.0 : 2500.0;
        require(index.move_entry(id, bounds), "a move within the displaced capacity is accepted");
        entries[id].bounds = bounds;
        ++moved;
    }
    require(index.statistics().moved_in_place > 0U && index.statistics().moved_out_of_cells > 0U,
            "some moves stay in place and some leave their cells");
    check_queries(index, entries, sequence, storage, "queries match the brute force after moves");

    // A displaced entry moved back to its cells leaves the displaced list.
    std::uint32_t displaced_id = 0U;
    for (std::uint32_t id = 0U; id < entries.size(); ++id) {
        if (entries[id].present) {
            displaced_id = id;
            break;
        }
    }
    const WorldRect original = entries[displaced_id].bounds;
    const std::size_t displaced_before = index.statistics().displaced;
    WorldRect far = original;
    far.x += 100000.0;
    require(index.move_entry(displaced_id, far), "a far move is displaced");
    require(index.statistics().displaced == displaced_before + 1U ||
                index.statistics().displaced == displaced_before,
            "the displaced count counts it once");
    require(index.move_entry(displaced_id, original), "and it moves back");
    entries[displaced_id].bounds = original;
    check_queries(index, entries, sequence, storage, "queries match after a displaced entry returns");

    // Removal.
    for (std::uint32_t id = 0U; id < 100U; ++id) {
        const bool removed = index.remove_entry(id);
        require(removed == entries[id].present, "remove_entry reports whether the entry was present");
        entries[id].present = false;
    }
    require(!index.remove_entry(0U) && !index.present(0U), "a removed entry is absent");
    check_queries(index, entries, sequence, storage, "queries match after removals");

    // Refusals: an unknown id, invalid bounds, an absent entry.
    WorldRect bad{};
    bad.width = -1.0;
    require(!index.move_entry(static_cast<std::uint32_t>(entries.size()), WorldRect{}) &&
                !index.move_entry(200U, bad) && !index.move_entry(0U, WorldRect{}),
            "unknown ids, invalid bounds and absent entries are refused");
    const SpatialQueryResult nothing =
        index.query(WorldRect{0.0, 0.0, std::numeric_limits<double>::quiet_NaN(), 1.0}, SpatialQueryMode::intersecting,
                    std::span<std::uint32_t>(storage));
    require(nothing.matched == 0U, "an invalid area matches nothing");
}

void displaced_capacity_and_rebuild() {
    std::vector<SpatialEntry> entries(10U);
    for (std::size_t id = 0U; id < entries.size(); ++id) {
        entries[id] = SpatialEntry{WorldRect{static_cast<double>(id) * 1000.0, 0.0, 10.0, 10.0}, true};
    }
    SpatialIndex index{};
    SpatialIndexOptions options{};
    options.cell_size = 100.0;
    options.maximum_displaced = 2U;
    index.build(entries, options);
    require(index.move_entry(0U, WorldRect{5000.0, 500.0, 10.0, 10.0}) &&
                index.move_entry(1U, WorldRect{6000.0, 500.0, 10.0, 10.0}),
            "two displacements fit");
    const WorldRect kept = index.bounds(2U);
    require(!index.move_entry(2U, WorldRect{7000.0, 500.0, 10.0, 10.0}), "a third asks for a rebuild");
    require(index.bounds(2U) == kept && index.statistics().moves_refused == 1U, "the refused move changed nothing");
    entries[0].bounds = WorldRect{5000.0, 500.0, 10.0, 10.0};
    entries[1].bounds = WorldRect{6000.0, 500.0, 10.0, 10.0};
    entries[2].bounds = WorldRect{7000.0, 500.0, 10.0, 10.0};
    index.build(entries, options);
    require(index.statistics().builds == 2U && index.statistics().displaced == 0U, "a rebuild empties the list");
    std::array<std::uint32_t, 4> found{};
    const SpatialQueryResult result =
        index.query(WorldRect{6990.0, 490.0, 30.0, 30.0}, SpatialQueryMode::intersecting, found);
    require(result.written == 1U && found[0] == 2U, "the rebuilt index finds the moved entry");
}

void clamping_oversize_and_short_storage() {
    std::vector<SpatialEntry> entries{
        SpatialEntry{WorldRect{0.0, 0.0, 10.0, 10.0}, true},
        SpatialEntry{WorldRect{100.0, 100.0, 10.0, 10.0}, true},
        // Covers far more than maximum_cells_per_entry cells: oversize.
        SpatialEntry{WorldRect{-5000.0, -5000.0, 10000.0, 10000.0}, true},
        SpatialEntry{WorldRect{50.0, 50.0, 0.0, 0.0}, true},
    };
    SpatialIndex index{};
    SpatialIndexOptions options{};
    options.cell_size = 16.0;
    options.maximum_cells_per_entry = 4U;
    index.build(entries, options);
    require(index.statistics().oversize == 1U, "the huge entry is kept in the oversize list");
    // Moved far beyond the built box, entry 0 is still found: queries clamp.
    require(index.move_entry(0U, WorldRect{90000.0, 90000.0, 10.0, 10.0}), "a move past the box is accepted");
    std::array<std::uint32_t, 8> found{};
    SpatialQueryResult result =
        index.query(WorldRect{89990.0, 89990.0, 40.0, 40.0}, SpatialQueryMode::intersecting, found);
    require(result.written == 1U && found[0] == 0U, "an entry outside the built box is found by a clamped query");
    // The oversize entry and the point entry.
    result = index.query(WorldRect{40.0, 40.0, 20.0, 20.0}, SpatialQueryMode::intersecting, found);
    require(result.matched == 2U, "the oversize entry and the point inside are both found");
    result = index.query(WorldRect{40.0, 40.0, 20.0, 20.0}, SpatialQueryMode::contained, found);
    require(result.matched == 1U && found[0] == 3U, "containment excludes the oversize entry");
    // Storage too short: every match counted, the first written.
    std::array<std::uint32_t, 1> one{};
    result = index.query(WorldRect{-10.0, -10.0, 200.0, 200.0}, SpatialQueryMode::intersecting, one);
    require(result.written == 1U && result.matched == 3U, "short storage writes what fits and counts every match");
    // Each entry is reported once even when it covers several cells.
    std::vector<SpatialEntry> wide{SpatialEntry{WorldRect{0.0, 0.0, 60.0, 60.0}, true}};
    SpatialIndexOptions wide_options{};
    wide_options.cell_size = 16.0;
    SpatialIndex wide_index{};
    wide_index.build(wide, wide_options);
    require(wide_index.statistics().oversize == 0U, "a 4 by 4 cell entry is kept in its cells");
    result = wide_index.query(WorldRect{-10.0, -10.0, 100.0, 100.0}, SpatialQueryMode::intersecting, found);
    require(result.matched == 1U, "an entry in several cells is reported once");
}

void build_refusals() {
    SpatialIndex index{};
    std::vector<SpatialEntry> entries{SpatialEntry{WorldRect{0.0, 0.0, 10.0, 10.0}, true}};
    index.build(entries, SpatialIndexOptions{});
    bool threw = false;
    SpatialIndexOptions bad{};
    bad.cell_size = 0.0;
    try {
        index.build(entries, bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && index.entry_count() == 1U && index.statistics().builds == 1U,
            "a zero cell size is refused and the index is unchanged");
    threw = false;
    bad = SpatialIndexOptions{};
    bad.maximum_cells = 3U;
    try {
        index.build(entries, bad);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "fewer than 4 cells are refused");
    threw = false;
    std::vector<SpatialEntry> broken{SpatialEntry{WorldRect{0.0, 0.0, std::numeric_limits<double>::infinity(), 1.0}, true}};
    try {
        index.build(broken, SpatialIndexOptions{});
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && index.entry_count() == 1U, "an infinite entry is refused and the index is unchanged");
    // An absent entry's bounds are not read.
    broken[0].present = false;
    index.build(broken, SpatialIndexOptions{});
    require(index.entry_count() == 1U && !index.present(0U), "an absent entry is skipped, whatever its bounds");
    // An empty build and a cleared index answer nothing.
    index.build(std::span<const SpatialEntry>{}, SpatialIndexOptions{});
    std::array<std::uint32_t, 2> found{};
    require(index.query(WorldRect{0.0, 0.0, 1.0, 1.0}, SpatialQueryMode::intersecting, found).matched == 0U,
            "an empty index matches nothing");
    index.clear();
    require(index.entry_count() == 0U, "clear empties the index");
}

// -- links ----------------------------------------------------------------------------

void soft_links() {
    const SoftLinkShape shape{};
    // Forward: tangents of half the horizontal distance.
    const SceneCubic forward =
        soft_link_cubic(WorldPoint{0.0, 0.0}, SceneEdge::right, WorldPoint{200.0, 50.0}, SceneEdge::left, shape);
    require(forward.p1.x == 100.0 && forward.p1.y == 0.0 && forward.p2.x == 100.0 && forward.p2.y == 50.0,
            "a forward link's tangents are horizontal and half the distance long");
    // Short: held to the shortest tangent; long: to the longest.
    const SceneCubic close =
        soft_link_cubic(WorldPoint{0.0, 0.0}, SceneEdge::right, WorldPoint{10.0, 0.0}, SceneEdge::left, shape);
    require(close.p1.x == 24.0 && close.p2.x == -14.0, "a short link's tangents are held to 24");
    const SceneCubic distant =
        soft_link_cubic(WorldPoint{0.0, 0.0}, SceneEdge::right, WorldPoint{1000.0, 0.0}, SceneEdge::left, shape);
    require(distant.p1.x == 160.0, "a long link's tangent is held to 160");
    // A back-link loops: the sideways distance lengthens its tangents.
    const SceneCubic back =
        soft_link_cubic(WorldPoint{0.0, 0.0}, SceneEdge::right, WorldPoint{-100.0, 200.0}, SceneEdge::left, shape);
    require(back.p1.x == 50.0 + 50.0 && back.p2.x == -100.0 - 100.0, "a back-link adds a quarter of the sideways distance");
    // A vertical port leaves along its normal.
    const SceneCubic down =
        soft_link_cubic(WorldPoint{0.0, 0.0}, SceneEdge::bottom, WorldPoint{0.0, 300.0}, SceneEdge::top, shape);
    require(down.p1.x == 0.0 && down.p1.y == 150.0 && down.p2.y == 150.0, "a bottom port leaves downward");

    // Bounds hold every flattened point.
    const WorldRect bounds = scene_cubic_bounds(back);
    std::array<Point, 33> points{};
    const SceneViewTransform identity{};
    const std::size_t count = flatten_scene_cubic(back, 32U, identity, points);
    require(count == 33U && points[0].x == 0.0 && points[32].x == -100.0 && points[32].y == 200.0,
            "flattening writes the ends exactly");
    for (std::size_t index = 0U; index < count; ++index) {
        require(bounds.contains(WorldPoint{points[index].x, points[index].y}), "the bounds hold the curve");
    }
    std::array<Point, 4> few{};
    require(flatten_scene_cubic(back, 32U, identity, few) == 4U, "flattening stops at the storage's end");
    require(flatten_scene_cubic(back, 0U, identity, points) == 2U, "zero segments count as one");

    // Distances and crossings.
    const std::array<Point, 3> polyline{Point{0.0, 0.0}, Point{10.0, 0.0}, Point{10.0, 10.0}};
    require(scene_distance_to_polyline(polyline, Point{5.0, 3.0}) == 3.0 &&
                scene_distance_to_polyline(polyline, Point{13.0, 5.0}) == 3.0,
            "the distance is to the nearest segment");
    require(std::isinf(scene_distance_to_polyline(std::span<const Point>{}, Point{})), "no points is infinitely far");
    require(scene_segments_cross(Point{0.0, 0.0}, Point{10.0, 10.0}, Point{0.0, 10.0}, Point{10.0, 0.0}),
            "an X crosses");
    require(!scene_segments_cross(Point{0.0, 0.0}, Point{10.0, 0.0}, Point{0.0, 1.0}, Point{10.0, 1.0}),
            "parallel segments do not");
    require(scene_segments_cross(Point{0.0, 0.0}, Point{10.0, 0.0}, Point{5.0, 0.0}, Point{5.0, 5.0}),
            "touching counts");
    require(scene_polyline_crosses_segment(polyline, Point{5.0, -5.0}, Point{5.0, 5.0}) &&
                !scene_polyline_crosses_segment(polyline, Point{20.0, -5.0}, Point{20.0, 5.0}),
            "a polyline crosses a segment through any of its segments");
}

void world_rect_helpers() {
    const WorldRect left{0.0, 0.0, 10.0, 10.0};
    const WorldRect right{10.0, 5.0, 10.0, 10.0};
    require(world_rects_intersect(left, right) && !world_rects_intersect(left, WorldRect{10.5, 0.0, 1.0, 1.0}),
            "touching rectangles intersect, apart ones do not");
    require(world_rect_contains(left, WorldRect{2.0, 2.0, 8.0, 8.0}) && !world_rect_contains(left, right),
            "containment includes the edges");
    const WorldRect joined = world_rect_union(left, right);
    require(joined == WorldRect{0.0, 0.0, 20.0, 15.0}, "the union holds both");
    const WorldRect shrunk = world_rect_inflated(left, -8.0);
    require(shrunk == WorldRect{5.0, 5.0, 0.0, 0.0}, "shrinking past empty stops at the centre");
}

} // namespace

int main() {
    transform_and_inverse();
    zoom_pan_and_frame();
    detail_levels();
    spatial_index_against_brute_force();
    displaced_capacity_and_rebuild();
    clamping_oversize_and_short_storage();
    build_refusals();
    soft_links();
    world_rect_helpers();
    std::cout << "gui_forms_scene_tests passed\n";
    return 0;
}
