#pragma once

// Plot3DScene: the plots package's retained 3D scene (D5; notes
// D5_PLOTTING_CONVENTIONS.md, owner decisions of 2026-11-13). It owns copies
// of its items (surfaces over a grid, lines and scatters), the camera and
// the box around the data, and draws them on the CPU through stx_raster3d,
// a deterministic visibility-buffer renderer: one depth per pixel for
// surfaces, panes, grid, lines and markers alike, so intersections are
// drawn correctly.
//
// Retained work, from the most to the least expensive:
// - the data mesh (each surface tessellated or triangulated, each line and
//   scatter placed in the box) is built when an item, the colours or the
//   box aspect change, and kept otherwise;
// - the back panes, grid, axis lines and ticks depend on the view; they are
//   appended after the data mesh (Mesh::mark and truncate) on each render;
// - a render rasterises the whole view; the caller renders only when the
//   view, the size or the data changed (Plot3DView keeps the picture).
//
// The box spans -a..a along each axis, a being the box aspect scaled so its
// largest side is 1 (4:4:3 gives 1, 1 and 0.75); the data limits map onto
// it. The camera is matplotlib's: elevation 30, azimuth -60 and roll 0
// degrees by default, perspective with focal length 1 (a 90 degree field of
// view; owner decision, 2026-11-13), turning about `target`, a point in box
// units. Each axis is drawn on the box edge matplotlib's rule chooses (on
// the panes, towards the viewer) with its ticks; tick and axis labels are
// returned as overlay entries (labels()) for the caller to draw as themed
// 2D text, screen-facing by default (owner decision, 2026-11-13).
//
// Failures: add_* validate before they change anything and throw
// std::invalid_argument (std::bad_alloc when memory runs out); render
// returns a check and leaves the previous picture and labels when it fails
// (picking a surface may then find nothing until a render succeeds). A
// moved-from scene can only be destroyed or assigned. One thread: the
// scene's owner.

#include "gui_forms/export/application_export.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/plots/colormap/colormap.hpp"
#include "gui_forms/plots/plot_types/plot_types.hpp"
#include "gui_forms/plots/tick_generator/tick_generator.hpp"
#include "gui_forms/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stxlib/optional.hpp>
#include <span>
#include <string>
#include <string_view>

namespace gui_forms::plots {

namespace detail {
struct Plot3DSceneState;
} // namespace detail

enum class Plot3DProjection : std::uint8_t { perspective, orthographic };

// How a surface is made from its grid:
// - conv: the CONV surface through the nodes (stx_conv), tessellated
//   adaptively, so a coarse grid draws smooth and picking lands on the
//   interpolant exactly;
// - linear: the grid's cells as they stand, two triangles each, smooth
//   shaded;
// - flat: as linear, each triangle flat shaded (matplotlib's look).
enum class Plot3DInterpolation : std::uint8_t { conv, linear, flat };

// The light on a surface: none (its colours as they are), fixed (from the
// upper left of the box, as matplotlib's shade) or headlight (from the eye).
enum class Plot3DLighting : std::uint8_t { none, fixed, headlight };

enum class Plot3DMarker : std::uint8_t { circle, square, diamond, triangle_up, plus, cross };

enum class Plot3DAxis : std::uint8_t { x, y, z };

// What a hit found: a data item, or a back pane (the point on it, as
// matplotlib's coordinate readout), or nothing.
enum class Plot3DHitKind : std::uint8_t { none, surface, line, scatter, pane };

enum class Plot3DLabelRole : std::uint8_t { tick, title };

// How axis titles lie: screen-facing (the default), or along the axis: a
// title's baseline follows its axis edge as projected on the screen, turned
// by the least angle that keeps it reading left to right (within -90 to 90
// degrees, so it never reads upside down). Tick labels stay screen-facing.
enum class Plot3DLabelAlignment : std::uint8_t { screen, along_axis };

// A render's outcome.
enum class Plot3DCheck : std::uint8_t {
    ok,
    invalid_frame,  // a size, scale, supersample or destination that cannot be used
    out_of_memory,  // the renderer's or the mesh's storage was refused
    build_failed,   // an item could not be meshed (refused by the renderer)
};

// An item's identity, stable for the scene's lifetime (never reused).
struct GUI_FORMS_API Plot3DItemId final {
    std::uint32_t value{};
    friend bool operator==(Plot3DItemId left, Plot3DItemId right) noexcept { return left.value == right.value; }
};

struct GUI_FORMS_API Plot3DCamera final {
    double elevation{30.0};
    double azimuth{-60.0};
    double roll{0.0};
    // The picture's magnification, 0.1 to 20.
    double zoom{1.0};
    // The point the camera turns about, in box units.
    std::array<double, 3> target{};
    Plot3DProjection projection{Plot3DProjection::perspective};
    // Perspective's focal length (1: a 90 degree field of view), 0.2 to 20.
    double focal_length{1.0};
    friend bool operator==(const Plot3DCamera& left, const Plot3DCamera& right) noexcept {
        return left.elevation == right.elevation && left.azimuth == right.azimuth && left.roll == right.roll &&
               left.zoom == right.zoom && left.target == right.target && left.projection == right.projection &&
               left.focal_length == right.focal_length;
    }
};

struct GUI_FORMS_API Plot3DOptions final {
    // The box's side ratios (positive); matplotlib's default 4:4:3.
    std::array<double, 3> box_aspect{4.0, 4.0, 3.0};
    bool show_panes{true};
    bool show_grid{true};
    bool show_axes{true};
    Plot3DLabelAlignment label_alignment{Plot3DLabelAlignment::screen};
    std::array<std::string, 3> axis_titles{};
    friend bool operator==(const Plot3DOptions& left, const Plot3DOptions& right) noexcept {
        return left.box_aspect == right.box_aspect && left.show_panes == right.show_panes &&
               left.show_grid == right.show_grid && left.show_axes == right.show_axes &&
               left.label_alignment == right.label_alignment && left.axis_titles == right.axis_titles;
    }
};

// The colours the caller resolves from its theme: the panes (translucent,
// drawn over the caller's own background), the grid, the axis lines and
// ticks, and the series colours an item without its own colour takes in
// turn.
struct GUI_FORMS_API Plot3DColors final {
    static constexpr std::size_t series_count = 8U;
    std::array<Color, 3> panes{Color{128, 128, 128, 20}, Color{128, 128, 128, 28}, Color{128, 128, 128, 36}};
    Color grid{128, 128, 128, 110};
    Color axis{64, 64, 64, 255};
    std::array<Color, series_count> series{Color{31, 119, 180, 255},  Color{255, 127, 14, 255},
                                           Color{44, 160, 44, 255},   Color{214, 39, 40, 255},
                                           Color{148, 103, 189, 255}, Color{140, 86, 75, 255},
                                           Color{227, 119, 194, 255}, Color{127, 127, 127, 255}};
    friend bool operator==(const Plot3DColors& left, const Plot3DColors& right) noexcept {
        return left.panes == right.panes && left.grid == right.grid && left.axis == right.axis &&
               left.series == right.series;
    }
};

// A surface over a grid (matplotlib's plot_surface). Borrowed for the call:
// the scene copies what it keeps.
struct GUI_FORMS_API Plot3DSurfaceSpec final {
    // z: rows * columns heights, row-major ([row][column]); NaN leaves a
    // hole. x: `columns` values (one per column) or rows * columns; y:
    // `rows` values (one per row) or rows * columns. x and y are finite.
    std::span<const double> x{};
    std::span<const double> y{};
    std::span<const double> z{};
    std::size_t rows{};
    std::size_t columns{};
    // Downsampling (matplotlib's rstride and cstride): every row_stride-th
    // row and column_stride-th column, the last always kept; at least 1.
    std::size_t row_stride{1U};
    std::size_t column_stride{1U};
    Plot3DInterpolation interpolation{Plot3DInterpolation::conv};
    BuiltInColormap colormap{BuiltInColormap::viridis};
    // One colour instead of the colour map over z, when set.
    stxlib::Optional<Color> color{};
    Plot3DLighting lighting{Plot3DLighting::fixed};
    std::string_view label{};
};

// A polyline through points (matplotlib's plot). NaN in any coordinate
// breaks the line. Equal lengths, at least one point.
struct GUI_FORMS_API Plot3DLineSpec final {
    std::span<const double> x{};
    std::span<const double> y{};
    std::span<const double> z{};
    stxlib::Optional<Color> color{};
    // In logical units, above 0 and at most 32.
    double width{1.5};
    std::string_view label{};
};

// Markers at points (matplotlib's scatter). A point with NaN is left out.
struct GUI_FORMS_API Plot3DScatterSpec final {
    std::span<const double> x{};
    std::span<const double> y{};
    std::span<const double> z{};
    stxlib::Optional<Color> color{};
    // The marker's size in logical units, above 0 and at most 64.
    double size{6.0};
    Plot3DMarker marker{Plot3DMarker::circle};
    std::string_view label{};
};

// What lies under a point of the last picture, exact per pixel.
// - index: a line's or scatter's point (the nearer end of the hit
//   segment), a surface's nearest grid node (row * columns + column);
// - data_point: the point in data coordinates (on a surface, the
//   interpolant's point; on a pane, the point on the pane);
// - surface_parameter: a surface's fractional row and column;
// - depth: the distance in front of the eye along the view, in box units.
struct GUI_FORMS_API Plot3DHit final {
    Plot3DHitKind kind{Plot3DHitKind::none};
    Plot3DItemId item{};
    std::size_t index{};
    std::array<double, 3> data_point{};
    std::array<double, 2> surface_parameter{};
    double depth{};
};

// A label to draw over the picture, centred on `anchor` (logical units from
// the picture's top left). A tick label carries its text; a title's text is
// options().axis_titles[axis]. `occluded` marks a label whose place on the
// box is hidden behind data: the caller dims it rather than removing it.
// `angle`: how far the label's baseline is turned about `anchor`, in degrees
// anticlockwise as seen (Painter::draw_rotated_text_utf8), in (-90, 90]; 0
// for a screen-facing label.
struct GUI_FORMS_API Plot3DLabel final {
    Plot3DLabelRole role{Plot3DLabelRole::tick};
    Plot3DAxis axis{Plot3DAxis::x};
    TickLabel text{};
    Point anchor{};
    bool occluded{};
    double angle{};
};

// One render's request: the picture's logical size, its device scale (0.5
// to 8), the supersampling per side (1 to 4) and how wide the caller's tick
// labels are (logical units; null estimates them).
struct GUI_FORMS_API Plot3DFrame final {
    double width{};
    double height{};
    DeviceScale scale{};
    std::size_t supersample{1U};
    const TickLabelMetrics* metrics{};
};

class GUI_FORMS_API Plot3DScene final {
public:
    // The most device pixels per side a render draws.
    static constexpr std::uint32_t maximum_side = 8192U;

    Plot3DScene();
    ~Plot3DScene();
    Plot3DScene(Plot3DScene&& other) noexcept;
    Plot3DScene& operator=(Plot3DScene&& other) noexcept;
    Plot3DScene(const Plot3DScene&) = delete;
    Plot3DScene& operator=(const Plot3DScene&) = delete;

    // -- items -----------------------------------------------------------------
    // Each copies its spec; std::invalid_argument for a shape that cannot
    // be drawn (the scene unchanged).
    Plot3DItemId add_surface(const Plot3DSurfaceSpec& spec);
    Plot3DItemId add_line(const Plot3DLineSpec& spec);
    Plot3DItemId add_scatter(const Plot3DScatterSpec& spec);
    // False when no item has the identity.
    bool remove(Plot3DItemId item);
    void clear();
    [[nodiscard]] std::size_t item_count() const noexcept;
    // An item's label; empty for an unknown identity.
    [[nodiscard]] std::string_view item_label(Plot3DItemId item) const& noexcept;
    std::string_view item_label(Plot3DItemId item) const&& = delete;
    // The data limits along an axis (all items' finite values, widened when
    // they are one value).
    [[nodiscard]] PlotRange limits(Plot3DAxis axis) const;

    // -- view ------------------------------------------------------------------
    [[nodiscard]] const Plot3DCamera& camera() const noexcept;
    // Takes the camera with its zoom, focal length and target clamped;
    // false (unchanged) for a non-finite field. True when it changed.
    bool set_camera(const Plot3DCamera& camera);
    // Back to the default angles, zoom and target; the projection stays.
    bool reset_camera();
    [[nodiscard]] const Plot3DOptions& options() const noexcept;
    // std::invalid_argument for a box aspect that is not positive and finite.
    void set_options(const Plot3DOptions& options);
    [[nodiscard]] const Plot3DColors& colors() const noexcept;
    void set_colors(const Plot3DColors& colors);

    // -- gestures (logical units of the last render's picture) -----------------
    // Turntable rotation by a pointer movement (matplotlib's azel style):
    // across turns the azimuth, down and up the elevation, half a turn per
    // picture width or height.
    bool rotate_by(double dx, double dy);
    // Moves the target so the picture follows the pointer.
    bool pan_by(double dx, double dy);
    // Multiplies the zoom by `factor`, keeping the point under `about`
    // (logical, from the picture's top left) in place when given.
    bool zoom_by(double factor, stxlib::Optional<Point> about);
    // The standard form, kept for one stage (D15 Q3).
    [[deprecated("pass stxlib::Optional<Point>")]]
    bool zoom_by(double factor, std::optional<Point> about) {
        stxlib::Optional<Point> owned{};
        if (about.has_value()) owned.set(*about);
        const bool zoomed = zoom_by(factor, owned);
        return zoomed;
    }
    // A plain point is refused rather than routed to the deprecated form:
    // pass stxlib::Optional<Point>::of(about).
    bool zoom_by(double factor, Point about) = delete;

    // -- picture ---------------------------------------------------------------
    // The device size of a frame's picture (its logical size times its
    // scale, rounded); zero for an unusable frame.
    [[nodiscard]] static DeviceExtent device_extent(const Plot3DFrame& frame) noexcept;
    // Draws the view into `destination` (premultiplied BGRA8, transparent
    // where nothing is drawn, of device_extent(frame)) and lays out the
    // labels. Builds the data mesh first when it is stale.
    Plot3DCheck render(const Plot3DFrame& frame, const PartRasterView& destination);
    // The labels of the last render, valid until the next render.
    [[nodiscard]] std::span<const Plot3DLabel> labels() const& noexcept;
    std::span<const Plot3DLabel> labels() const&& = delete;
    // What lies under `point` (logical, from the picture's top left) in the
    // last picture; none before the first render, and none after an add,
    // remove or clear until the next render (the picture's items are gone).
    [[nodiscard]] Plot3DHit hit(Point point) const;
    // The status-line readout of a hit (matplotlib's coordinate readout):
    // "x=…, y=…, z=…", after the item's label when it has one; empty for
    // none.
    [[nodiscard]] std::string readout(const Plot3DHit& hit) const;

    // -- diagnostics ------------------------------------------------------------
    // Renders done and data meshes built since the scene was made.
    [[nodiscard]] std::uint64_t render_count() const noexcept;
    [[nodiscard]] std::uint64_t mesh_build_count() const noexcept;
    // The last render's triangles drawn (data and panes).
    [[nodiscard]] std::size_t triangles_drawn() const noexcept;

private:
    std::unique_ptr<detail::Plot3DSceneState> state_;
};

} // namespace gui_forms::plots
