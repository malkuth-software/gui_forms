// The node canvas's look in pixels (cp31), drawn windowless through
// OffscreenRaster: a fixed sample graph (four nodes in every node state,
// ports, plain, dashed, selected and invalid links, lamps on, in error and
// off, a marquee and the link flow) painted directly through the canvas
// parts (paint_canvas_*), as NodeCanvas will paint it, then:
// - the paper canvas between grid lines is the theme's #fbfcfd, a minor
//   grid line differs from it, and forced high contrast draws no minor line;
// - dark is darker, and the blue variant is blue;
// - a texture (a baked felt pattern, no image) multiplied at 3 % changes
//   the canvas by at most 3 % of a channel, and somewhere, once its
//   pattern is made in idle frames; in dark it screens, within the same
//   bound;
// - two flow frames differ, and two still ones do not.
//
// Preview mode: `gui_forms_canvas_look_raster_tests --preview <directory>`
// writes the review set as PNGs into an existing <directory> and checks
// nothing: light and dark; each grid variant; forced high contrast; zoom
// out, normal and in; close-ups of the links (and on blue) and the lamps
// (and in dark); four flow frames; the texture off; felt and paper fibre
// (procedural), orange peel and cloth weave (finishes), each in light and
// dark; felt over paper fibre: 29 files. It never opens a window.

#include "../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/capture/offscreen_raster/offscreen_raster.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/link_flow_effects.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/effect/effect.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/resources/application_resources/application_resources.hpp"
#include "gui_forms/scheduler/types/scheduler_types.hpp"
#include "gui_forms/surface_root/surface_root.hpp"
#include "gui_forms/theme/canvas_look/canvas_look.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_canvas_look_raster_tests: " << message << '\n';
    std::exit(1);
}

std::unique_ptr<OffscreenRaster> made_raster() {
    OffscreenRasterOptions options{};
    options.font_directory = GUI_FORMS_TEST_FONT_DIR;
    OffscreenRasterCreateResult made = OffscreenRaster::create(options);
    require(made.succeeded() && made.error() == OffscreenRasterError::none, "the bundled faces make a raster");
    std::unique_ptr<OffscreenRaster> raster = made.take_raster();
    require(raster != nullptr, "the raster is taken");
    return raster;
}

// -- the sample graph ----------------------------------------------------------

constexpr Size graph_size{520.0, 330.0};
constexpr double header_height = 24.0;
constexpr double port_radius = 5.0;
constexpr double lamp_radius = 4.0;
// Points per flattened link: within EffectPoints' capacity.
constexpr std::size_t link_points = 16U;
static_assert(link_points <= maximum_effect_batch, "a link fits one flow polyline");

struct SampleNode final {
    Rect body{};
    CanvasNodeState state{CanvasNodeState::normal};
    CanvasLampState lamp{CanvasLampState::off};
    std::string_view title{};
};

struct SamplePort final {
    Point centre{};
    CanvasPortState state{CanvasPortState::normal};
};

// A link from `from` leaving to the right to `to`, entered from the left
// (`enters_from_right` false) or from the right.
struct SampleLink final {
    Point from{};
    Point to{};
    bool enters_from_right{};
    CanvasLinkState state{CanvasLinkState::normal};
    CanvasLinkStyle style{CanvasLinkStyle::plain};
};

// The fixed sample graph, in world units.
constexpr std::array<SampleNode, 4> sample_nodes{{
    {Rect{40.0, 40.0, 140.0, 90.0}, CanvasNodeState::normal, CanvasLampState::on, "Source"},
    {Rect{260.0, 30.0, 150.0, 100.0}, CanvasNodeState::selected, CanvasLampState::error, "Filter"},
    {Rect{250.0, 190.0, 140.0, 90.0}, CanvasNodeState::hot, CanvasLampState::off, "Mixer"},
    {Rect{40.0, 200.0, 130.0, 80.0}, CanvasNodeState::disabled, CanvasLampState::off, "Muted"},
}};
constexpr std::array<SamplePort, 6> sample_ports{{
    {Point{180.0, 85.0}, CanvasPortState::normal},
    {Point{260.0, 80.0}, CanvasPortState::hot},
    {Point{410.0, 80.0}, CanvasPortState::drop_accepted},
    {Point{250.0, 235.0}, CanvasPortState::normal},
    {Point{170.0, 240.0}, CanvasPortState::drop_rejected},
    {Point{390.0, 235.0}, CanvasPortState::normal},
}};
constexpr std::array<SampleLink, 4> sample_links{{
    {Point{180.0, 85.0}, Point{260.0, 80.0}, false, CanvasLinkState::normal, CanvasLinkStyle::plain},
    {Point{180.0, 85.0}, Point{250.0, 235.0}, false, CanvasLinkState::normal, CanvasLinkStyle::dashed},
    {Point{410.0, 80.0}, Point{390.0, 235.0}, true, CanvasLinkState::selected, CanvasLinkStyle::plain},
    {Point{170.0, 240.0}, Point{250.0, 235.0}, false, CanvasLinkState::invalid, CanvasLinkStyle::plain},
}};
constexpr Rect sample_marquee{200.0, 292.0, 130.0, 28.0};

Point to_view(Point world, CanvasView view) {
    const Point result{view.origin.x + world.x * view.zoom, view.origin.y + world.y * view.zoom};
    return result;
}

Rect to_view(Rect world, CanvasView view) {
    const Point corner = to_view(Point{world.x, world.y}, view);
    const Rect result{corner.x, corner.y, world.width * view.zoom, world.height * view.zoom};
    return result;
}

// `link` as a cubic Bézier through link_points points, in view units.
std::array<Point, link_points> flattened_link(const SampleLink& link, CanvasView view) {
    const double reach = std::max(40.0, std::abs(link.to.x - link.from.x) * 0.5);
    const Point first_control{link.from.x + reach, link.from.y};
    const double entry = link.enters_from_right ? reach : -reach;
    const Point second_control{link.to.x + entry, link.to.y};
    std::array<Point, link_points> points{};
    for (std::size_t index = 0U; index < link_points; ++index) {
        const double t = static_cast<double>(index) / static_cast<double>(link_points - 1U);
        const double u = 1.0 - t;
        const double a = u * u * u;
        const double b = 3.0 * u * u * t;
        const double c = 3.0 * u * t * t;
        const double d = t * t * t;
        const Point world{a * link.from.x + b * first_control.x + c * second_control.x + d * link.to.x,
                          a * link.from.y + b * first_control.y + c * second_control.y + d * link.to.y};
        points[index] = to_view(world, view);
    }
    return points;
}

// What the sample paints: where world (0, 0) lies and the zoom, whether
// the flow draws, and whether its frame is still.
struct SampleSettings final {
    CanvasView view{};
    bool flow{true};
    bool still{};
};

// The sample graph as a control: each paint resolves its look from its
// theme and visual context (a canvas would keep it until a restyle) and
// draws every part, then the flow over the links that flow.
class SampleGraph final : public Control {
public:
    explicit SampleGraph(StableId stable_id) : Control(std::move(stable_id)) {}

    SampleSettings settings{};
    EffectInstance flow{};

    void on_paint(Painter& painter, Rect) override {
        const CanvasLook look = resolve_canvas_look(effective_theme(), *this, visual_context());
        const CanvasView view = settings.view;
        const Rect area = client_rectangle();
        paint_canvas_background(painter, *this, look, area, view);
        for (const SampleLink& link : sample_links) {
            const std::array<Point, link_points> points = flattened_link(link, view);
            paint_canvas_link(painter, look, points, link.state, link.style);
        }
        if (settings.flow) paint_flow(painter, look, area);
        for (const SampleNode& node : sample_nodes) {
            const Rect body = to_view(node.body, view);
            if (node.state == CanvasNodeState::selected) paint_canvas_node_selection(painter, look, body);
            paint_canvas_node(painter, look, body, header_height * view.zoom, node.state);
            const std::size_t title_state = static_cast<std::size_t>(node.state);
            FontSpec font{};
            font.size = 12.0 * view.zoom;
            const Point baseline{body.x + 8.0 * view.zoom, body.y + 16.0 * view.zoom};
            if (view.zoom >= 0.5) painter.draw_text_utf8(baseline, node.title, font, look.titles[title_state]);
            const Point lamp{body.right() - 12.0 * view.zoom, body.y + 12.0 * view.zoom};
            paint_canvas_lamp(painter, look, lamp, lamp_radius * view.zoom, node.lamp);
        }
        for (const SamplePort& port : sample_ports) {
            paint_canvas_port(painter, look, to_view(port.centre, view), port_radius * view.zoom, port.state);
        }
        paint_canvas_marquee(painter, look, to_view(sample_marquee, view));
    }

private:
    // The flow over the plain links at rest, as NodeCanvas will host it.
    void paint_flow(Painter& painter, const CanvasLook& look, Rect area) {
        EffectView<NodeCanvas> view{};
        view.style = look.flow.style;
        view.speed = look.flow.speed * settings.view.zoom;
        view.spacing = look.flow.spacing;
        view.width = look.links[static_cast<std::size_t>(CanvasLinkState::normal)].width;
        for (const SampleLink& link : sample_links) {
            const bool flows = link.state == CanvasLinkState::normal && link.style == CanvasLinkStyle::plain;
            if (!flows) continue;
            const std::array<Point, link_points> points = flattened_link(link, settings.view);
            EffectPoints& target = view.links.items[view.links.count];
            for (std::size_t index = 0U; index < link_points; ++index) target.items[index] = points[index];
            target.count = static_cast<std::uint32_t>(link_points);
            view.links.count = view.links.count + 1U;
        }
        EffectColours colours{};
        colours.primary = link_flow_mark_colour(look);
        flow.set_colours(colours);
        flow.paint(painter, area, 1.0, settings.still, view);
    }
};

const FrameTime start{std::chrono::seconds(1000)};

const EffectEntry& link_flow() {
    const EffectEntry* const entry = built_in_effect_registry().find("link_flow");
    require(entry != nullptr, "link_flow is a built-in effect");
    return *entry;
}

// One window holding one sample graph filling it, its flow attached.
struct SampleWindow final {
    std::shared_ptr<Panel> root{};
    std::shared_ptr<SampleGraph> graph{};
    std::unique_ptr<Window> window{};
};

SampleWindow made_sample(std::shared_ptr<ApplicationResources> resources) {
    SampleWindow made{};
    made.root = make_control<Panel>(StableId("sample.root"));
    made.graph = make_control<SampleGraph>(StableId("sample.graph"));
    (*made.graph).set_requested_bounds(Rect{0.0, 0.0, graph_size.width, graph_size.height});
    (*made.graph).settings.view.origin = Point{0.5, 0.5};
    (*made.root).add_child(made.graph);
    if (resources == nullptr) made.window = std::make_unique<Window>(made.root, graph_size);
    if (resources != nullptr) made.window = std::make_unique<Window>(made.root, graph_size, std::move(resources));
    (*made.window).set_active(true);
    (*made.window).perform_layout();
    (*made.graph).flow.attach(link_flow(), 31U, start);
    return made;
}

std::string hex_of(std::string_view bytes) {
    return theme_language::package_sha256_hex(theme_language::package_sha256(bytes));
}

// The default theme with `texture` (a background-texture value) under its
// canvas, compiled from a sheet alone, no image in the package, into
// `resources` and selected there.
void select_texture_theme(ApplicationResources& resources, std::string_view texture) {
    // The theme declares both schemes, or a dark window would show
    // GUI.Forms' own theme instead (and no texture).
    const std::string sheet = ":root { color-scheme: light dark; }\n"
                              "node-canvas::part(region) { background-texture: " +
                              std::string(texture) + " }";
    ThemePackage package{};
    package.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"canvas-texture\", \"version\": \"1.0.0\", "
                       "\"engine\": \">=0.3\", \"sheet\": {\"path\": \"theme.theme.css\", \"sha256\": \"" +
                       hex_of(sheet) + "\"}}";
    package.files.push_back(ThemePackageFile{"theme.theme.css", sheet});
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    options.base = ThemeBase::gui_forms;
    const ThemeLibrary library{};
    const ThemeCompileResult compiled = resources.compile_theme_package(package, library, options);
    for (const ThemeCompileDiagnostic& diagnostic : compiled.diagnostics()) {
        std::cerr << "  texture theme: " << diagnostic.code << " " << diagnostic.name << " (" << diagnostic.detail
                  << ")\n";
    }
    require(compiled.succeeded(), "the texture theme compiles");
    resources.set_theme(compiled.theme());
}

// The felt texture the checks use: full strength at 3 %.
constexpr std::string_view felt_texture = "procedural(felt, 31, 64px, 1) multiply 3%";

struct CaptureRequest final {
    ColourScheme scheme{ColourScheme::light};
    bool high_contrast{};
};

WindowCapture captured(OffscreenAttachment& attached, const SampleWindow& sample, const CaptureRequest& request) {
    OffscreenCaptureOptions options{};
    options.colour_scheme = request.scheme;
    options.high_contrast = request.high_contrast;
    options.include_backplane = false;
    const std::optional<WindowCapture> capture = attached.capture_control(sample.graph, options);
    require(capture.has_value() && (*capture).pixel_width == 520U && (*capture).pixel_height == 330U,
            "the sample graph is captured at its size");
    return *capture;
}

// Idle frames a capture may take to make its patterns: one a frame, at
// most three layers.
constexpr std::size_t maximum_settle_frames = 16U;

// A capture with its procedural patterns made: the first paint queues them
// and draws their grounds (E10), the idle frames make them, one a frame,
// and the second capture shows them. Damage is taken first, since an idle
// frame waits while a paint is pending. Bounded by maximum_settle_frames.
WindowCapture settled(OffscreenAttachment& attached, const SampleWindow& sample, const CaptureRequest& request) {
    static_cast<void>(captured(attached, sample, request));
    Window& window = *sample.window;
    FrameTime now = FrameClock::now();
    std::size_t frame = 0U;
    while (frame < maximum_settle_frames && window.engine_parts().patterns_waiting()) {
        static_cast<void>(window.take_damage());
        now += std::chrono::milliseconds(16);
        static_cast<void>(window.poll_frame_schedule(now));
        frame = frame + 1U;
    }
    require(!window.engine_parts().patterns_waiting(), "the texture's patterns are made in idle frames");
    const WindowCapture shown = captured(attached, sample, request);
    return shown;
}

// The capture's pixel at (x, y): B, G, R, A.
std::array<std::uint8_t, 4> pixel_at(const WindowCapture& capture, std::uint32_t x, std::uint32_t y) {
    const std::size_t at = (static_cast<std::size_t>(y) * capture.pixel_width + x) * 4U;
    std::array<std::uint8_t, 4> pixel{};
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        pixel[channel] = static_cast<std::uint8_t>(capture.bgra[at + channel]);
    }
    return pixel;
}

int channel_distance(std::array<std::uint8_t, 4> left, std::array<std::uint8_t, 4> right) {
    int largest = 0;
    for (std::size_t channel = 0U; channel < 3U; ++channel) {
        const int difference = static_cast<int>(left[channel]) - static_cast<int>(right[channel]);
        const int magnitude = difference < 0 ? -difference : difference;
        largest = std::max(largest, magnitude);
    }
    return largest;
}

// An empty cell of the canvas at zoom 1 with world (0, 0) at (0.5, 0.5):
// minor lines lie on the pixel centres 16k, so (8, 8)'s pixel is clear of
// them and of every part; (16, 8)'s lies on a minor line.
constexpr std::uint32_t empty_x = 8U;
constexpr std::uint32_t empty_y = 8U;
constexpr std::uint32_t minor_x = 16U;

void canvas_pixels() {
    const std::unique_ptr<OffscreenRaster> raster = made_raster();
    const SampleWindow sample = made_sample(nullptr);
    (*sample.graph).settings.flow = false;
    OffscreenAttachment attached(*raster, *sample.window);
    const WindowCapture light = captured(attached, sample, CaptureRequest{});
    const std::array<std::uint8_t, 4> paper = pixel_at(light, empty_x, empty_y);
    require(channel_distance(paper, std::array<std::uint8_t, 4>{0xfd, 0xfc, 0xfb, 0xff}) <= 1,
            "the paper canvas is #fbfcfd between lines");
    require(channel_distance(pixel_at(light, minor_x, empty_y), paper) >= 8, "a minor grid line shows on paper");

    const WindowCapture dark = captured(attached, sample, CaptureRequest{ColourScheme::dark, false});
    const std::array<std::uint8_t, 4> well = pixel_at(dark, empty_x, empty_y);
    require(static_cast<int>(well[0]) + well[1] + well[2] < (static_cast<int>(paper[0]) + paper[1] + paper[2]) / 2,
            "the dark canvas is dark");

    const WindowCapture contrast = captured(attached, sample, CaptureRequest{ColourScheme::light, true});
    require(channel_distance(pixel_at(contrast, minor_x, empty_y), pixel_at(contrast, empty_x, empty_y)) == 0,
            "forced high contrast draws no minor line");

    require((*sample.graph).add_class("blue"), "the blue variant is selected");
    const WindowCapture blue = captured(attached, sample, CaptureRequest{});
    const std::array<std::uint8_t, 4> royale = pixel_at(blue, empty_x, empty_y);
    require(royale[0] > royale[2] + 64U, "the blue variant's canvas is blue");
}

void texture_pixels() {
    const std::unique_ptr<OffscreenRaster> raster = made_raster();
    const SampleWindow plain = made_sample(nullptr);
    (*plain.graph).settings.flow = false;
    std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
    select_texture_theme(*resources, felt_texture);
    const SampleWindow textured = made_sample(resources);
    (*textured.graph).settings.flow = false;
    const CaptureRequest dark{ColourScheme::dark, false};
    WindowCapture off{};
    WindowCapture on{};
    WindowCapture dark_off{};
    WindowCapture dark_on{};
    {
        OffscreenAttachment attached(*raster, *plain.window);
        off = captured(attached, plain, CaptureRequest{});
        dark_off = captured(attached, plain, dark);
    }
    {
        OffscreenAttachment attached(*raster, *textured.window);
        on = settled(attached, textured, CaptureRequest{});
        dark_on = settled(attached, textured, dark);
    }
    int largest = 0;
    int changed = 0;
    for (std::uint32_t y = 0U; y < 16U; ++y) {
        for (std::uint32_t x = 1U; x < 16U; ++x) {
            const int distance = channel_distance(pixel_at(on, x, y), pixel_at(off, x, y));
            largest = std::max(largest, distance);
            if (distance > 0) changed = changed + 1;
        }
    }
    require(largest <= 9, "a 3 % texture changes a channel by at most 3 % (and rounding)");
    require(changed > 0, "the texture shows");
    // In dark the texture screens over the well: within the same bound,
    // and visible.
    int dark_largest = 0;
    int dark_changed = 0;
    for (std::uint32_t y = 0U; y < 16U; ++y) {
        for (std::uint32_t x = 1U; x < 16U; ++x) {
            const int distance = channel_distance(pixel_at(dark_on, x, y), pixel_at(dark_off, x, y));
            dark_largest = std::max(dark_largest, distance);
            if (distance > 0) dark_changed = dark_changed + 1;
        }
    }
    require(dark_largest <= 9 && dark_changed > 0, "the texture shows in dark, within 3 %");
}

void flow_pixels() {
    const std::unique_ptr<OffscreenRaster> raster = made_raster();
    const SampleWindow sample = made_sample(nullptr);
    OffscreenAttachment attached(*raster, *sample.window);
    (*sample.graph).settings.still = true;
    const WindowCapture still_first = captured(attached, sample, CaptureRequest{});
    static_cast<void>((*sample.graph).flow.advance(start + std::chrono::milliseconds(50)));
    const WindowCapture still_second = captured(attached, sample, CaptureRequest{});
    require(still_first.bgra == still_second.bgra, "still frames do not move");
    (*sample.graph).settings.still = false;
    const WindowCapture moving_first = captured(attached, sample, CaptureRequest{});
    static_cast<void>((*sample.graph).flow.advance(start + std::chrono::milliseconds(100)));
    const WindowCapture moving_second = captured(attached, sample, CaptureRequest{});
    require(!(moving_first.bgra == moving_second.bgra), "the flow moves between frames");
}

// -- preview -------------------------------------------------------------------

void write_capture(const WindowCapture& capture, const std::string& directory, std::string_view name) {
    std::vector<std::byte> png{};
    require(encode_window_capture_png(capture, png), "a capture encodes");
    const std::string path = directory + "/" + std::string(name) + ".png";
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    require(file.good(), "the preview directory takes a file");
    file.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    require(file.good(), "the preview file is written");
    std::cout << path << '\n';
}

struct PreviewShot final {
    std::string_view name{};
    std::string_view variant{};
    CaptureRequest request{};
    CanvasView view{};
};

// One texture shot: its file name, its background-texture value, and
// whether a dark shot follows (name-dark).
struct TextureShot final {
    std::string_view name{};
    std::string_view value{};
    bool dark{};
};

void write_preview(const std::string& directory) {
    const std::unique_ptr<OffscreenRaster> raster = made_raster();
    const CanvasView normal{Point{0.5, 0.5}, 1.0};
    const CaptureRequest light{};
    const CaptureRequest dark{ColourScheme::dark, false};
    const CaptureRequest contrast{ColourScheme::light, true};
    const std::array<PreviewShot, 15> shots{{
        {"light", "", light, normal},
        {"dark", "", dark, normal},
        {"grid-paper", "paper", light, normal},
        {"grid-blue", "blue", light, normal},
        {"grid-green", "green", light, normal},
        {"grid-grey", "grey", light, normal},
        {"grid-blue-dark", "blue", dark, normal},
        {"high-contrast", "", contrast, normal},
        {"zoom-out", "", light, CanvasView{Point{160.5, 100.5}, 0.35}},
        {"zoom-normal", "", light, normal},
        // Zoom 4 on the Source node's output port, its lamp and the
        // links leaving it (world (175, 68) at the shot's centre).
        {"zoom-in", "", light, CanvasView{Point{-440.5, -107.5}, 4.0}},
        {"links", "", light, CanvasView{Point{-220.5, -60.5}, 1.5}},
        {"lamps", "", light, CanvasView{Point{-200.5, -40.5}, 1.6}},
        {"lamps-dark", "", dark, CanvasView{Point{-200.5, -40.5}, 1.6}},
        {"links-blue", "blue", light, CanvasView{Point{-220.5, -60.5}, 1.5}},
    }};
    {
        const SampleWindow sample = made_sample(nullptr);
        OffscreenAttachment attached(*raster, *sample.window);
        (*sample.graph).settings.still = true;
        for (const PreviewShot& shot : shots) {
            (*sample.graph).settings.view = shot.view;
            const std::array<std::string_view, 4> variants{"paper", "blue", "green", "grey"};
            for (const std::string_view variant : variants) static_cast<void>((*sample.graph).remove_class(variant));
            if (!shot.variant.empty()) static_cast<void>((*sample.graph).add_class(shot.variant));
            write_capture(captured(attached, sample, shot.request), directory, shot.name);
        }
        for (const std::string_view variant : std::array<std::string_view, 4>{"paper", "blue", "green", "grey"}) {
            static_cast<void>((*sample.graph).remove_class(variant));
        }
        (*sample.graph).settings.view = CanvasView{Point{-160.5, -60.5}, 2.0};
        (*sample.graph).settings.still = false;
        const std::array<std::string_view, 4> frames{"flow-0", "flow-1", "flow-2", "flow-3"};
        for (std::size_t frame = 0U; frame < frames.size(); ++frame) {
            const std::chrono::milliseconds elapsed(50 * static_cast<long long>(frame));
            static_cast<void>((*sample.graph).flow.advance(start + elapsed));
            write_capture(captured(attached, sample, light), directory, frames[frame]);
        }
        (*sample.graph).settings.view = normal;
        (*sample.graph).settings.still = true;
        write_capture(captured(attached, sample, light), directory, "texture-off");
    }
    // Each texture kind in light and dark, then two layers: one window
    // each, its theme compiled from a sheet alone.
    const std::array<TextureShot, 5> textures{{
        {"texture-felt", "procedural(felt, 31, 64px, 1) multiply 3%", true},
        {"texture-paper-fibre", "procedural(paper-fibre, 17, 64px, 1) multiply 3%", true},
        {"texture-orange-peel", "finish(orange-peel, #ffffff, #c8c8c8, 5) multiply 3%", true},
        {"texture-cloth-weave", "finish(cloth-weave, #ffffff, #d8d8d8, 9) multiply 3%", true},
        {"texture-layered",
         "procedural(paper-fibre, 17, 64px, 1) multiply 2%, procedural(felt, 31, 48px, 1) multiply 2%", false},
    }};
    for (const TextureShot& texture : textures) {
        std::shared_ptr<ApplicationResources> resources = std::make_shared<ApplicationResources>();
        select_texture_theme(*resources, texture.value);
        const SampleWindow textured = made_sample(resources);
        (*textured.graph).settings.still = true;
        (*textured.graph).settings.flow = false;
        OffscreenAttachment attached(*raster, *textured.window);
        write_capture(settled(attached, textured, light), directory, texture.name);
        if (!texture.dark) continue;
        const std::string dark_name = std::string(texture.name) + "-dark";
        write_capture(settled(attached, textured, dark), directory, dark_name);
    }
}

} // namespace

int main(int argc, char** argv) {
    const std::span<char*> arguments(argv, static_cast<std::size_t>(argc));
    if (arguments.size() == 3U && std::string_view(arguments[1]) == "--preview") {
        write_preview(std::string(arguments[2]));
        return 0;
    }
    canvas_pixels();
    texture_pixels();
    flow_pixels();
    std::cout << "gui_forms_canvas_look_raster_tests passed\n";
    return 0;
}
