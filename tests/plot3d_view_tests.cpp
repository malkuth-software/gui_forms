// Plot3DScene and Plot3DView (D5; notes D5_PLOTTING_CONVENTIONS.md, owner
// decisions of 2026-11-13): the scene's refusals, limits and identities; a
// render's picture, labels and determinism; the data mesh kept across view
// changes; exact picking of a CONV surface, a gridded surface, a line and a
// scatter, and a hole that shows the pane behind it; the readout; and the
// control's retained picture (rendered again only when the view, the size,
// the items or a property change), its turntable drag, wheel zoom, double
// click reset, keyboard, hover and pick events, and its declared
// interactions. Every expectation is stated here; no data is loaded.

#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/plot_controls.hpp"
#include "gui_forms/plots.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <stxlib/optional.hpp>

namespace {
using namespace gui_forms;

// D15 Q3: Plot3DScene::zoom_by keeps its deprecated std::optional point for
// one stage; a plain point or {} must not reach it silently. Each plain call
// below is refused (the deleted overload or an ambiguity), and the stxlib
// form is accepted.
template <typename Scene>
concept takes_plain_zoom_point = requires(Scene& scene) { scene.zoom_by(2.0, Point{1.0, 1.0}); };
template <typename Scene>
concept takes_empty_zoom_point = requires(Scene& scene) { scene.zoom_by(2.0, {}); };
template <typename Scene>
concept takes_optional_zoom_point = requires(Scene& scene) { scene.zoom_by(2.0, stxlib::Optional<Point>::none()); };
static_assert(!takes_plain_zoom_point<plots::Plot3DScene> && !takes_empty_zoom_point<plots::Plot3DScene>);
static_assert(takes_optional_zoom_point<plots::Plot3DScene>);

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_plot3d_view_tests: " << message << '\n';
    std::exit(1);
}

bool within(double value, double expected, double tolerance) {
    return std::fabs(value - expected) <= tolerance;
}

constexpr std::size_t grid_side = 9U;

// A grid of `grid_side` nodes over [-1, 1] each way.
struct Grid final {
    std::array<double, grid_side> along{};
    std::array<double, grid_side * grid_side> heights{};
};

// A flat grid at `level`.
Grid flat_grid(double level) {
    Grid grid{};
    for (std::size_t index = 0U; index < grid_side; ++index) {
        grid.along[index] = -1.0 + 2.0 * static_cast<double>(index) / static_cast<double>(grid_side - 1U);
    }
    for (std::size_t node = 0U; node < grid.heights.size(); ++node) grid.heights[node] = level;
    return grid;
}

plots::Plot3DSurfaceSpec surface_of(const Grid& grid, plots::Plot3DInterpolation interpolation) {
    plots::Plot3DSurfaceSpec spec{};
    spec.x = grid.along;
    spec.y = grid.along;
    spec.z = grid.heights;
    spec.rows = grid_side;
    spec.columns = grid_side;
    spec.interpolation = interpolation;
    spec.label = "Plane";
    return spec;
}

// A frame and the buffer it renders into.
struct Picture final {
    plots::Plot3DFrame frame{};
    std::vector<std::uint8_t> pixels{};
    PartRasterView view{};
};

Picture make_picture(double width, double height) {
    Picture picture{};
    picture.frame.width = width;
    picture.frame.height = height;
    picture.frame.scale = DeviceScale{1.0};
    picture.frame.supersample = 2U;
    const DeviceExtent extent = plots::Plot3DScene::device_extent(picture.frame);
    picture.pixels.assign(static_cast<std::size_t>(extent.width) * extent.height * 4U, std::uint8_t{0});
    picture.view.pixels = picture.pixels;
    picture.view.extent = extent;
    picture.view.row_bytes = static_cast<std::size_t>(extent.width) * 4U;
    return picture;
}

std::uint8_t alpha_at(const Picture& picture, std::size_t x, std::size_t y) {
    return picture.pixels[y * picture.view.row_bytes + x * 4U + 3U];
}

// -- scene -----------------------------------------------------------------------------

void test_scene_refusals_and_limits() {
    plots::Plot3DScene scene;
    const Grid grid = flat_grid(0.5);
    plots::Plot3DSurfaceSpec narrow = surface_of(grid, plots::Plot3DInterpolation::conv);
    narrow.rows = 1U;
    bool refused = false;
    try {
        static_cast<void>(scene.add_surface(narrow));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && scene.item_count() == 0U, "a one-row surface is refused and nothing is added");
    plots::Plot3DSurfaceSpec strided = surface_of(grid, plots::Plot3DInterpolation::conv);
    strided.row_stride = 0U;
    refused = false;
    try {
        static_cast<void>(scene.add_surface(strided));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && scene.item_count() == 0U, "a zero stride is refused");
    const std::array<double, 2> two{0.0, 1.0};
    const std::array<double, 3> three{0.0, 1.0, 2.0};
    plots::Plot3DLineSpec uneven{};
    uneven.x = two;
    uneven.y = three;
    uneven.z = two;
    refused = false;
    try {
        static_cast<void>(scene.add_line(uneven));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && scene.item_count() == 0U, "a line of uneven lengths is refused");
    const std::array<double, 2> infinite{0.0, std::numeric_limits<double>::infinity()};
    plots::Plot3DScatterSpec unbounded{};
    unbounded.x = two;
    unbounded.y = two;
    unbounded.z = infinite;
    refused = false;
    try {
        static_cast<void>(scene.add_scatter(unbounded));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && scene.item_count() == 0U, "an infinite coordinate is refused");

    const plots::Plot3DItemId first = scene.add_surface(surface_of(grid, plots::Plot3DInterpolation::conv));
    const plots::Plot3DItemId second = scene.add_surface(surface_of(grid, plots::Plot3DInterpolation::linear));
    require(scene.item_count() == 2U && !(first == second), "items get distinct identities");
    require(scene.item_label(first) == "Plane", "an item keeps its label");
    const plots::PlotRange x = scene.limits(plots::Plot3DAxis::x);
    const plots::PlotRange z = scene.limits(plots::Plot3DAxis::z);
    require(x.minimum == -1.0 && x.maximum == 1.0, "the x limits are the data's");
    require(within(z.minimum, 0.45, 1e-12) && within(z.maximum, 0.55, 1e-12), "one z value is widened by a tenth");
    require(scene.remove(first) && !scene.remove(first) && scene.item_count() == 1U, "remove takes an item once");
    const plots::Plot3DItemId third = scene.add_line(plots::Plot3DLineSpec{two, two, two, {}, 1.5, ""});
    require(third.value > second.value, "identities are not reused");

    plots::Plot3DOptions bad{};
    bad.box_aspect = std::array<double, 3>{1.0, 0.0, 1.0};
    refused = false;
    try {
        scene.set_options(bad);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && scene.options().box_aspect[1] == 4.0, "a zero box side is refused and the aspect kept");
}

void test_scene_render_and_retention() {
    plots::Plot3DScene scene;
    const Grid grid = flat_grid(0.5);
    static_cast<void>(scene.add_surface(surface_of(grid, plots::Plot3DInterpolation::conv)));
    plots::Plot3DOptions options = scene.options();
    options.axis_titles = std::array<std::string, 3>{"x", "y", "z"};
    scene.set_options(options);

    Picture refused = make_picture(64.0, 48.0);
    refused.view.extent.width = refused.view.extent.width - 1U;
    require(scene.render(refused.frame, refused.view) == plots::Plot3DCheck::invalid_frame,
            "a destination of the wrong size is refused");
    plots::Plot3DFrame zero{};
    require(plots::Plot3DScene::device_extent(zero).width == 0U, "an empty frame has no device extent");

    Picture picture = make_picture(400.0, 300.0);
    require(scene.render(picture.frame, picture.view) == plots::Plot3DCheck::ok, "the scene renders");
    require(scene.render_count() == 1U && scene.mesh_build_count() == 1U, "one render, one data build");
    require(alpha_at(picture, 200U, 150U) == 255U, "the surface covers the picture's centre");
    require(alpha_at(picture, 0U, 0U) == 0U, "the corner is left transparent");
    require(scene.triangles_drawn() > 0U, "triangles are drawn");
    std::array<std::size_t, 3> ticks{};
    std::array<std::size_t, 3> titles{};
    for (const plots::Plot3DLabel& label : scene.labels()) {
        const std::size_t axis = static_cast<std::size_t>(label.axis);
        if (label.role == plots::Plot3DLabelRole::tick) ticks[axis] = ticks[axis] + 1U;
        if (label.role == plots::Plot3DLabelRole::title) titles[axis] = titles[axis] + 1U;
    }
    require(ticks[0] >= 2U && ticks[1] >= 2U && ticks[2] >= 2U, "each axis has tick labels");
    require(titles[0] == 1U && titles[1] == 1U && titles[2] == 1U, "each titled axis has its title");

    // The same view draws the same bytes.
    Picture again = make_picture(400.0, 300.0);
    require(scene.render(again.frame, again.view) == plots::Plot3DCheck::ok, "the scene renders again");
    require(again.pixels == picture.pixels, "a render is deterministic");

    // A camera change keeps the data mesh.
    plots::Plot3DCamera turned = scene.camera();
    turned.azimuth = 30.0;
    require(scene.set_camera(turned), "the camera turns");
    require(!scene.set_camera(turned), "the same camera is no change");
    require(scene.render(again.frame, again.view) == plots::Plot3DCheck::ok, "the turned scene renders");
    require(scene.mesh_build_count() == 1U, "a new view rebuilds no data mesh");
    require(again.pixels != picture.pixels, "a new view draws a new picture");
    static_cast<void>(scene.add_line(plots::Plot3DLineSpec{grid.along, grid.along, grid.along, {}, 2.0, "Line"}));
    require(scene.render(again.frame, again.view) == plots::Plot3DCheck::ok, "the scene with a line renders");
    require(scene.mesh_build_count() == 2U, "a new item rebuilds the data mesh");

    // Clamping and wrapping.
    plots::Plot3DCamera wild{};
    wild.azimuth = 190.0;
    wild.zoom = 1000.0;
    require(scene.set_camera(wild), "a wild camera is taken");
    require(within(scene.camera().azimuth, -170.0, 1e-9), "the azimuth wraps into [-180, 180)");
    require(scene.camera().zoom == 20.0, "the zoom is clamped");
    wild.elevation = std::numeric_limits<double>::quiet_NaN();
    require(!scene.set_camera(wild), "a NaN angle is refused");
    require(scene.reset_camera() && scene.camera().azimuth == -60.0 && scene.camera().elevation == 30.0,
            "reset returns to matplotlib's angles");
}

void test_scene_picking() {
    const Grid grid = flat_grid(0.5);
    for (const plots::Plot3DInterpolation interpolation :
         std::array<plots::Plot3DInterpolation, 2>{plots::Plot3DInterpolation::conv, plots::Plot3DInterpolation::linear}) {
        plots::Plot3DScene scene;
        const plots::Plot3DItemId plane = scene.add_surface(surface_of(grid, interpolation));
        Picture picture = make_picture(200.0, 160.0);
        require(scene.render(picture.frame, picture.view) == plots::Plot3DCheck::ok, "the plane renders");
        // The picture's centre looks at the box's centre: the plane's
        // middle, data (0, 0, 0.5). A CONV hit is refined to the pointer's
        // exact point; a gridded one is the sample's, within a pixel (about
        // 0.035 across and 0.07 deep in data units here).
        const plots::Plot3DHit hit = scene.hit(Point{100.0, 80.0});
        require(hit.kind == plots::Plot3DHitKind::surface && hit.item == plane, "the centre hits the plane");
        require(within(hit.data_point[0], 0.0, 0.08) && within(hit.data_point[1], 0.0, 0.08), "at the plane's middle");
        require(within(hit.data_point[2], 0.5, 1e-6), "at the plane's height");
        require(within(hit.surface_parameter[0], 4.0, 0.35) && within(hit.surface_parameter[1], 4.0, 0.35),
                "the surface parameter is the middle node");
        require(hit.index == 4U * grid_side + 4U, "the nearest node is the middle one");
        require(hit.depth > 0.0, "the hit is in front of the eye");
        require(scene.readout(hit).rfind("Plane: x=", 0) == 0U, "the readout names the item, then the point");
        require(scene.hit(Point{-5.0, 10.0}).kind == plots::Plot3DHitKind::none, "outside the picture is nothing");
    }

    // A scatter's middle point and a line's middle point at the centre.
    plots::Plot3DScene scatter_scene;
    const std::array<double, 3> xs{-1.0, 1.0, 0.0};
    const std::array<double, 3> ys{-1.0, 1.0, 0.0};
    const std::array<double, 3> zs{0.0, 1.0, 0.5};
    plots::Plot3DScatterSpec scatter{};
    scatter.x = xs;
    scatter.y = ys;
    scatter.z = zs;
    scatter.size = 10.0;
    const plots::Plot3DItemId dots = scatter_scene.add_scatter(scatter);
    Picture dots_picture = make_picture(200.0, 160.0);
    require(scatter_scene.render(dots_picture.frame, dots_picture.view) == plots::Plot3DCheck::ok, "the scatter renders");
    const plots::Plot3DHit dot = scatter_scene.hit(Point{100.0, 80.0});
    require(dot.kind == plots::Plot3DHitKind::scatter && dot.item == dots && dot.index == 2U,
            "the centre hits the scatter's third point");
    require(dot.data_point[2] == 0.5, "a scatter hit reports its point exactly");
    // A-gf-core-09-F1: replacing the data leaves the picture's slot numbers
    // naming the old items until the next render. A pick in between finds
    // nothing, rather than the new scatter's point 2, which it does not
    // have.
    require(scatter_scene.remove(dots), "the scatter is removed");
    const std::array<double, 1> one_x{0.0};
    const std::array<double, 1> one_y{0.0};
    const std::array<double, 1> one_z{0.5};
    plots::Plot3DScatterSpec single{};
    single.x = one_x;
    single.y = one_y;
    single.z = one_z;
    single.size = 10.0;
    const plots::Plot3DItemId lone = scatter_scene.add_scatter(single);
    require(scatter_scene.hit(Point{100.0, 80.0}).kind == plots::Plot3DHitKind::none,
            "a pick between a data change and the next render finds nothing");
    require(scatter_scene.render(dots_picture.frame, dots_picture.view) == plots::Plot3DCheck::ok, "the new data renders");
    const plots::Plot3DHit lone_hit = scatter_scene.hit(Point{100.0, 80.0});
    require(lone_hit.kind == plots::Plot3DHitKind::scatter && lone_hit.item == lone && lone_hit.index == 0U,
            "after the render the pick names the new point");

    plots::Plot3DScene line_scene;
    const std::array<double, 3> line_x{-1.0, 0.0, 1.0};
    const std::array<double, 3> line_y{0.0, 0.0, 0.0};
    const std::array<double, 3> line_z{0.5, 0.5, 0.5};
    plots::Plot3DLineSpec line{};
    line.x = line_x;
    line.y = line_y;
    line.z = line_z;
    line.width = 4.0;
    const plots::Plot3DItemId path = line_scene.add_line(line);
    Picture line_picture = make_picture(200.0, 160.0);
    require(line_scene.render(line_picture.frame, line_picture.view) == plots::Plot3DCheck::ok, "the line renders");
    const plots::Plot3DHit on_line = line_scene.hit(Point{100.0, 80.0});
    require(on_line.kind == plots::Plot3DHitKind::line && on_line.item == path && on_line.index == 1U,
            "the centre hits the line's middle point");

    // A hole: the middle node is NaN, so the ray through it reaches the pane
    // below.
    Grid holed = flat_grid(0.5);
    holed.heights[4U * grid_side + 4U] = std::numeric_limits<double>::quiet_NaN();
    plots::Plot3DScene hole_scene;
    static_cast<void>(hole_scene.add_surface(surface_of(holed, plots::Plot3DInterpolation::conv)));
    Picture hole_picture = make_picture(200.0, 160.0);
    require(hole_scene.render(hole_picture.frame, hole_picture.view) == plots::Plot3DCheck::ok, "a holed surface renders");
    const plots::Plot3DHit through = hole_scene.hit(Point{100.0, 80.0});
    require(through.kind == plots::Plot3DHitKind::pane, "the hole shows the pane behind it");
    require(hole_scene.readout(through).rfind("x=", 0) == 0U, "a pane's readout is the bare point");
}

// -- control ---------------------------------------------------------------------------

// Keeps the last raster drawn and counts the draws.
class RecordingPainter final : public Painter {
public:
    void save() override { saves = saves + 1; }
    void restore() override { restores = restores + 1; }
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override {}
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point origin, double) override {
        rasters = rasters + 1;
        last = raster;
        last_origin = origin;
    }
    void draw_rotated_text_utf8(Point, std::string_view, FontSpec, Color, double degrees) override {
        rotated.push_back(degrees);
    }

    std::vector<double> rotated{};
    int saves{};
    int restores{};
    int rasters{};
    std::shared_ptr<const PartRaster> last{};
    Point last_origin{};
};

struct Recorder final {
    int hovers{};
    int picks{};
    int cameras{};
    plots::Plot3DHit last_hover{};
    plots::Plot3DHit last_pick{};
    plots::Plot3DCamera last_camera{};

    void on_hover(const plots::Plot3DHit& hit) {
        hovers = hovers + 1;
        last_hover = hit;
    }
    void on_pick(const plots::Plot3DHit& hit) {
        picks = picks + 1;
        last_pick = hit;
    }
    void on_camera(const plots::Plot3DCamera& camera) {
        cameras = cameras + 1;
        last_camera = camera;
    }
};

void subscribe(Plot3DView& plot, Recorder& recorder, std::vector<SubscriptionToken>& tokens) {
    tokens.push_back(plot.hovered().subscribe(
        Delegate<const plots::Plot3DHit&>::bind<Recorder, &Recorder::on_hover>(recorder)));
    tokens.push_back(plot.picked().subscribe(
        Delegate<const plots::Plot3DHit&>::bind<Recorder, &Recorder::on_pick>(recorder)));
    tokens.push_back(plot.camera_changed().subscribe(
        Delegate<const plots::Plot3DCamera&>::bind<Recorder, &Recorder::on_camera>(recorder)));
}

std::shared_ptr<Plot3DView> make_plane_view(const char* id, const Grid& grid) {
    const std::shared_ptr<Plot3DView> plot = make_control<Plot3DView>(StableId(std::string_view(id)));
    static_cast<void>((*plot).add_surface(surface_of(grid, plots::Plot3DInterpolation::conv)));
    (*plot).set_axis_title(plots::Plot3DAxis::x, "x");
    (*plot).set_axis_title(plots::Plot3DAxis::y, "y");
    (*plot).set_axis_title(plots::Plot3DAxis::z, "z");
    return plot;
}

void paint(Plot3DView& plot, RecordingPainter& painter) {
    plot.on_paint(painter, Rect{0.0, 0.0, 400.0, 300.0});
}

void test_view_retains_picture() {
    const Grid grid = flat_grid(0.5);
    const std::shared_ptr<Plot3DView> plot = make_plane_view("plot3d.retain", grid);
    Window window(plot, {400.0, 300.0});
    window.perform_layout();
    RecordingPainter painter;
    paint(*plot, painter);
    require(painter.rasters == 1 && painter.last != nullptr, "the picture is drawn as one part raster");
    require(painter.saves == painter.restores, "saves and restores balance");
    const Rect bounds = (*plot).plot_bounds();
    require(painter.last_origin.x == bounds.x && painter.last_origin.y == bounds.y, "the raster sits on the plot");
    require((*plot).picture_renders() == 1U, "the first paint renders the picture");
    const DeviceExtent extent = (*painter.last).extent();
    require(extent.width == static_cast<std::uint32_t>(bounds.width) && extent.height == static_cast<std::uint32_t>(bounds.height),
            "the raster is the plot's size at scale 1");
    paint(*plot, painter);
    require((*plot).picture_renders() == 1U && painter.rasters == 2, "an unchanged repaint draws the kept picture");

    plots::Plot3DCamera turned = (*plot).camera();
    turned.azimuth = 10.0;
    require((*plot).set_camera(turned), "the camera turns");
    paint(*plot, painter);
    require((*plot).picture_renders() == 2U, "a camera change renders again");
    (*plot).show_panes = false;
    paint(*plot, painter);
    require((*plot).picture_renders() == 3U, "a property change renders again");
    (*plot).wheel_zoom = false;
    paint(*plot, painter);
    require((*plot).picture_renders() == 3U, "wheel_zoom changes nothing drawn");
    require((*plot).scene().mesh_build_count() == 1U, "none of these rebuilt the data mesh");

    // Screen-facing labels are not turned.
    for (const plots::Plot3DLabel& label : (*plot).scene().labels()) {
        require(label.angle == 0.0, "screen-facing labels are level");
    }
    require(painter.rotated.empty(), "no text is turned while labels face the screen");

    // Along the axis, each title follows its edge as projected: the z title
    // (a vertical edge, upright on screen in an orthographic view at 30
    // degrees elevation) lies near a quarter turn, the floor axes' titles at
    // their edges' slants, every angle reading left to right; tick labels
    // stay level. The titles reach the painter turned, through the theme's
    // text part. The default perspective (a 90 degree field of view) leans
    // a corner's vertical edge toward the vanishing point below the view by
    // more than the 15 degrees allowed here, so this part looks
    // orthographically.
    plots::Plot3DCamera flat = (*plot).camera();
    flat.projection = plots::Plot3DProjection::orthographic;
    require((*plot).set_camera(flat), "the camera looks orthographically");
    (*plot).labels_along_axis = true;
    painter.rotated.clear();
    paint(*plot, painter);
    double z_angle = 0.0;
    std::size_t slanted = 0U;
    for (const plots::Plot3DLabel& label : (*plot).scene().labels()) {
        require(label.angle > -90.0 && label.angle <= 90.0, "a turned label reads left to right");
        if (label.role == plots::Plot3DLabelRole::tick) {
            require(label.angle == 0.0, "tick labels stay level along the axis");
            continue;
        }
        if (label.axis == plots::Plot3DAxis::z) z_angle = label.angle;
        if (label.axis != plots::Plot3DAxis::z && std::fabs(label.angle) > 1.0 && std::fabs(label.angle) < 89.0) {
            slanted = slanted + 1U;
        }
    }
    require(std::fabs(std::fabs(z_angle) - 90.0) < 15.0, "along the axis, the z title is near a quarter turn");
    require(slanted == 2U, "the x and y titles follow their slanted edges");
    require(painter.rotated.size() == 3U, "the three titles are drawn turned");
    // Turning the box turns the floor titles with their edges.
    std::array<double, 3> before{};
    for (const plots::Plot3DLabel& label : (*plot).scene().labels()) {
        if (label.role == plots::Plot3DLabelRole::title) before[static_cast<std::size_t>(label.axis)] = label.angle;
    }
    plots::Plot3DCamera spun = (*plot).camera();
    spun.azimuth = spun.azimuth + 25.0;
    require((*plot).set_camera(spun), "the camera spins");
    paint(*plot, painter);
    bool followed = false;
    for (const plots::Plot3DLabel& label : (*plot).scene().labels()) {
        if (label.role != plots::Plot3DLabelRole::title || label.axis == plots::Plot3DAxis::z) continue;
        if (std::fabs(label.angle - before[static_cast<std::size_t>(label.axis)]) > 2.0) followed = true;
    }
    require(followed, "a turned box turns the floor titles with their edges");
}

void test_view_gestures_and_keys() {
    const Grid grid = flat_grid(0.5);
    const std::shared_ptr<Plot3DView> plot = make_plane_view("plot3d.gestures", grid);
    Window window(plot, {400.0, 300.0});
    window.perform_layout();
    RecordingPainter painter;
    paint(*plot, painter);
    Recorder recorder{};
    std::vector<SubscriptionToken> tokens;
    subscribe(*plot, recorder, tokens);
    const Rect bounds = (*plot).plot_bounds();
    const Point centre{bounds.x + bounds.width / 2.0, bounds.y + bounds.height / 2.0};

    // Hover over the plane's middle.
    PointerEvent move{};
    move.action = PointerAction::move;
    move.position = centre;
    static_cast<void>(window.dispatch_pointer(move));
    require(recorder.hovers == 1 && recorder.last_hover.kind == plots::Plot3DHitKind::surface,
            "hovering the plane raises hovered with a surface hit");
    require((*plot).readout().rfind("Plane: x=", 0) == 0U, "the readout shows the hovered point");
    static_cast<void>(window.dispatch_pointer(move));
    require(recorder.hovers == 1, "the same point raises nothing");

    // A click without a drag picks.
    PointerEvent click{};
    click.action = PointerAction::down;
    click.button = PointerButton::primary;
    click.position = centre;
    static_cast<void>(window.dispatch_pointer(click));
    click.action = PointerAction::up;
    static_cast<void>(window.dispatch_pointer(click));
    require(recorder.picks == 1 && recorder.last_pick.kind == plots::Plot3DHitKind::surface, "a click picks the plane");
    require(recorder.cameras == 0, "a click moves no camera");

    // A left drag across turns the azimuth: half a turn per plot width.
    PointerEvent down{};
    down.action = PointerAction::down;
    down.button = PointerButton::primary;
    down.position = centre;
    static_cast<void>(window.dispatch_pointer(down));
    PointerEvent drag = down;
    drag.action = PointerAction::move;
    drag.button = PointerButton::none;
    drag.position = Point{centre.x + bounds.width / 4.0, centre.y};
    static_cast<void>(window.dispatch_pointer(drag));
    require(recorder.cameras == 1 && within(recorder.last_camera.azimuth, -60.0 - 45.0, 1e-9),
            "a quarter width to the right turns the azimuth back 45 degrees");
    require((*plot).readout().rfind("azimuth=", 0) == 0U, "while turning the readout shows the angles");
    PointerEvent up = drag;
    up.action = PointerAction::up;
    up.button = PointerButton::primary;
    static_cast<void>(window.dispatch_pointer(up));
    require(recorder.picks == 1, "a drag picks nothing");

    // A-gf-controls-10-F8: a press whose capture is lost ends its drag, so later plain moves
    // zoom nothing. (A secondary press, so no double click resets the view.)
    PointerEvent zoom_down{};
    zoom_down.action = PointerAction::down;
    zoom_down.button = PointerButton::secondary;
    zoom_down.position = centre;
    static_cast<void>(window.dispatch_pointer(zoom_down));
    window.release_pointer();
    PointerEvent after_loss{};
    after_loss.action = PointerAction::move;
    after_loss.button = PointerButton::none;
    after_loss.position = Point{centre.x, centre.y - 40.0};
    static_cast<void>(window.dispatch_pointer(after_loss));
    require(recorder.cameras == 1, "a move after the capture is lost moves no camera");
    paint(*plot, painter);
    const std::uint64_t rendered = (*plot).picture_renders();

    // The wheel zooms about the pointer.
    PointerEvent wheel{};
    wheel.action = PointerAction::wheel;
    wheel.position = centre;
    wheel.wheel_delta = Point{0.0, 1.0};
    require(window.dispatch_pointer(wheel), "a wheel over the plot is handled");
    require(recorder.cameras == 2 && within(recorder.last_camera.zoom, 1.1, 1e-12), "a notch zooms by 1.1");
    paint(*plot, painter);
    require((*plot).picture_renders() == rendered + 1U, "the zoom renders again");

    // A double click resets the view.
    PointerEvent twice{};
    twice.action = PointerAction::down;
    twice.button = PointerButton::primary;
    twice.position = centre;
    twice.click_count = 2U;
    static_cast<void>(window.dispatch_pointer(twice));
    twice.action = PointerAction::up;
    static_cast<void>(window.dispatch_pointer(twice));
    require(recorder.cameras == 3 && (*plot).camera() == plots::Plot3DCamera{}, "a double click resets the camera");

    // Keys: arrows turn, + zooms, P switches the projection, Home resets.
    require(window.request_focus(plot), "the plot takes focus");
    KeyEvent left{};
    left.physical_key = PhysicalKey::left;
    require(window.dispatch_key(left), "an arrow is handled");
    require(within((*plot).camera().azimuth, -55.0, 1e-9), "left turns the azimuth 5 degrees as a drag left does");
    KeyEvent up_key{};
    up_key.physical_key = PhysicalKey::up;
    static_cast<void>(window.dispatch_key(up_key));
    require(within((*plot).camera().elevation, 25.0, 1e-9), "up lowers the elevation as a drag up does");
    KeyEvent plus{};
    plus.physical_key = PhysicalKey::equal;
    static_cast<void>(window.dispatch_key(plus));
    require(within((*plot).camera().zoom, 1.25, 1e-12), "+ zooms by 1.25");
    KeyEvent projection{};
    projection.physical_key = PhysicalKey::p;
    static_cast<void>(window.dispatch_key(projection));
    require((*plot).camera().projection == plots::Plot3DProjection::orthographic, "P switches to orthographic");
    const int before_home = recorder.cameras;
    KeyEvent home{};
    home.physical_key = PhysicalKey::home;
    static_cast<void>(window.dispatch_key(home));
    require(recorder.cameras == before_home + 1 && (*plot).camera().azimuth == -60.0 &&
                (*plot).camera().projection == plots::Plot3DProjection::orthographic,
            "Home resets the angles and keeps the projection");
    KeyEvent letter{};
    letter.physical_key = PhysicalKey::q;
    require(!window.dispatch_key(letter), "other keys are not handled");

    // wheel_zoom off: the wheel passes.
    (*plot).wheel_zoom = false;
    const int before_wheel = recorder.cameras;
    static_cast<void>(window.dispatch_pointer(wheel));
    require(recorder.cameras == before_wheel, "with wheel_zoom off the wheel moves no camera");
}

void test_view_semantics_and_interactions() {
    const Grid grid = flat_grid(0.5);
    const std::shared_ptr<Plot3DView> plot = make_plane_view("plot3d.semantics", grid);
    const SemanticDescriptor descriptor = (*plot).semantic_descriptor();
    require(descriptor.role == SemanticRole::figure, "the plot is a figure");
    require(descriptor.description.find("1 items") != std::string::npos, "the description counts the items");
    std::vector<InteractionDeclaration> interactions;
    (*plot).append_interactions(interactions);
    bool hovered = false;
    bool picked = false;
    bool camera = false;
    for (const InteractionDeclaration& declared : interactions) {
        if (declared.name == "hovered") hovered = true;
        if (declared.name == "picked") picked = true;
        if (declared.name == "camera_changed") camera = true;
    }
    require(hovered && picked && camera, "the three events are declared");
    require((*plot).anatomy().control_id == "plot3d_view", "the anatomy carries the registry id");
    require((*plot).axis_title(plots::Plot3DAxis::z) == "z", "an axis keeps its title");
    // A-gf-controls-10-F9: an axis outside the enumeration neither writes nor reads past the
    // three titles.
    bool axis_refused = false;
    try {
        (*plot).set_axis_title(static_cast<plots::Plot3DAxis>(3), "w");
    } catch (const std::invalid_argument&) {
        axis_refused = true;
    }
    require(axis_refused && (*plot).axis_title(plots::Plot3DAxis::z) == "z", "an unknown axis is refused");
    require((*plot).axis_title(static_cast<plots::Plot3DAxis>(200)).empty(), "an unknown axis reads as empty");
    bool refused = false;
    try {
        (*plot).set_box_aspect(std::array<double, 3>{1.0, -1.0, 1.0});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "a negative box side is refused");
}

} // namespace

int main() {
    test_scene_refusals_and_limits();
    test_scene_render_and_retention();
    test_scene_picking();
    test_view_retains_picture();
    test_view_gestures_and_keys();
    test_view_semantics_and_interactions();
    std::cout << "gui_forms_plot3d_view_tests passed\n";
    return 0;
}
