#include "gui_forms/plots/plot3d_scene/plot3d_scene.hpp"

#include "gui_forms/plots/scale/scale.hpp"

#include <stx_conv/surface.hpp>
#include <stx_raster3d/camera.hpp>
#include <stx_raster3d/executor.hpp>
#include <stx_raster3d/geometry.hpp>
#include <stx_raster3d/memory.hpp>
#include <stx_raster3d/mesh.hpp>
#include <stx_raster3d/picking.hpp>
#include <stx_raster3d/raster.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <stdexcept>
#include <utility>
#include <vector>

namespace gui_forms::plots {

namespace {

// Mesh parts 0 to 2 are the panes across x, y and z; an item's surface is
// part pane_parts + its slot.
constexpr std::size_t pane_parts = 3U;
// The item of a strip that is not data (grid, axes, ticks).
constexpr std::uint32_t no_item = 0xffffffffU;
// Colours sampled from a surface's colour map.
constexpr std::size_t ramp_size = 256U;
// The most nodes of one surface, and points of one line or scatter.
constexpr std::size_t maximum_surface_nodes = std::size_t{1} << 22U;
constexpr std::size_t maximum_points = std::size_t{1} << 24U;
constexpr double maximum_line_width = 32.0;
constexpr double maximum_marker_size = 64.0;
// Camera bounds.
constexpr double minimum_zoom = 0.1;
constexpr double maximum_zoom = 20.0;
constexpr double minimum_focal_length = 0.2;
constexpr double maximum_focal_length = 20.0;
constexpr double maximum_target = 4.0;
// The box's bounding sphere fills this fraction of the picture's half
// height (or half width, when that is shorter), leaving room for labels.
constexpr double fit_fraction = 0.72;
// The CONV tessellation's tolerance, in box units (the box is 2 wide).
constexpr double tessellation_tolerance = 2e-3;
constexpr std::size_t tessellation_depth = 5U;
// Ticks: their length in box units; the gap to their labels, the labels'
// height and the gap before an axis title, in logical units.
constexpr double tick_length = 0.035;
constexpr double label_gap = 5.0;
constexpr double tick_label_height = 14.0;
constexpr double title_gap = 8.0;
// Widths of the view's lines, in logical units.
constexpr double grid_width = 1.0;
constexpr double axis_width = 1.0;
// A label's place counts as hidden when data lies this much nearer.
constexpr double occlusion_slack = 1e-3;
constexpr double degrees_per_radian = 57.29577951308232;
constexpr double half_turn_degrees = 180.0;

enum class ItemKind : std::uint8_t { surface, line, scatter };

// One item as the scene keeps it. Surfaces keep full grids (x and y
// expanded to rows * columns); lines and scatters their points.
struct SceneItem final {
    Plot3DItemId id{};
    ItemKind kind{ItemKind::surface};
    std::vector<double> x{};
    std::vector<double> y{};
    std::vector<double> z{};
    std::size_t rows{};
    std::size_t columns{};
    std::size_t row_stride{1U};
    std::size_t column_stride{1U};
    Plot3DInterpolation interpolation{Plot3DInterpolation::conv};
    BuiltInColormap colormap{BuiltInColormap::viridis};
    stxlib::Optional<Color> color{};
    Plot3DLighting lighting{Plot3DLighting::fixed};
    double width{1.5};
    double size{6.0};
    Plot3DMarker marker{Plot3DMarker::circle};
    std::string label{};
    // The finite z values' range (a surface's colour map spans it).
    PlotRange values{};
    // Derived at the last data build: a surface's kept rows and columns
    // (downsampling) and its CONV surface (interpolation conv).
    std::vector<std::size_t> row_nodes{};
    std::vector<std::size_t> column_nodes{};
    std::unique_ptr<stx_conv::surface::Surface> conv{};
};

} // namespace

namespace detail {

// Everything a scene owns. The data part of the mesh, strips and markers is
// rebuilt by build_data; the view part by each render.
struct Plot3DSceneState final {
    std::vector<SceneItem> items{};
    std::uint32_t next_id{1U};
    Plot3DCamera camera{};
    Plot3DOptions options{};
    Plot3DColors colors{};
    bool data_dirty{true};

    // The data limits and the box's half extents at the last build.
    std::array<PlotRange, 3> limits{};
    std::array<double, 3> half{1.0, 1.0, 0.75};

    stx_raster3d::Mesh mesh{stx_raster3d::system_allocator()};
    stx_raster3d::MeshMark data_mark{};
    // By part: the panes', then each slot's (unused for lines and scatters).
    std::vector<stx_raster3d::Material> materials{};
    // ramp_size colours per slot, borrowed by the materials; sized before
    // any material borrows it and not resized until the next build.
    std::vector<stx_raster3d::Color> ramps{};
    // The data lines and markers in box units, with widths and sizes in
    // logical units (scaled per render), and for each strip and marker the
    // item's point it starts at.
    std::vector<stx_raster3d::Vec3> data_points{};
    std::vector<stx_raster3d::LineStrip> data_strips{};
    std::vector<std::uint32_t> strip_points{};
    std::vector<stx_raster3d::Marker> data_markers{};
    std::vector<std::uint32_t> marker_points{};
    // What a render draws: the data's, then the view's lines.
    std::vector<stx_raster3d::Vec3> points{};
    std::vector<stx_raster3d::LineStrip> strips{};
    std::vector<stx_raster3d::Marker> markers{};
    // Build scratch: one surface's node channels and hidden cells.
    std::vector<double> node_scratch{};
    std::vector<double> value_scratch{};
    std::vector<std::uint8_t> hidden_scratch{};

    stx_raster3d::Renderer renderer{stx_raster3d::system_allocator()};
    // The last good render's view, its logical size and scale, the camera
    // distance and the side of each axis's back pane (-1 or +1).
    stx_raster3d::View view{};
    double view_width{};
    double view_height{};
    double view_scale{1.0};
    double distance{1.0};
    std::array<double, 3> back_side{-1.0, -1.0, -1.0};
    bool rendered{};
    // Whether the last picture's slot numbers still name the items: false
    // from any add, remove or clear until the next render rebuilds the
    // picture's tables. A pick against a stale picture finds nothing
    // (A-gf-core-09-F1).
    bool picture_items_current{};
    std::array<TickSet, 3> ticks{};
    std::array<bool, 3> ticks_valid{};
    std::vector<Plot3DLabel> labels{};
    std::uint64_t renders{};
    std::uint64_t mesh_builds{};
};

} // namespace detail

namespace {

using State = detail::Plot3DSceneState;

[[nodiscard]] stx_raster3d::Color raster_color(Color color) noexcept {
    const stx_raster3d::Color converted{static_cast<float>(color.red) / 255.0f,
                                        static_cast<float>(color.green) / 255.0f,
                                        static_cast<float>(color.blue) / 255.0f,
                                        static_cast<float>(color.alpha) / 255.0f};
    return converted;
}

[[nodiscard]] stx_raster3d::MarkerShape marker_shape(Plot3DMarker marker) noexcept {
    switch (marker) {
    case Plot3DMarker::square:
        return stx_raster3d::MarkerShape::square;
    case Plot3DMarker::diamond:
        return stx_raster3d::MarkerShape::diamond;
    case Plot3DMarker::triangle_up:
        return stx_raster3d::MarkerShape::triangle_up;
    case Plot3DMarker::plus:
        return stx_raster3d::MarkerShape::plus;
    case Plot3DMarker::cross:
        return stx_raster3d::MarkerShape::cross;
    case Plot3DMarker::circle:
    default:
        return stx_raster3d::MarkerShape::circle;
    }
}

[[nodiscard]] stx_raster3d::LightingMode lighting_mode(Plot3DLighting lighting) noexcept {
    switch (lighting) {
    case Plot3DLighting::fixed:
        return stx_raster3d::LightingMode::fixed;
    case Plot3DLighting::headlight:
        return stx_raster3d::LightingMode::headlight;
    case Plot3DLighting::none:
    default:
        return stx_raster3d::LightingMode::none;
    }
}

[[nodiscard]] double component(const stx_raster3d::Vec3& point, std::size_t axis) noexcept {
    if (axis == 0U) return point.x;
    if (axis == 1U) return point.y;
    return point.z;
}

void set_component(stx_raster3d::Vec3& point, std::size_t axis, double value) noexcept {
    if (axis == 0U) point.x = value;
    if (axis == 1U) point.y = value;
    if (axis == 2U) point.z = value;
}

[[nodiscard]] stx_raster3d::Vec3 vector_of(const std::array<double, 3>& values) noexcept {
    const stx_raster3d::Vec3 point{values[0], values[1], values[2]};
    return point;
}

[[nodiscard]] double clamped(double value, double low, double high) noexcept {
    const double result = std::min(high, std::max(low, value));
    return result;
}

// An angle wrapped into [-180, 180).
[[nodiscard]] double wrapped_degrees(double angle) noexcept {
    const double turns = std::floor((angle + half_turn_degrees) / (2.0 * half_turn_degrees));
    const double result = angle - turns * 2.0 * half_turn_degrees;
    return result;
}

[[nodiscard]] bool finite_or_nan(std::span<const double> values) noexcept {
    for (const double value : values) {
        if (std::isinf(value)) return false;
    }
    return true;
}

[[nodiscard]] bool all_finite(std::span<const double> values) noexcept {
    for (const double value : values) {
        if (!std::isfinite(value)) return false;
    }
    return true;
}

// The finite values' range, or minimum > maximum when there are none.
[[nodiscard]] PlotRange finite_range(std::span<const double> values) noexcept {
    PlotRange range{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    for (const double value : values) {
        if (!std::isfinite(value)) continue;
        range.minimum = std::min(range.minimum, value);
        range.maximum = std::max(range.maximum, value);
    }
    return range;
}

// A range widened when it is one value, and [0, 1] when it is empty.
[[nodiscard]] PlotRange widened(PlotRange range) noexcept {
    if (!(range.minimum <= range.maximum)) return PlotRange{0.0, 1.0};
    if (range.minimum < range.maximum) return range;
    const double half = range.minimum == 0.0 ? 0.5 : std::abs(range.minimum) * 0.1;
    const PlotRange result{range.minimum - half, range.maximum + half};
    return result;
}

[[nodiscard]] PlotRange united(PlotRange left, PlotRange right) noexcept {
    const PlotRange result{std::min(left.minimum, right.minimum), std::max(left.maximum, right.maximum)};
    return result;
}

[[nodiscard]] std::array<PlotRange, 3> data_limits(const std::vector<SceneItem>& items) noexcept {
    const PlotRange empty{std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()};
    std::array<PlotRange, 3> limits{empty, empty, empty};
    for (const SceneItem& item : items) {
        limits[0] = united(limits[0], finite_range(item.x));
        limits[1] = united(limits[1], finite_range(item.y));
        limits[2] = united(limits[2], finite_range(item.z));
    }
    for (std::size_t axis = 0U; axis < 3U; ++axis) limits[axis] = widened(limits[axis]);
    return limits;
}

[[nodiscard]] double to_box(const State& state, std::size_t axis, double value) noexcept {
    const PlotRange& range = state.limits[axis];
    const double unit = (value - range.minimum) / (range.maximum - range.minimum);
    const double result = (unit * 2.0 - 1.0) * state.half[axis];
    return result;
}

[[nodiscard]] double from_box(const State& state, std::size_t axis, double value) noexcept {
    const PlotRange& range = state.limits[axis];
    const double unit = (value / state.half[axis] + 1.0) * 0.5;
    const double result = range.minimum + unit * (range.maximum - range.minimum);
    return result;
}

[[nodiscard]] std::array<double, 3> data_of(const State& state, const stx_raster3d::Vec3& point) noexcept {
    const std::array<double, 3> data{from_box(state, 0U, point.x), from_box(state, 1U, point.y),
                                     from_box(state, 2U, point.z)};
    return data;
}

// The kept indices of `count` nodes taken every `stride`, the last always.
void kept_nodes(std::size_t count, std::size_t stride, std::vector<std::size_t>& kept) {
    kept.clear();
    for (std::size_t index = 0U; index < count; index += stride) kept.push_back(index);
    if (kept.back() != count - 1U) kept.push_back(count - 1U);
}

// A fractional original index from a position in kept-node units.
[[nodiscard]] double original_index(const std::vector<std::size_t>& kept, double position) noexcept {
    if (!(position > 0.0)) return static_cast<double>(kept.front());
    const double last = static_cast<double>(kept.size() - 1U);
    if (position >= last) return static_cast<double>(kept.back());
    const std::size_t cell = static_cast<std::size_t>(position);
    const double fraction = position - static_cast<double>(cell);
    const double low = static_cast<double>(kept[cell]);
    const double high = static_cast<double>(kept[cell + 1U]);
    const double result = low + (high - low) * fraction;
    return result;
}

// -- validation ----------------------------------------------------------------

void check_points(std::span<const double> x, std::span<const double> y, std::span<const double> z, const char* what) {
    if (x.empty() || x.size() != y.size() || x.size() != z.size()) {
        throw std::invalid_argument(std::string(what) + ": x, y and z must have one equal, nonzero length");
    }
    if (x.size() > maximum_points) throw std::invalid_argument(std::string(what) + ": too many points");
    if (!finite_or_nan(x) || !finite_or_nan(y) || !finite_or_nan(z)) {
        throw std::invalid_argument(std::string(what) + ": coordinates must be finite or NaN");
    }
}

void check_surface(const Plot3DSurfaceSpec& spec) {
    if (spec.rows < 2U || spec.columns < 2U) throw std::invalid_argument("Plot3D surface: at least 2 rows and 2 columns");
    if (spec.rows > maximum_surface_nodes / spec.columns) throw std::invalid_argument("Plot3D surface: too many nodes");
    const std::size_t nodes = spec.rows * spec.columns;
    if (spec.z.size() != nodes) throw std::invalid_argument("Plot3D surface: z must hold rows * columns values");
    if (spec.x.size() != spec.columns && spec.x.size() != nodes) {
        throw std::invalid_argument("Plot3D surface: x must hold columns or rows * columns values");
    }
    if (spec.y.size() != spec.rows && spec.y.size() != nodes) {
        throw std::invalid_argument("Plot3D surface: y must hold rows or rows * columns values");
    }
    if (spec.row_stride == 0U || spec.column_stride == 0U) throw std::invalid_argument("Plot3D surface: strides start at 1");
    if (!all_finite(spec.x) || !all_finite(spec.y)) throw std::invalid_argument("Plot3D surface: x and y must be finite");
    if (!finite_or_nan(spec.z)) throw std::invalid_argument("Plot3D surface: z must be finite or NaN");
}

// -- data build ----------------------------------------------------------------

[[nodiscard]] Plot3DCheck check_of(stx_raster3d::Status status) noexcept {
    if (status == stx_raster3d::Status::ok) return Plot3DCheck::ok;
    if (status == stx_raster3d::Status::out_of_memory) return Plot3DCheck::out_of_memory;
    return Plot3DCheck::build_failed;
}

void fill_surface_material(State& state, std::size_t slot, Color color) {
    const SceneItem& item = state.items[slot];
    stx_raster3d::Material& material = state.materials[pane_parts + slot];
    material = stx_raster3d::Material{};
    material.lighting.mode = lighting_mode(item.lighting);
    if (item.color.present()) {
        material.source = stx_raster3d::ColorSource::uniform;
        material.color = raster_color(color);
        return;
    }
    const Colormap& map = built_in_colormap(item.colormap);
    const std::size_t first = slot * ramp_size;
    for (std::size_t index = 0U; index < ramp_size; ++index) {
        const double position = static_cast<double>(index) / static_cast<double>(ramp_size - 1U);
        state.ramps[first + index] = raster_color(sample_colormap(map, position));
    }
    const PlotRange values = widened(item.values);
    material.source = stx_raster3d::ColorSource::vertex_value;
    material.colormap = std::span<const stx_raster3d::Color>(state.ramps.data() + first, ramp_size);
    material.value_minimum = values.minimum;
    material.value_maximum = values.maximum;
}

// Meshes surface `slot`: its kept nodes in box units, holes where z is NaN.
[[nodiscard]] Plot3DCheck build_surface(State& state, std::size_t slot, Color color) {
    SceneItem& item = state.items[slot];
    kept_nodes(item.rows, item.row_stride, item.row_nodes);
    kept_nodes(item.columns, item.column_stride, item.column_nodes);
    const std::size_t nodes_v = item.row_nodes.size();
    const std::size_t nodes_u = item.column_nodes.size();
    const bool conv = item.interpolation == Plot3DInterpolation::conv;
    const std::size_t channels = conv ? 4U : 3U;
    state.node_scratch.assign(nodes_u * nodes_v * channels, 0.0);
    state.value_scratch.assign(conv ? 0U : nodes_u * nodes_v, 0.0);
    state.hidden_scratch.assign((nodes_u - 1U) * (nodes_v - 1U), std::uint8_t{0});
    bool holes = false;
    const double middle = (item.values.minimum <= item.values.maximum) ? 0.5 * (item.values.minimum + item.values.maximum)
                                                                        : 0.0;
    for (std::size_t v = 0U; v < nodes_v; ++v) {
        for (std::size_t u = 0U; u < nodes_u; ++u) {
            const std::size_t source = item.row_nodes[v] * item.columns + item.column_nodes[u];
            const std::size_t node = v * nodes_u + u;
            const double height = item.z[source];
            const bool hole = !std::isfinite(height);
            const std::size_t at = node * channels;
            state.node_scratch[at] = to_box(state, 0U, item.x[source]);
            state.node_scratch[at + 1U] = to_box(state, 1U, item.y[source]);
            if (conv) {
                // CONV takes finite nodes only: a hole's node sits at the
                // middle height and its cells are hidden.
                const double placed = hole ? middle : height;
                state.node_scratch[at + 2U] = to_box(state, 2U, placed);
                state.node_scratch[at + 3U] = placed;
            } else {
                state.node_scratch[at + 2U] = hole ? std::numeric_limits<double>::quiet_NaN() : to_box(state, 2U, height);
                state.value_scratch[node] = height;
            }
            if (!hole || !conv) continue;
            holes = true;
            const std::size_t first_u = u > 0U ? u - 1U : 0U;
            const std::size_t last_u = std::min(u, nodes_u - 2U);
            const std::size_t first_v = v > 0U ? v - 1U : 0U;
            const std::size_t last_v = std::min(v, nodes_v - 2U);
            for (std::size_t cell_v = first_v; cell_v <= last_v; ++cell_v) {
                for (std::size_t cell_u = first_u; cell_u <= last_u; ++cell_u) {
                    state.hidden_scratch[cell_v * (nodes_u - 1U) + cell_u] = 1U;
                }
            }
        }
    }
    fill_surface_material(state, slot, color);
    const std::uint32_t part = static_cast<std::uint32_t>(pane_parts + slot);
    if (!conv) {
        item.conv.reset();
        stx_raster3d::GridInput grid{};
        grid.positions = state.node_scratch;
        grid.values = state.value_scratch;
        grid.nodes_u = nodes_u;
        grid.nodes_v = nodes_v;
        grid.shading = item.interpolation == Plot3DInterpolation::flat ? stx_raster3d::GridShading::flat
                                                                        : stx_raster3d::GridShading::smooth;
        grid.part = part;
        grid.color = raster_color(color);
        const stx_raster3d::Status made = stx_raster3d::triangulate_grid(grid, state.mesh);
        return check_of(made);
    }
    stx_conv::surface::SurfaceShape shape{};
    shape.nodes_u = nodes_u;
    shape.nodes_v = nodes_v;
    shape.channels = 4U;
    shape.periodic_u = false;
    shape.closure = stx_conv::surface::Closure::cubic;
    shape.bubble = stx_conv::surface::Bubble::continuous;
    // The foreign constructor allocates and reports by exception.
    try {
        item.conv = std::make_unique<stx_conv::surface::Surface>(shape);
    } catch (const std::bad_alloc&) {
        item.conv.reset();
        return Plot3DCheck::out_of_memory;
    } catch (const std::exception&) {
        item.conv.reset();
        return Plot3DCheck::build_failed;
    }
    const stx_conv::Status compiled = (*item.conv).compile(state.node_scratch, std::span<const std::uint8_t>{},
                                                           std::span<const std::uint8_t>{});
    if (compiled != stx_conv::Status::ok) return Plot3DCheck::build_failed;
    stx_raster3d::TessellationOptions options{};
    options.tolerance = tessellation_tolerance;
    options.maximum_depth = tessellation_depth;
    options.value_channel = 3U;
    if (holes) options.hidden_cells = state.hidden_scratch;
    options.part = part;
    options.color = raster_color(color);
    const stx_raster3d::Status made =
        stx_raster3d::tessellate_surface(*item.conv, options, stx_raster3d::system_allocator(), state.mesh);
    return check_of(made);
}

// Places line `slot` in the box: a strip per run of finite points (a lone
// point draws nothing).
void build_line(State& state, std::size_t slot, Color color) {
    const SceneItem& item = state.items[slot];
    stx_raster3d::LineStrip strip{};
    strip.color = raster_color(color);
    strip.width = static_cast<float>(item.width);
    strip.item = static_cast<std::uint32_t>(slot);
    bool open = false;
    std::uint32_t first_point = 0U;
    for (std::size_t index = 0U; index <= item.x.size(); ++index) {
        const bool present = index < item.x.size() && std::isfinite(item.x[index]) && std::isfinite(item.y[index]) &&
                             std::isfinite(item.z[index]);
        if (present) {
            if (!open) {
                open = true;
                strip.first = static_cast<std::uint32_t>(state.data_points.size());
                first_point = static_cast<std::uint32_t>(index);
            }
            const stx_raster3d::Vec3 point{to_box(state, 0U, item.x[index]), to_box(state, 1U, item.y[index]),
                                           to_box(state, 2U, item.z[index])};
            state.data_points.push_back(point);
            continue;
        }
        if (!open) continue;
        open = false;
        strip.count = static_cast<std::uint32_t>(state.data_points.size()) - strip.first;
        if (strip.count < 2U) {
            state.data_points.pop_back();
            continue;
        }
        state.data_strips.push_back(strip);
        state.strip_points.push_back(first_point);
    }
}

void build_scatter(State& state, std::size_t slot, Color color) {
    const SceneItem& item = state.items[slot];
    stx_raster3d::Marker marker{};
    marker.color = raster_color(color);
    marker.size = static_cast<float>(item.size);
    marker.shape = marker_shape(item.marker);
    marker.item = static_cast<std::uint32_t>(slot);
    for (std::size_t index = 0U; index < item.x.size(); ++index) {
        if (!std::isfinite(item.x[index]) || !std::isfinite(item.y[index]) || !std::isfinite(item.z[index])) continue;
        marker.position = stx_raster3d::Vec3{to_box(state, 0U, item.x[index]), to_box(state, 1U, item.y[index]),
                                             to_box(state, 2U, item.z[index])};
        state.data_markers.push_back(marker);
        state.marker_points.push_back(static_cast<std::uint32_t>(index));
    }
}

// Rebuilds the data mesh, lines and markers. On failure the data stays
// stale (the next render tries again) and the mesh is left empty.
[[nodiscard]] Plot3DCheck build_data(State& state) {
    state.limits = data_limits(state.items);
    const std::array<double, 3>& aspect = state.options.box_aspect;
    const double largest = std::max(aspect[0], std::max(aspect[1], aspect[2]));
    for (std::size_t axis = 0U; axis < 3U; ++axis) state.half[axis] = aspect[axis] / largest;
    state.mesh.clear();
    state.data_points.clear();
    state.data_strips.clear();
    state.strip_points.clear();
    state.data_markers.clear();
    state.marker_points.clear();
    const std::size_t count = state.items.size();
    state.materials.assign(pane_parts + count, stx_raster3d::Material{});
    state.ramps.assign(count * ramp_size, stx_raster3d::Color{});
    for (std::size_t axis = 0U; axis < pane_parts; ++axis) {
        state.materials[axis].source = stx_raster3d::ColorSource::uniform;
        state.materials[axis].color = raster_color(state.colors.panes[axis]);
        state.materials[axis].lighting.mode = stx_raster3d::LightingMode::none;
    }
    std::size_t series = 0U;
    for (std::size_t slot = 0U; slot < count; ++slot) {
        const SceneItem& item = state.items[slot];
        const Color color = item.color.value_or(state.colors.series[series % Plot3DColors::series_count]);
        series = series + 1U;
        switch (item.kind) {
        case ItemKind::surface: {
            const Plot3DCheck built = build_surface(state, slot, color);
            if (built != Plot3DCheck::ok) {
                state.mesh.clear();
                return built;
            }
            break;
        }
        case ItemKind::line:
            build_line(state, slot, color);
            break;
        case ItemKind::scatter:
        default:
            build_scatter(state, slot, color);
            break;
        }
    }
    state.data_mark = state.mesh.mark();
    state.data_dirty = false;
    state.mesh_builds = state.mesh_builds + 1U;
    return Plot3DCheck::ok;
}

// -- view ------------------------------------------------------------------------

// The camera and view for a picture of `extent`, and the eye's distance
// from the target; false (both unchanged) when the view cannot be made.
[[nodiscard]] bool make_scene_view(const State& state, DeviceExtent extent, stx_raster3d::View& view,
                                   double& eye_distance) {
    const double width = static_cast<double>(extent.width);
    const double height = static_cast<double>(extent.height);
    const double fit = std::min(width, height) / height;
    const double radius = std::sqrt(state.half[0] * state.half[0] + state.half[1] * state.half[1] +
                                    state.half[2] * state.half[2]);
    const Plot3DCamera& camera = state.camera;
    // At this distance the bounding sphere fills fit_fraction of the half
    // picture whatever the focal length: tan(asin(r / d)) * f = fraction.
    const double focal = camera.focal_length;
    const double distance = radius * std::sqrt(focal * focal / (fit_fraction * fit_fraction) + 1.0);
    const stx_raster3d::Projection projection = camera.projection == Plot3DProjection::orthographic
                                                    ? stx_raster3d::Projection::orthographic
                                                    : stx_raster3d::Projection::perspective;
    stx_raster3d::Camera placed = stx_raster3d::orbit_camera(vector_of(camera.target), distance, camera.elevation,
                                                             camera.azimuth, camera.roll, projection);
    placed.focal_length = focal * camera.zoom * fit;
    placed.orthographic_height = 2.0 * radius / (fit_fraction * camera.zoom * fit);
    placed.near = distance * 1e-3;
    const stx_raster3d::Status made = stx_raster3d::make_view(placed, extent.width, extent.height, view);
    if (made != stx_raster3d::Status::ok) return false;
    eye_distance = distance;
    return true;
}

// Box units per logical unit at the target's depth, in the last view.
[[nodiscard]] double world_per_logical(const State& state) noexcept {
    const double depth = state.view.projection() == stx_raster3d::Projection::perspective ? state.distance : 1.0;
    const double result = depth * state.view_scale / state.view.scale();
    return result;
}

void append_panes(State& state) {
    if (!state.options.show_panes) return;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const std::size_t first = (axis + 1U) % 3U;
        const std::size_t second = (axis + 2U) % 3U;
        std::array<stx_raster3d::MeshVertex, 4> corners{};
        const std::array<double, 4> along_first{-1.0, 1.0, 1.0, -1.0};
        const std::array<double, 4> along_second{-1.0, -1.0, 1.0, 1.0};
        for (std::size_t corner = 0U; corner < 4U; ++corner) {
            stx_raster3d::Vec3 position{};
            set_component(position, axis, state.back_side[axis] * state.half[axis]);
            set_component(position, first, along_first[corner] * state.half[first]);
            set_component(position, second, along_second[corner] * state.half[second]);
            stx_raster3d::Vec3 normal{};
            set_component(normal, axis, -state.back_side[axis]);
            corners[corner].position = position;
            corners[corner].normal = normal;
        }
        const std::array<std::uint32_t, 6> indices{0U, 1U, 2U, 0U, 2U, 3U};
        static_cast<void>(state.mesh.append_triangles(corners, indices, static_cast<std::uint32_t>(axis)));
    }
}

// An axis's edge of the box, its outward direction for ticks and labels.
struct AxisEdge final {
    stx_raster3d::Vec3 start{};
    stx_raster3d::Vec3 end{};
    stx_raster3d::Vec3 outward{};
};

[[nodiscard]] double project_x(const stx_raster3d::View& view, const stx_raster3d::Vec3& point) noexcept {
    const stx_raster3d::ProjectedPoint projected = view.project(point);
    return projected.x;
}

// matplotlib's placement: x and y on the panes' lower front edges, z on the
// left of the two vertical edges at the box's silhouette.
[[nodiscard]] AxisEdge axis_edge(const State& state, const stx_raster3d::View& view, std::size_t axis) {
    const std::array<double, 3>& back = state.back_side;
    const std::array<double, 3>& half = state.half;
    AxisEdge edge{};
    stx_raster3d::Vec3 at{};
    if (axis == 0U) {
        at = stx_raster3d::Vec3{0.0, -back[1] * half[1], back[2] * half[2]};
        edge.outward = stx_raster3d::Vec3{0.0, -back[1], 0.0};
    } else if (axis == 1U) {
        at = stx_raster3d::Vec3{-back[0] * half[0], 0.0, back[2] * half[2]};
        edge.outward = stx_raster3d::Vec3{-back[0], 0.0, 0.0};
    } else {
        const stx_raster3d::Vec3 one{back[0] * half[0], -back[1] * half[1], 0.0};
        const stx_raster3d::Vec3 other{-back[0] * half[0], back[1] * half[1], 0.0};
        const bool first = project_x(view, one) <= project_x(view, other);
        at = first ? one : other;
        // Outward along whichever horizontal axis points further left.
        const stx_raster3d::Vec3 along_x{at.x > 0.0 ? 1.0 : -1.0, 0.0, 0.0};
        const stx_raster3d::Vec3 along_y{0.0, at.y > 0.0 ? 1.0 : -1.0, 0.0};
        const double left_x = project_x(view, at + along_x * 0.1) - project_x(view, at);
        const double left_y = project_x(view, at + along_y * 0.1) - project_x(view, at);
        edge.outward = left_x <= left_y ? along_x : along_y;
    }
    edge.start = at;
    edge.end = at;
    set_component(edge.start, axis, -half[axis]);
    set_component(edge.end, axis, half[axis]);
    return edge;
}

// Tick label extents along an axis that runs at an angle on screen: the
// caller's widths across, the label height up and down.
class AxisLabelMetrics final : public TickLabelMetrics {
public:
    AxisLabelMetrics(const TickLabelMetrics& widths, double across, double upright) noexcept
        : widths_(widths), across_(across), upright_(upright) {}

    [[nodiscard]] double label_extent(std::string_view label) const override {
        const double extent = widths_.label_extent(label) * across_ + tick_label_height * upright_;
        return extent;
    }

private:
    const TickLabelMetrics& widths_;
    double across_{};
    double upright_{};
};

struct ScreenPoint final {
    double x{};
    double y{};
    double depth{};
};

[[nodiscard]] ScreenPoint logical_point(const stx_raster3d::View& view, double scale,
                                        const stx_raster3d::Vec3& point) noexcept {
    const stx_raster3d::ProjectedPoint projected = view.project(point);
    const ScreenPoint result{projected.x / scale, projected.y / scale, projected.depth};
    return result;
}

// The unit screen direction of `outward` from `at`, with a fallback when it
// points along the view.
[[nodiscard]] Point screen_direction(const stx_raster3d::View& view, double scale,
                                     const stx_raster3d::Vec3& at, const stx_raster3d::Vec3& outward, Point fallback) {
    const ScreenPoint from = logical_point(view, scale, at);
    const ScreenPoint to = logical_point(view, scale, at + outward * 0.1);
    const double dx = to.x - from.x;
    const double dy = to.y - from.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (!(length > 1e-6)) return fallback;
    const Point direction{dx / length, dy / length};
    return direction;
}

void push_segment(State& state, const stx_raster3d::Vec3& from, const stx_raster3d::Vec3& to, Color color, double width) {
    stx_raster3d::LineStrip strip{};
    strip.first = static_cast<std::uint32_t>(state.points.size());
    strip.count = 2U;
    strip.color = raster_color(color);
    strip.width = static_cast<float>(width);
    strip.item = no_item;
    state.points.push_back(from);
    state.points.push_back(to);
    state.strips.push_back(strip);
}

// Ticks for every axis, the grid on the panes and the axis lines with their
// tick marks, after the data's lines.
void build_view_lines(State& state, const stx_raster3d::View& view, const Plot3DFrame& frame,
                      const std::array<AxisEdge, 3>& edges) {
    const double scale = frame.scale.value;
    const EstimatedLabelMetrics estimate(6.5, 2.0);
    const TickLabelMetrics& widths = frame.metrics != nullptr ? *frame.metrics : estimate;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        state.ticks_valid[axis] = false;
        const ScreenPoint from = logical_point(view, scale, edges[axis].start);
        const ScreenPoint to = logical_point(view, scale, edges[axis].end);
        const double dx = to.x - from.x;
        const double dy = to.y - from.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (!(length >= 1.0)) continue;
        Scale mapping_scale{};
        mapping_scale.kind = ScaleKind::linear;
        mapping_scale.domain = state.limits[axis];
        mapping_scale.device_start = 0.0;
        mapping_scale.device_end = length;
        const ScaleMappingResult made = ScaleMapping::make(mapping_scale);
        if (made.check != ScaleCheck::valid) continue;
        const double across = std::abs(dx) / length;
        const double upright = std::abs(dy) / length;
        const AxisLabelMetrics metrics(widths, across, upright);
        TickRequest request{};
        request.minimum_spacing = 30.0 + 26.0 * across;
        const TickOutcome outcome = generate_ticks(made.mapping, request, &metrics, state.ticks[axis]);
        state.ticks_valid[axis] = outcome != TickOutcome::too_small;
    }
    // The data's lines first, widths in device pixels.
    state.points.assign(state.data_points.begin(), state.data_points.end());
    state.strips.assign(state.data_strips.begin(), state.data_strips.end());
    for (stx_raster3d::LineStrip& strip : state.strips) strip.width = static_cast<float>(static_cast<double>(strip.width) * scale);
    state.markers.assign(state.data_markers.begin(), state.data_markers.end());
    for (stx_raster3d::Marker& marker : state.markers) marker.size = static_cast<float>(static_cast<double>(marker.size) * scale);
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        if (!state.ticks_valid[axis]) continue;
        const std::span<const MajorTick> majors = state.ticks[axis].majors();
        for (const MajorTick& tick : majors) {
            const double at = to_box(state, axis, tick.value);
            if (std::abs(at) > state.half[axis] * (1.0 + 1e-9)) continue;
            if (state.options.show_grid) {
                for (std::size_t offset = 1U; offset < 3U; ++offset) {
                    const std::size_t pane = (axis + offset) % 3U;
                    const std::size_t across = (axis + 3U - offset) % 3U;
                    stx_raster3d::Vec3 from{};
                    set_component(from, axis, at);
                    set_component(from, pane, state.back_side[pane] * state.half[pane]);
                    set_component(from, across, -state.half[across]);
                    stx_raster3d::Vec3 to = from;
                    set_component(to, across, state.half[across]);
                    push_segment(state, from, to, state.colors.grid, grid_width * scale);
                }
            }
            if (state.options.show_axes) {
                stx_raster3d::Vec3 base = edges[axis].start;
                set_component(base, axis, at);
                push_segment(state, base, base + edges[axis].outward * tick_length, state.colors.axis, axis_width * scale);
            }
        }
    }
    if (!state.options.show_axes) return;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        push_segment(state, edges[axis].start, edges[axis].end, state.colors.axis, axis_width * scale);
    }
}

// Whether data lies in front of `point` at its pixel in the last picture.
[[nodiscard]] bool hidden_by_data(const State& state, double scale, const ScreenPoint& point) {
    const double x = point.x * scale;
    const double y = point.y * scale;
    if (!(x >= 0.0) || !(y >= 0.0)) return false;
    const std::size_t column = static_cast<std::size_t>(x);
    const std::size_t row = static_cast<std::size_t>(y);
    if (column >= state.renderer.width() || row >= state.renderer.height()) return false;
    const stx_raster3d::Pick found = state.renderer.pick(column, row, &state.mesh);
    bool data = false;
    if (found.kind == stx_raster3d::PickKind::surface) data = found.item >= pane_parts;
    if (found.kind == stx_raster3d::PickKind::line) data = found.index < state.data_strips.size();
    if (found.kind == stx_raster3d::PickKind::marker) data = true;
    const bool hidden = data && found.depth + occlusion_slack < point.depth;
    return hidden;
}

// The angle, in degrees anticlockwise as seen, of a screen direction
// (dx across, dy down the surface), turned by half a turn when needed so
// text along it reads left to right: within (-90, 90]. A direction too short
// to have one is 0.
[[nodiscard]] double readable_edge_angle(double dx, double dy) noexcept {
    if (!(std::abs(dx) + std::abs(dy) > 1e-9)) return 0.0;
    double angle = std::atan2(-dy, dx) * degrees_per_radian;
    if (angle > half_turn_degrees * 0.5) angle -= half_turn_degrees;
    if (angle <= -half_turn_degrees * 0.5) angle += half_turn_degrees;
    return angle;
}

void lay_out_labels(State& state, const stx_raster3d::View& view, const Plot3DFrame& frame,
                    const std::array<AxisEdge, 3>& edges) {
    state.labels.clear();
    if (!state.options.show_axes) return;
    const double scale = frame.scale.value;
    const EstimatedLabelMetrics estimate(6.5, 2.0);
    const TickLabelMetrics& widths = frame.metrics != nullptr ? *frame.metrics : estimate;
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        const Point fallback = axis == 2U ? Point{-1.0, 0.0} : Point{0.0, 1.0};
        const Point direction = screen_direction(view, scale, edges[axis].start, edges[axis].outward, fallback);
        const ScreenPoint tick_from = logical_point(view, scale, edges[axis].start);
        const ScreenPoint tick_to = logical_point(view, scale, edges[axis].start + edges[axis].outward * tick_length);
        const double tick_pixels = std::sqrt((tick_to.x - tick_from.x) * (tick_to.x - tick_from.x) +
                                             (tick_to.y - tick_from.y) * (tick_to.y - tick_from.y));
        double reach = 0.0;
        if (state.ticks_valid[axis]) {
            const std::span<const MajorTick> majors = state.ticks[axis].majors();
            for (const MajorTick& tick : majors) {
                if (!tick.labelled) continue;
                const double at = to_box(state, axis, tick.value);
                if (std::abs(at) > state.half[axis] * (1.0 + 1e-9)) continue;
                stx_raster3d::Vec3 base = edges[axis].start;
                set_component(base, axis, at);
                const ScreenPoint place = logical_point(view, scale, base);
                const double width = widths.label_extent(tick.label.text());
                const double half_extent = std::abs(direction.x) * width * 0.5 + std::abs(direction.y) * tick_label_height * 0.5;
                const double offset = tick_pixels + label_gap + half_extent;
                reach = std::max(reach, half_extent * 2.0);
                Plot3DLabel label{};
                label.role = Plot3DLabelRole::tick;
                label.axis = static_cast<Plot3DAxis>(axis);
                label.text = tick.label;
                label.anchor = Point{place.x + direction.x * offset, place.y + direction.y * offset};
                label.occluded = hidden_by_data(state, scale, place);
                state.labels.push_back(label);
            }
        }
        if (state.options.axis_titles[axis].empty()) continue;
        const stx_raster3d::Vec3 middle = (edges[axis].start + edges[axis].end) * 0.5;
        const ScreenPoint place = logical_point(view, scale, middle);
        const ScreenPoint start = logical_point(view, scale, edges[axis].start);
        const ScreenPoint end = logical_point(view, scale, edges[axis].end);
        const bool along_axis = state.options.label_alignment == Plot3DLabelAlignment::along_axis;
        const double angle = along_axis ? readable_edge_angle(end.x - start.x, end.y - start.y) : 0.0;
        // The title's half extent along the outward direction, as turned: its
        // width along the turned baseline and its height across it.
        const double radians = angle / degrees_per_radian;
        const double title_width = widths.label_extent(state.options.axis_titles[axis]);
        const double along_extent = std::abs(direction.x * std::cos(radians) - direction.y * std::sin(radians));
        const double across_extent = std::abs(direction.x * std::sin(radians) + direction.y * std::cos(radians));
        const double half_extent = along_extent * title_width * 0.5 + across_extent * tick_label_height * 0.5;
        const double offset = tick_pixels + label_gap + reach + title_gap + half_extent;
        Plot3DLabel title{};
        title.role = Plot3DLabelRole::title;
        title.axis = static_cast<Plot3DAxis>(axis);
        title.anchor = Point{place.x + direction.x * offset, place.y + direction.y * offset};
        title.occluded = hidden_by_data(state, scale, place);
        title.angle = angle;
        state.labels.push_back(title);
    }
}

[[nodiscard]] std::size_t slot_of(const State& state, Plot3DItemId item) noexcept {
    for (std::size_t slot = 0U; slot < state.items.size(); ++slot) {
        if (state.items[slot].id == item) return slot;
    }
    return state.items.size();
}

Plot3DItemId add_item(State& state, SceneItem&& item) {
    item.id = Plot3DItemId{state.next_id};
    state.items.push_back(std::move(item));
    state.next_id = state.next_id + 1U;
    state.data_dirty = true;
    state.picture_items_current = false;
    return state.items.back().id;
}

} // namespace

// -- life ---------------------------------------------------------------------------

Plot3DScene::Plot3DScene() : state_(std::make_unique<detail::Plot3DSceneState>()) {
    (*state_).labels.reserve(3U * (maximum_major_ticks + 1U));
}

Plot3DScene::~Plot3DScene() = default;
Plot3DScene::Plot3DScene(Plot3DScene&& other) noexcept = default;
Plot3DScene& Plot3DScene::operator=(Plot3DScene&& other) noexcept = default;

// -- items --------------------------------------------------------------------------

Plot3DItemId Plot3DScene::add_surface(const Plot3DSurfaceSpec& spec) {
    check_surface(spec);
    SceneItem item{};
    item.kind = ItemKind::surface;
    item.rows = spec.rows;
    item.columns = spec.columns;
    const std::size_t nodes = spec.rows * spec.columns;
    item.x.resize(nodes);
    item.y.resize(nodes);
    for (std::size_t row = 0U; row < spec.rows; ++row) {
        for (std::size_t column = 0U; column < spec.columns; ++column) {
            const std::size_t node = row * spec.columns + column;
            item.x[node] = spec.x.size() == nodes ? spec.x[node] : spec.x[column];
            item.y[node] = spec.y.size() == nodes ? spec.y[node] : spec.y[row];
        }
    }
    item.z.assign(spec.z.begin(), spec.z.end());
    item.row_stride = spec.row_stride;
    item.column_stride = spec.column_stride;
    item.interpolation = spec.interpolation;
    item.colormap = spec.colormap;
    item.color = spec.color;
    item.lighting = spec.lighting;
    item.label = std::string(spec.label);
    item.values = finite_range(item.z);
    const Plot3DItemId added = add_item(*state_, std::move(item));
    return added;
}

Plot3DItemId Plot3DScene::add_line(const Plot3DLineSpec& spec) {
    check_points(spec.x, spec.y, spec.z, "Plot3D line");
    if (!(spec.width > 0.0) || spec.width > maximum_line_width) {
        throw std::invalid_argument("Plot3D line: width must be above 0 and at most 32");
    }
    SceneItem item{};
    item.kind = ItemKind::line;
    item.x.assign(spec.x.begin(), spec.x.end());
    item.y.assign(spec.y.begin(), spec.y.end());
    item.z.assign(spec.z.begin(), spec.z.end());
    item.color = spec.color;
    item.width = spec.width;
    item.label = std::string(spec.label);
    item.values = finite_range(item.z);
    const Plot3DItemId added = add_item(*state_, std::move(item));
    return added;
}

Plot3DItemId Plot3DScene::add_scatter(const Plot3DScatterSpec& spec) {
    check_points(spec.x, spec.y, spec.z, "Plot3D scatter");
    if (!(spec.size > 0.0) || spec.size > maximum_marker_size) {
        throw std::invalid_argument("Plot3D scatter: size must be above 0 and at most 64");
    }
    SceneItem item{};
    item.kind = ItemKind::scatter;
    item.x.assign(spec.x.begin(), spec.x.end());
    item.y.assign(spec.y.begin(), spec.y.end());
    item.z.assign(spec.z.begin(), spec.z.end());
    item.color = spec.color;
    item.size = spec.size;
    item.marker = spec.marker;
    item.label = std::string(spec.label);
    item.values = finite_range(item.z);
    const Plot3DItemId added = add_item(*state_, std::move(item));
    return added;
}

bool Plot3DScene::remove(Plot3DItemId item) {
    State& state = *state_;
    const std::size_t slot = slot_of(state, item);
    if (slot == state.items.size()) return false;
    state.items.erase(state.items.begin() + static_cast<std::ptrdiff_t>(slot));
    state.data_dirty = true;
    state.picture_items_current = false;
    return true;
}

void Plot3DScene::clear() {
    State& state = *state_;
    state.items.clear();
    state.data_dirty = true;
    state.picture_items_current = false;
}

std::size_t Plot3DScene::item_count() const noexcept {
    return (*state_).items.size();
}

std::string_view Plot3DScene::item_label(Plot3DItemId item) const& noexcept {
    const State& state = *state_;
    const std::size_t slot = slot_of(state, item);
    if (slot == state.items.size()) return std::string_view{};
    const std::string_view label = state.items[slot].label;
    return label;
}

PlotRange Plot3DScene::limits(Plot3DAxis axis) const {
    const std::array<PlotRange, 3> all = data_limits((*state_).items);
    const PlotRange range = all[static_cast<std::size_t>(axis)];
    return range;
}

// -- view ---------------------------------------------------------------------------

const Plot3DCamera& Plot3DScene::camera() const noexcept {
    return (*state_).camera;
}

bool Plot3DScene::set_camera(const Plot3DCamera& camera) {
    const bool finite = std::isfinite(camera.elevation) && std::isfinite(camera.azimuth) && std::isfinite(camera.roll) &&
                        std::isfinite(camera.zoom) && std::isfinite(camera.focal_length) &&
                        std::isfinite(camera.target[0]) && std::isfinite(camera.target[1]) &&
                        std::isfinite(camera.target[2]);
    if (!finite) return false;
    Plot3DCamera taken = camera;
    taken.elevation = wrapped_degrees(camera.elevation);
    taken.azimuth = wrapped_degrees(camera.azimuth);
    taken.roll = wrapped_degrees(camera.roll);
    taken.zoom = clamped(camera.zoom, minimum_zoom, maximum_zoom);
    taken.focal_length = clamped(camera.focal_length, minimum_focal_length, maximum_focal_length);
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        taken.target[axis] = clamped(camera.target[axis], -maximum_target, maximum_target);
    }
    if (taken == (*state_).camera) return false;
    (*state_).camera = taken;
    return true;
}

bool Plot3DScene::reset_camera() {
    Plot3DCamera initial{};
    initial.projection = (*state_).camera.projection;
    initial.focal_length = (*state_).camera.focal_length;
    const bool changed = set_camera(initial);
    return changed;
}

const Plot3DOptions& Plot3DScene::options() const noexcept {
    return (*state_).options;
}

void Plot3DScene::set_options(const Plot3DOptions& options) {
    for (const double side : options.box_aspect) {
        if (!std::isfinite(side) || !(side > 0.0)) throw std::invalid_argument("Plot3D box aspect must be positive and finite");
    }
    State& state = *state_;
    if (options == state.options) return;
    const bool box = !(options.box_aspect == state.options.box_aspect);
    state.options = options;
    if (box) state.data_dirty = true;
}

const Plot3DColors& Plot3DScene::colors() const noexcept {
    return (*state_).colors;
}

void Plot3DScene::set_colors(const Plot3DColors& colors) {
    State& state = *state_;
    if (colors == state.colors) return;
    state.colors = colors;
    state.data_dirty = true;
}

// -- gestures -----------------------------------------------------------------------

bool Plot3DScene::rotate_by(double dx, double dy) {
    const State& state = *state_;
    if (!std::isfinite(dx) || !std::isfinite(dy)) return false;
    const double width = state.view_width > 0.0 ? state.view_width : 1.0;
    const double height = state.view_height > 0.0 ? state.view_height : 1.0;
    // matplotlib's azel rotation, with the screen's y pointing down.
    const double roll = state.camera.roll / degrees_per_radian;
    const double across = dx / width * half_turn_degrees;
    const double down = dy / height * half_turn_degrees;
    Plot3DCamera turned = state.camera;
    turned.elevation = state.camera.elevation + down * std::cos(roll) + across * std::sin(roll);
    turned.azimuth = state.camera.azimuth + down * std::sin(roll) - across * std::cos(roll);
    const bool changed = set_camera(turned);
    return changed;
}

bool Plot3DScene::pan_by(double dx, double dy) {
    const State& state = *state_;
    if (!state.rendered || !std::isfinite(dx) || !std::isfinite(dy)) return false;
    const double step = world_per_logical(state);
    const stx_raster3d::Vec3 moved = vector_of(state.camera.target) - state.view.right() * (dx * step) +
                                     state.view.upward() * (dy * step);
    Plot3DCamera panned = state.camera;
    panned.target = std::array<double, 3>{moved.x, moved.y, moved.z};
    const bool changed = set_camera(panned);
    return changed;
}

bool Plot3DScene::zoom_by(double factor, std::optional<Point> about) {
    const State& state = *state_;
    if (!std::isfinite(factor) || !(factor > 0.0)) return false;
    const double zoom = clamped(state.camera.zoom * factor, minimum_zoom, maximum_zoom);
    const double applied = zoom / state.camera.zoom;
    Plot3DCamera zoomed = state.camera;
    zoomed.zoom = zoom;
    if (about.has_value() && state.rendered) {
        // The point under `about` on the target's plane stays put: the
        // target moves towards it by (1 - 1 / applied) of its offset.
        const double across = (*about).x - state.view_width * 0.5;
        const double down = (*about).y - state.view_height * 0.5;
        const double step = world_per_logical(state) * (1.0 - 1.0 / applied);
        const stx_raster3d::Vec3 moved = vector_of(state.camera.target) + state.view.right() * (across * step) -
                                         state.view.upward() * (down * step);
        zoomed.target = std::array<double, 3>{moved.x, moved.y, moved.z};
    }
    const bool changed = set_camera(zoomed);
    return changed;
}

// -- picture ------------------------------------------------------------------------

DeviceExtent Plot3DScene::device_extent(const Plot3DFrame& frame) noexcept {
    const double scale = frame.scale.value;
    const bool usable = std::isfinite(frame.width) && std::isfinite(frame.height) && std::isfinite(scale) &&
                        frame.width > 0.0 && frame.height > 0.0 && scale >= minimum_device_scale &&
                        scale <= maximum_device_scale;
    if (!usable) return DeviceExtent{};
    const double width = std::round(frame.width * scale);
    const double height = std::round(frame.height * scale);
    if (!(width >= 1.0) || !(height >= 1.0)) return DeviceExtent{};
    if (width > static_cast<double>(maximum_side) || height > static_cast<double>(maximum_side)) return DeviceExtent{};
    const DeviceExtent extent{static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height)};
    return extent;
}

Plot3DCheck Plot3DScene::render(const Plot3DFrame& frame, const PartRasterView& destination) {
    State& state = *state_;
    const DeviceExtent extent = device_extent(frame);
    if (extent.width == 0U || frame.supersample < 1U || frame.supersample > stx_raster3d::Renderer::maximum_supersample) {
        return Plot3DCheck::invalid_frame;
    }
    if (validate_raster_view(destination) != RasterViewDefect::none || !(destination.extent == extent)) {
        return Plot3DCheck::invalid_frame;
    }
    if (state.data_dirty) {
        const Plot3DCheck built = build_data(state);
        if (built != Plot3DCheck::ok) return built;
    }
    stx_raster3d::View view{};
    double distance = 1.0;
    if (!make_scene_view(state, extent, view, distance)) return Plot3DCheck::invalid_frame;

    // The back panes face the eye: each sits on the side of the box away
    // from it.
    const stx_raster3d::Vec3 eye = view.eye();
    for (std::size_t axis = 0U; axis < 3U; ++axis) state.back_side[axis] = component(eye, axis) > 0.0 ? -1.0 : 1.0;
    const stx_raster3d::Status truncated = state.mesh.truncate(state.data_mark);
    if (truncated != stx_raster3d::Status::ok) return Plot3DCheck::build_failed;
    append_panes(state);
    std::array<AxisEdge, 3> edges{};
    for (std::size_t axis = 0U; axis < 3U; ++axis) edges[axis] = axis_edge(state, view, axis);
    build_view_lines(state, view, frame, edges);

    stx_raster3d::Scene scene{};
    scene.mesh = &state.mesh;
    scene.materials = state.materials;
    scene.line_points = state.points;
    scene.lines = state.strips;
    scene.markers = state.markers;
    const stx_raster3d::Status drawn =
        state.renderer.render(scene, view, frame.supersample, stx_raster3d::inline_executor());
    if (drawn != stx_raster3d::Status::ok) {
        return drawn == stx_raster3d::Status::out_of_memory ? Plot3DCheck::out_of_memory : Plot3DCheck::invalid_frame;
    }
    // The picture's rows are packed; the destination's may be padded.
    const std::span<const std::uint8_t> picture = state.renderer.picture();
    const std::size_t row_length = static_cast<std::size_t>(extent.width) * part_raster_bytes_per_pixel;
    for (std::size_t row = 0U; row < extent.height; ++row) {
        std::memcpy(destination.pixels.data() + row * destination.row_bytes, picture.data() + row * row_length, row_length);
    }
    state.view = view;
    state.distance = distance;
    state.view_width = frame.width;
    state.view_height = frame.height;
    state.view_scale = frame.scale.value;
    state.rendered = true;
    state.picture_items_current = true;
    lay_out_labels(state, view, frame, edges);
    state.renders = state.renders + 1U;
    return Plot3DCheck::ok;
}

std::span<const Plot3DLabel> Plot3DScene::labels() const& noexcept {
    const std::span<const Plot3DLabel> all((*state_).labels);
    return all;
}

Plot3DHit Plot3DScene::hit(Point point) const {
    const State& state = *state_;
    Plot3DHit result{};
    if (!state.rendered || !state.picture_items_current || !std::isfinite(point.x) || !std::isfinite(point.y)) {
        return result;
    }
    const double x = point.x * state.view_scale;
    const double y = point.y * state.view_scale;
    if (!(x >= 0.0) || !(y >= 0.0)) return result;
    const std::size_t column = static_cast<std::size_t>(x);
    const std::size_t row = static_cast<std::size_t>(y);
    const stx_raster3d::Pick found = state.renderer.pick(column, row, &state.mesh);
    result.depth = found.depth;
    switch (found.kind) {
    case stx_raster3d::PickKind::surface: {
        if (found.item < pane_parts) {
            result.kind = Plot3DHitKind::pane;
            result.data_point = data_of(state, found.position);
            return result;
        }
        const std::size_t slot = found.item - pane_parts;
        if (slot >= state.items.size()) return Plot3DHit{};
        const SceneItem& item = state.items[slot];
        // The picture is current, so the slot is this surface; its node
        // tables are still checked before they are indexed.
        if (item.kind != ItemKind::surface || item.row_nodes.empty() || item.column_nodes.empty() || item.rows == 0U ||
            item.columns == 0U) {
            return Plot3DHit{};
        }
        double u = found.u;
        double v = found.v;
        stx_raster3d::Vec3 position = found.position;
        double height = found.value;
        if (item.conv) {
            // Newton refinement onto the interpolant under the pixel's centre.
            double refined_u = u;
            double refined_v = v;
            stx_raster3d::Vec3 refined{};
            const stx_raster3d::Status settled =
                stx_raster3d::refine_surface_pick(*item.conv, state.view, x, y, refined_u, refined_v, refined);
            if (settled == stx_raster3d::Status::ok) {
                std::array<double, 4> channels{};
                const stx_conv::Status evaluated = (*item.conv).evaluate(refined_u, refined_v, channels);
                if (evaluated == stx_conv::Status::ok) {
                    u = refined_u;
                    v = refined_v;
                    position = refined;
                    height = channels[3];
                }
            }
        }
        result.kind = Plot3DHitKind::surface;
        result.item = item.id;
        const double row_at = original_index(item.row_nodes, v);
        const double column_at = original_index(item.column_nodes, u);
        result.surface_parameter = std::array<double, 2>{row_at, column_at};
        const std::size_t nearest_row = static_cast<std::size_t>(std::lround(row_at));
        const std::size_t nearest_column = static_cast<std::size_t>(std::lround(column_at));
        result.index = std::min(nearest_row, item.rows - 1U) * item.columns + std::min(nearest_column, item.columns - 1U);
        result.data_point = std::array<double, 3>{from_box(state, 0U, position.x), from_box(state, 1U, position.y), height};
        result.depth = dot(position - state.view.eye(), state.view.forward());
        return result;
    }
    case stx_raster3d::PickKind::line: {
        if (found.index >= state.data_strips.size()) return Plot3DHit{};
        const stx_raster3d::LineStrip& strip = state.data_strips[found.index];
        const std::size_t slot = strip.item;
        if (slot >= state.items.size()) return Plot3DHit{};
        const SceneItem& item = state.items[slot];
        if (item.kind != ItemKind::line) return Plot3DHit{};
        // The nearer end of the hit segment on screen.
        const std::size_t first = static_cast<std::size_t>(strip.first) + found.segment;
        const std::size_t last = std::min(first + 1U, static_cast<std::size_t>(strip.first + strip.count - 1U));
        const stx_raster3d::ProjectedPoint near_end = state.view.project(state.data_points[first]);
        const stx_raster3d::ProjectedPoint far_end = state.view.project(state.data_points[last]);
        const double to_first = (near_end.x - x) * (near_end.x - x) + (near_end.y - y) * (near_end.y - y);
        const double to_last = (far_end.x - x) * (far_end.x - x) + (far_end.y - y) * (far_end.y - y);
        const std::size_t step = to_last < to_first ? last - first : 0U;
        const std::size_t index = static_cast<std::size_t>(state.strip_points[found.index]) + found.segment + step;
        if (index >= item.x.size()) return Plot3DHit{};
        result.kind = Plot3DHitKind::line;
        result.item = item.id;
        result.index = index;
        result.data_point = std::array<double, 3>{item.x[index], item.y[index], item.z[index]};
        return result;
    }
    case stx_raster3d::PickKind::marker: {
        if (found.index >= state.data_markers.size()) return Plot3DHit{};
        const std::size_t slot = state.data_markers[found.index].item;
        if (slot >= state.items.size()) return Plot3DHit{};
        const SceneItem& item = state.items[slot];
        if (found.index >= state.marker_points.size()) return Plot3DHit{};
        const std::size_t index = state.marker_points[found.index];
        if (item.kind != ItemKind::scatter || index >= item.x.size()) return Plot3DHit{};
        result.kind = Plot3DHitKind::scatter;
        result.item = item.id;
        result.index = index;
        result.data_point = std::array<double, 3>{item.x[index], item.y[index], item.z[index]};
        return result;
    }
    case stx_raster3d::PickKind::none:
    default:
        return Plot3DHit{};
    }
}

std::string Plot3DScene::readout(const Plot3DHit& hit) const {
    if (hit.kind == Plot3DHitKind::none) return std::string{};
    std::array<char, 128> text{};
    const int written = std::snprintf(text.data(), text.size(), "x=%.6g, y=%.6g, z=%.6g", hit.data_point[0],
                                      hit.data_point[1], hit.data_point[2]);
    const std::size_t length = written > 0 ? std::min(static_cast<std::size_t>(written), text.size() - 1U) : 0U;
    const std::string_view coordinates(text.data(), length);
    const std::string_view label = hit.kind == Plot3DHitKind::pane ? std::string_view{} : item_label(hit.item);
    std::string result;
    if (!label.empty()) {
        result.append(label);
        result.append(": ");
    }
    result.append(coordinates);
    return result;
}

// -- diagnostics ----------------------------------------------------------------------

std::uint64_t Plot3DScene::render_count() const noexcept {
    return (*state_).renders;
}

std::uint64_t Plot3DScene::mesh_build_count() const noexcept {
    return (*state_).mesh_builds;
}

std::size_t Plot3DScene::triangles_drawn() const noexcept {
    return (*state_).renderer.statistics().triangles_drawn;
}

} // namespace gui_forms::plots
