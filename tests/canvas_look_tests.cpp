// The node canvas's look, theme side (cp31): the canvas anatomy; the
// default theme's canvas recipes resolved in light, derived dark and forced
// high contrast; the grid variants switched by class; the texture's
// contrast clamp; the paint functions' commands (the background's fill,
// texture group and grid level at three zooms, nodes, links with their
// halo and dashes, the marquee); and the link_flow effect: its frames
// wanted only while a link flows and motion is not reduced, its still
// pose at phase 0, and its phase following the ticks. Time comes from the
// test, never the wall clock. Pixels are checked in canvas_look_raster_tests.

#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/link_flow_effects.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/engine/effect_registry/effect_registry.hpp"
#include "gui_forms/theme/canvas_look/canvas_look.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/types/painter/painter.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "gui_forms_canvas_look_tests: " << message << '\n';
    std::exit(1);
}

// Records the commands the canvas paint functions and the flow draw.
class RecordingPainter final : public Painter {
public:
    void save() override { saves = saves + 1; }
    void restore() override { restores = restores + 1; }
    void translate(Point) override {}
    void clip_rect(Rect rect) override { clips.push_back(rect); }
    void fill_rect(Rect rect, Color color) override {
        const Fill fill{rect, color};
        fills.push_back(fill);
    }
    void fill_rounded_rect(Rect rect, double, Color color) override {
        const Fill fill{rect, color};
        rounded_fills.push_back(fill);
    }
    void stroke_rect(Rect, Color, double) override {}
    void stroke_rounded_rect(Rect rect, double radius, Color color, double width) override {
        const Outline outline{rect, radius, color, width};
        outlines.push_back(outline);
    }
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override {}
    void fill_linear_gradient_spread(Rect, Point, Point, std::span<const GradientStop>, GradientSpreadMode) override {}
    void fill_radial_gradient(Rect, Point, Size, std::span<const GradientStop>) override {}
    void draw_line(Point, Point, Color, double) override { lines = lines + 1; }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color) override {}
    void draw_image(ImageId, Rect, double) override {}
    void stroke_polyline(std::span<const Point> points, const StrokeStyle& style) override {
        const Polyline polyline{points.size(), style};
        polylines.push_back(polyline);
    }
    void fill_grid_pattern(Rect, const GridPattern& pattern) override { grids.push_back(pattern); }
    void begin_group(const PaintGroup& group) override { groups.push_back(group); }
    void end_group() override { group_ends = group_ends + 1; }
    void fill_image_pattern(ImageId image, Size source_pixel_size, Rect destination, Size logical_tile_size,
                            ImagePatternWrap, double) override {
        const Pattern pattern{image, source_pixel_size, destination, logical_tile_size};
        patterns.push_back(pattern);
    }

    struct Fill final {
        Rect rect{};
        Color color{};
    };
    struct Outline final {
        Rect rect{};
        double radius{};
        Color color{};
        double width{};
    };
    struct Polyline final {
        std::size_t points{};
        StrokeStyle style{};
    };
    struct Pattern final {
        ImageId image{};
        Size source{};
        Rect destination{};
        Size tile{};
    };
    int saves{};
    int restores{};
    int lines{};
    int group_ends{};
    std::vector<Rect> clips{};
    std::vector<Fill> fills{};
    std::vector<Fill> rounded_fills{};
    std::vector<Outline> outlines{};
    std::vector<Polyline> polylines{};
    std::vector<GridPattern> grids{};
    std::vector<PaintGroup> groups{};
    std::vector<Pattern> patterns{};
};

const FrameTime start{std::chrono::seconds(1000)};

bool same_rgb(Color colour, std::uint8_t red, std::uint8_t green, std::uint8_t blue) {
    return colour.red == red && colour.green == green && colour.blue == blue;
}

bool near_alpha(Color colour, int alpha) {
    const int difference = static_cast<int>(colour.alpha) - alpha;
    return difference >= -1 && difference <= 1;
}

// Relative brightness, enough to tell a light colour from its dark form.
int channel_sum(Color colour) {
    const int sum = static_cast<int>(colour.red) + static_cast<int>(colour.green) + static_cast<int>(colour.blue);
    return sum;
}

ControlVisualContext light_context() {
    const ControlVisualContext context{};
    return context;
}

ControlVisualContext dark_context() {
    ControlVisualContext context{};
    context.dark = true;
    return context;
}

ControlVisualContext contrast_context() {
    ControlVisualContext context{};
    context.high_contrast = true;
    return context;
}

std::size_t index_of(CanvasLinkState state) { return static_cast<std::size_t>(state); }
std::size_t index_of(CanvasNodeState state) { return static_cast<std::size_t>(state); }
std::size_t index_of(CanvasLampState state) { return static_cast<std::size_t>(state); }
std::size_t index_of(CanvasPortState state) { return static_cast<std::size_t>(state); }

void anatomy_checks() {
    const ControlAnatomy& anatomy = node_canvas_anatomy();
    require(anatomy.control_id == "node_canvas" && anatomy.control_id == node_canvas_control_id,
            "the canvas anatomy is node_canvas's");
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none, "the canvas anatomy is valid");
    require(anatomy.parts.size() == 11U, "the canvas has eleven parts");
    const std::array<std::string_view, 11> names{"region", "grid_minor",  "grid_major", "node",
                                                 "node_header", "node_selection", "port", "link",
                                                 "dashed_link", "lamp", "marquee"};
    const std::array<PartRole, 11> roles{PartRole::region, PartRole::grid_minor, PartRole::grid_major,
                                         PartRole::node,   PartRole::header,     PartRole::selection,
                                         PartRole::port,   PartRole::link,       PartRole::link,
                                         PartRole::lamp,   PartRole::marquee};
    bool listed = anatomy.parts.size() == names.size();
    for (std::size_t index = 0U; listed && index < names.size(); ++index) {
        listed = anatomy.parts[index].name == names[index] && anatomy.parts[index].role == roles[index];
    }
    require(listed, "the parts are named and roled as the part table states");
    require(part_role_name(PartRole::node) == "node" && part_role_name(PartRole::marquee) == "marquee",
            "the canvas roles have registered names");
}

void light_recipes() {
    const std::shared_ptr<const Theme> theme = default_theme();
    require((*theme).definition().dark_variant == ThemeDarkVariant::derived, "the default theme derives its dark");
    const std::shared_ptr<Panel> canvas = make_control<Panel>(StableId("canvas.light"));
    const CanvasLook look = resolve_canvas_look(*theme, *canvas, light_context());
    require(same_rgb(look.background.fill, 0xfb, 0xfc, 0xfd), "the paper canvas is #fbfcfd");
    require(same_rgb(look.grid.minor, 99, 132, 178) && near_alpha(look.grid.minor, 56),
            "the minor grid is the ordinary line at 22 %");
    require(same_rgb(look.grid.major, 99, 132, 178) && near_alpha(look.grid.major, 115),
            "the major grid is the ordinary line at 45 %");
    require(look.grid.spacing == 16.0 && look.grid.band.minimum_spacing == 12.0 &&
                look.grid.band.maximum_spacing == 48.0 && look.grid.line_width == 1.0 && look.grid.minor_shown,
            "the grid spaces 16 world units with a 12-to-48 band");
    require(!look.texture.has_value(), "the default theme sets no texture");
    const CanvasArea& node = look.nodes[index_of(CanvasNodeState::normal)];
    require(same_rgb(node.fill, 255, 255, 255) && same_rgb(node.stroke, 0x63, 0x84, 0xb2) &&
                node.stroke_width == 1.0 && node.corner_radius == 6.0,
            "a node is white, outlined in the ordinary line, with 6-unit corners");
    require(same_rgb(look.nodes[index_of(CanvasNodeState::selected)].stroke, 0x2f, 0x69, 0xbe),
            "a selected node is outlined in the accent");
    require(same_rgb(look.nodes[index_of(CanvasNodeState::disabled)].fill, 0xf4, 0xf4, 0xf5),
            "a disabled node is the near-white stock");
    require(same_rgb(look.headers[index_of(CanvasNodeState::normal)].fill, 0xca, 0xde, 0xfb) &&
                same_rgb(look.titles[index_of(CanvasNodeState::normal)], 0x15, 0x37, 0x65),
            "a header is chrome with chrome ink");
    require(same_rgb(look.titles[index_of(CanvasNodeState::disabled)], 0x62, 0x79, 0x97),
            "a disabled header's title is the disabled ink");
    require(same_rgb(look.selection.stroke, 0x2f, 0x69, 0xbe) && look.selection.stroke_width == 2.0 &&
                look.selection.corner_radius == 8.0,
            "the selection ring is two accent units with 8-unit corners");
    require(same_rgb(look.ports[index_of(CanvasPortState::hot)].fill, 0xff, 0xcf, 0x66),
            "a hot port is gold");
    require(same_rgb(look.ports[index_of(CanvasPortState::drop_rejected)].stroke, 0x9b, 0x1c, 0x1c),
            "a rejected drop is outlined in the invalid ink");
    const CanvasLine& plain = look.links[index_of(CanvasLinkState::normal)];
    require(same_rgb(plain.colour, 0x49, 0x6c, 0x9f) && plain.width == 2.0 && plain.dash == 0.0 &&
                plain.halo_width == 0.0,
            "a link is two solid units of the dark line, with no halo");
    const CanvasLine& selected = look.links[index_of(CanvasLinkState::selected)];
    require(same_rgb(selected.colour, 0x2f, 0x69, 0xbe) && selected.halo_width == 4.0 &&
                same_rgb(selected.halo, 47, 105, 190) && near_alpha(selected.halo, 77),
            "a selected link is the accent with a 4-unit halo at 30 %");
    const CanvasLine& invalid = look.links[index_of(CanvasLinkState::invalid)];
    require(same_rgb(invalid.colour, 0xb4, 0x41, 0x3c) && invalid.dash == 2.0 && invalid.gap == 3.0,
            "an invalid link is the signal red, dotted 2 on 3");
    const CanvasLine& dashed = look.dashed_links[index_of(CanvasLinkState::normal)];
    require(same_rgb(dashed.colour, 0x49, 0x6c, 0x9f) && dashed.dash == 6.0 && dashed.gap == 4.0,
            "a dashed link dashes 6 on 4");
    require(same_rgb(look.lamps[index_of(CanvasLampState::on)].fill, 0x3f, 0xae, 0x5a) &&
                same_rgb(look.lamps[index_of(CanvasLampState::error)].fill, 0xd9, 0x46, 0x3e) &&
                same_rgb(look.lamps[index_of(CanvasLampState::off)].fill, 0xbe, 0xcf, 0xe4),
            "lamps are green on, red in error and chrome off");
    require(same_rgb(look.marquee.fill, 47, 105, 190) && near_alpha(look.marquee.fill, 31) &&
                look.marquee_outline.dash == 4.0 && look.marquee_outline.gap == 3.0,
            "the marquee is a 12 % accent wash with a 4-on-3 outline");
    require(look.flow.style == CanvasFlowStyle::dots && look.flow.speed == 48.0 && look.flow.spacing == 16.0,
            "links flow in dots at 48 world units a second, 16 apart");
    const CanvasLook again = resolve_canvas_look(*theme, *canvas, light_context());
    require(again == look, "resolution is deterministic");
}

void dark_recipes() {
    const std::shared_ptr<const Theme> theme = default_theme();
    const std::shared_ptr<Panel> canvas = make_control<Panel>(StableId("canvas.dark"));
    const CanvasLook light = resolve_canvas_look(*theme, *canvas, light_context());
    const CanvasLook dark = resolve_canvas_look(*theme, *canvas, dark_context());
    require(channel_sum(dark.background.fill) < channel_sum(light.background.fill) / 2,
            "the dark canvas is a dark well");
    require(channel_sum(dark.nodes[0].fill) < channel_sum(light.nodes[0].fill) / 2, "dark nodes are dark stock");
    require(channel_sum(dark.titles[0]) > channel_sum(light.titles[0]), "dark titles are light ink");
    require(dark.grid.minor.alpha == light.grid.minor.alpha && dark.grid.major.alpha == light.grid.major.alpha,
            "the dark rule keeps a grid line's translucency");
    require(dark.links[index_of(CanvasLinkState::selected)].halo.alpha ==
                light.links[index_of(CanvasLinkState::selected)].halo.alpha,
            "and a halo's");
    require(dark.grid.spacing == light.grid.spacing && dark.flow == light.flow &&
                dark.dashed_links[0].dash == light.dashed_links[0].dash,
            "geometry and flow do not change with the scheme");
    require(!(dark == light), "the dark look differs from the light");
}

void high_contrast_recipes() {
    const std::shared_ptr<const Theme> theme = default_theme();
    const std::shared_ptr<Panel> canvas = make_control<Panel>(StableId("canvas.contrast"));
    const CanvasLook look = resolve_canvas_look(*theme, *canvas, contrast_context());
    require(!same_rgb(look.background.fill, 0xfb, 0xfc, 0xfd), "forced high contrast ignores the rules' colours");
    require(!look.grid.minor_shown && !look.texture.has_value(), "no minor grid and no texture in high contrast");
    require(look.marquee.fill.alpha == 0U, "the marquee has no fill in high contrast");
    require(look.links[index_of(CanvasLinkState::selected)].halo_width == 2.0 &&
                look.links[index_of(CanvasLinkState::normal)].halo_width == 0.0 &&
                look.dashed_links[index_of(CanvasLinkState::selected)].halo_width == 2.0,
            "only a selected link has a halo, two units wide");
    require(look.dashed_links[index_of(CanvasLinkState::normal)].dash == 6.0,
            "dashes are geometry and stay in high contrast");
    const std::shared_ptr<Panel> blue = make_control<Panel>(StableId("canvas.contrast.blue"));
    require((*blue).add_class("blue"), "the blue class is added");
    const CanvasLook blue_look = resolve_canvas_look(*theme, *blue, contrast_context());
    require(blue_look.background == look.background, "grid variants do not apply in high contrast");
}

void grid_variants() {
    const std::shared_ptr<const Theme> theme = default_theme();
    const std::shared_ptr<Panel> canvas = make_control<Panel>(StableId("canvas.variants"));
    const CanvasLook paper = resolve_canvas_look(*theme, *canvas, light_context());
    require((*canvas).add_class("blue"), "the blue class is added");
    const CanvasLook blue = resolve_canvas_look(*theme, *canvas, light_context());
    require(same_rgb(blue.background.fill, 0x1f, 0x4f, 0x8f) && same_rgb(blue.grid.minor, 255, 255, 255) &&
                near_alpha(blue.grid.minor, 41),
            "blue: white lines on the royale blue");
    require(blue.nodes == paper.nodes && blue.links == paper.links, "a variant changes the grid alone");
    require((*canvas).remove_class("blue") && (*canvas).add_class("green"), "switched to green");
    const CanvasLook green = resolve_canvas_look(*theme, *canvas, light_context());
    require(same_rgb(green.background.fill, 0xe9, 0xf2, 0xe4) && same_rgb(green.grid.major, 46, 125, 79),
            "green: a pale green canvas with green lines");
    require((*canvas).remove_class("green") && (*canvas).add_class("grey"), "switched to grey");
    const CanvasLook grey = resolve_canvas_look(*theme, *canvas, light_context());
    require(same_rgb(grey.background.fill, 0x8a, 0x8f, 0x96) && same_rgb(grey.grid.minor, 27, 39, 51),
            "grey: a mid grey canvas with ink lines");
    require((*canvas).remove_class("grey") && (*canvas).add_class("paper"), "switched to paper");
    const CanvasLook named_paper = resolve_canvas_look(*theme, *canvas, light_context());
    require(named_paper == paper, "the paper class is the default look");
    const CanvasLook dark_blue_base = resolve_canvas_look(*theme, *canvas, dark_context());
    require((*canvas).remove_class("paper") && (*canvas).add_class("blue"), "switched to blue again");
    const CanvasLook dark_blue = resolve_canvas_look(*theme, *canvas, dark_context());
    require(!(dark_blue.background == dark_blue_base.background), "a variant also applies in dark");
}

void texture_clamp() {
    require(clamp_canvas_texture_contrast(0.02) == 0.02, "a contrast within the rule is kept");
    require(clamp_canvas_texture_contrast(0.05) == maximum_canvas_texture_contrast, "more is clamped to 3 %");
    require(maximum_canvas_texture_contrast == 0.03, "the rule is 3 %");
    require(clamp_canvas_texture_contrast(-1.0) == 0.0, "a negative contrast is 0");
    require(clamp_canvas_texture_contrast(std::numeric_limits<double>::quiet_NaN()) == 0.0, "NaN is 0");
    require(clamp_canvas_texture_contrast(std::numeric_limits<double>::infinity()) == 0.0, "infinity is 0");
}

CanvasLook stock_look() {
    const std::shared_ptr<const Theme> theme = default_theme();
    const std::shared_ptr<Panel> canvas = make_control<Panel>(StableId("canvas.paint"));
    const CanvasLook look = resolve_canvas_look(*theme, *canvas, light_context());
    return look;
}

void background_commands() {
    CanvasLook look = stock_look();
    const Rect area{0.0, 0.0, 100.0, 100.0};
    CanvasView view{};
    view.origin = Point{5.0, 7.0};
    const std::array<double, 3> zooms{0.05, 1.0, 8.0};
    for (const double zoom : zooms) {
        view.zoom = zoom;
        RecordingPainter painter;
        paint_canvas_background(painter, look, area, view);
        require(painter.fills.size() == 1U && painter.groups.empty() && painter.grids.size() == 1U,
                "an untextured background is one fill and one grid pass");
        const GridPattern& grid = painter.grids[0];
        require(grid.spacing >= 12.0 && grid.spacing < 48.0 && grid.origin == view.origin && grid.major_every == 4U,
                "the grid level keeps the band at every zoom, anchored at world (0, 0)");
    }
    look.texture = ControlCanvasTexture{ImageId{7U}, Size{64.0, 64.0}, 2.0, 0.08};
    view.zoom = 1.0;
    RecordingPainter textured;
    paint_canvas_background(textured, look, area, view);
    require(textured.groups.size() == 1U && textured.group_ends == 1 && textured.patterns.size() == 1U,
            "a texture is one group holding one pattern");
    const PaintGroup& group = textured.groups[0];
    require(group.composite == PaintGroupComposite::multiply && group.opacity == maximum_canvas_texture_contrast &&
                group.isolated && group.bounds == area,
            "the texture multiplies at the clamped contrast over the area");
    const RecordingPainter::Pattern& pattern = textured.patterns[0];
    require(pattern.image == ImageId{7U} && pattern.tile == Size{32.0, 32.0},
            "tiles are the image's logical size (pixels over density)");
    require(pattern.destination.x == -27.0 && pattern.destination.y == -25.0 &&
                pattern.destination.width == 127.0 && pattern.destination.height == 125.0,
            "the tiles are anchored on the world lattice through the origin");
    view.origin = Point{37.0, 39.0};
    RecordingPainter panned;
    paint_canvas_background(panned, look, area, view);
    require(panned.patterns.size() == 1U && panned.patterns[0].destination.x == -27.0 &&
                panned.patterns[0].destination.y == -25.0,
            "a pan by one tile leaves the lattice where it was");
    look.texture.reset();
    look.grid.minor_shown = false;
    RecordingPainter contrast;
    paint_canvas_background(contrast, look, area, view);
    require(contrast.grids.size() == 1U && contrast.grids[0].minor.alpha == 0U, "a hidden minor grid is clear");
    view.zoom = std::numeric_limits<double>::quiet_NaN();
    RecordingPainter unplaced;
    paint_canvas_background(unplaced, look, area, view);
    require(unplaced.fills.size() == 1U && unplaced.grids.empty(), "an unplaced view draws the fill alone");
}

void part_commands() {
    const CanvasLook look = stock_look();
    RecordingPainter node;
    paint_canvas_node(node, look, Rect{10.0, 10.0, 120.0, 80.0}, 24.0, CanvasNodeState::normal);
    require(node.saves == node.restores && node.saves == 1 && node.clips.size() == 1U &&
                node.clips[0] == Rect{10.0, 10.0, 120.0, 24.0},
            "the header is clipped to its band");
    require(node.rounded_fills.size() == 2U && node.lines == 1 && node.outlines.size() == 1U,
            "a node is its body, its header, the header's rule and its outline");
    require(node.outlines[0].rect == Rect{10.5, 10.5, 119.0, 79.0}, "the outline lies inside the body");
    RecordingPainter selection;
    paint_canvas_node_selection(selection, look, Rect{10.0, 10.0, 120.0, 80.0});
    require(selection.outlines.size() == 1U && selection.outlines[0].rect == Rect{7.0, 7.0, 126.0, 86.0} &&
                selection.outlines[0].width == 2.0,
            "the ring lies two units outside the body plus half its width");
    const std::array<Point, 3> path{Point{0.0, 0.0}, Point{40.0, 10.0}, Point{80.0, 0.0}};
    RecordingPainter selected;
    paint_canvas_link(selected, look, path, CanvasLinkState::selected, CanvasLinkStyle::plain);
    require(selected.polylines.size() == 2U && selected.polylines[0].style.width == 10.0 &&
                selected.polylines[0].style.dash_count == 0U && selected.polylines[1].style.width == 2.0,
            "a selected link draws its halo, solid and wider, under its stroke");
    require(selected.polylines[1].style.join == StrokeJoin::round && selected.polylines[1].style.cap == StrokeCap::round,
            "a solid link has round joins and caps");
    RecordingPainter dashed;
    paint_canvas_link(dashed, look, path, CanvasLinkState::normal, CanvasLinkStyle::dashed);
    require(dashed.polylines.size() == 1U && dashed.polylines[0].style.dash_count == 2U &&
                dashed.polylines[0].style.dashes[0] == 6.0 && dashed.polylines[0].style.dashes[1] == 4.0,
            "a dashed link is one dashed stroke");
    RecordingPainter one_point;
    paint_canvas_link(one_point, look, std::span<const Point>(path.data(), 1U), CanvasLinkState::normal,
                      CanvasLinkStyle::plain);
    require(one_point.polylines.empty(), "a link of one point draws nothing");
    RecordingPainter lamp;
    paint_canvas_lamp(lamp, look, Point{20.0, 20.0}, 4.0, CanvasLampState::on);
    require(lamp.rounded_fills.size() == 1U && lamp.rounded_fills[0].rect == Rect{16.0, 16.0, 8.0, 8.0} &&
                lamp.outlines.size() == 1U,
            "a lamp is a filled and outlined disc");
    RecordingPainter port;
    paint_canvas_port(port, look, Point{20.0, 20.0}, 0.0, CanvasPortState::normal);
    require(port.rounded_fills.empty() && port.outlines.empty(), "a port of no radius draws nothing");
    RecordingPainter marquee;
    paint_canvas_marquee(marquee, look, Rect{0.0, 0.0, 50.0, 30.0});
    require(marquee.fills.size() == 1U && marquee.polylines.size() == 1U && marquee.polylines[0].points == 5U &&
                marquee.polylines[0].style.dash_count == 2U,
            "the marquee is a wash and a closed dashed outline");
}

const EffectEntry& link_flow() {
    const EffectEntry* const entry = built_in_effect_registry().find("link_flow");
    require(entry != nullptr, "link_flow is a built-in effect");
    return *entry;
}

EffectView<NodeCanvas> flow_view(std::size_t links) {
    EffectView<NodeCanvas> view{};
    view.style = CanvasFlowStyle::dots;
    view.speed = 48.0;
    view.spacing = 16.0;
    view.width = 2.0;
    for (std::size_t index = 0U; index < links; ++index) {
        EffectPoints& points = view.links.items[index];
        const double y = 10.0 + 20.0 * static_cast<double>(index);
        points.items[0] = Point{0.0, y};
        points.items[1] = Point{100.0, y};
        points.count = 2U;
    }
    view.links.count = static_cast<std::uint32_t>(links);
    return view;
}

void flow_frames_wanted() {
    const EffectView<NodeCanvas> idle = flow_view(0U);
    require(!link_flow_wants_frames(idle, false), "no flowing link: no frames");
    const EffectView<NodeCanvas> flowing = flow_view(2U);
    require(link_flow_wants_frames(flowing, false), "a flowing link wants frames");
    require(!link_flow_wants_frames(flowing, true), "reduced motion wants none");
    EffectView<NodeCanvas> unstyled = flowing;
    unstyled.style = CanvasFlowStyle::none;
    require(!link_flow_wants_frames(unstyled, false), "flow none wants none");
    EffectView<NodeCanvas> halted = flowing;
    halted.speed = 0.0;
    require(!link_flow_wants_frames(halted, false), "a speed of 0 wants none");
    EffectView<NodeCanvas> degenerate = flowing;
    degenerate.links.items[0].count = 1U;
    degenerate.links.items[1].count = 0U;
    require(!link_flow_wants_frames(degenerate, false), "links of fewer than two points do not flow");
    const CanvasLook look = stock_look();
    const Color mark = link_flow_mark_colour(look);
    require(same_rgb(mark, 0xfb, 0xfc, 0xfd) && mark.alpha == 255U, "the marks are the canvas colour, opaque");
}

void flow_runs() {
    require(link_flow().parent == "node_canvas", "the flow names the node canvas");
    const EffectView<NodeCanvas> view = flow_view(2U);
    const Rect layer{0.0, 0.0, 120.0, 60.0};
    EffectInstance instance;
    instance.attach(link_flow(), 1U, start);
    EffectColours colours{};
    colours.primary = Color::rgba(251, 252, 253);
    instance.set_colours(colours);

    RecordingPainter still;
    instance.paint(still, layer, 1.0, true, view);
    require(still.polylines.size() == 2U, "the still pose draws every flowing link");
    require(still.polylines[0].style.dash_phase == 16.0 && still.polylines[0].style.cap == StrokeCap::round &&
                still.polylines[0].style.width == 1.0 && still.polylines[0].style.dash_count == 2U,
            "still dots rest at phase 0: round beads half the link's width");
    require(instance.last_meter().refused == 0U, "the flow stays within budget");
    const std::uint64_t rested = instance.state_checksum();
    static_cast<void>(instance.advance(start + std::chrono::milliseconds(50)));
    RecordingPainter still_again;
    instance.paint(still_again, layer, 1.0, true, view);
    require(instance.state_checksum() == rested && still_again.polylines[0].style.dash_phase == 16.0,
            "a still frame advances nothing");

    // A quarter second at 48 units a second over a 16-unit pitch is
    // three quarters of a pitch (in frames of three ticks).
    EffectInstance moving;
    moving.attach(link_flow(), 1U, start);
    moving.set_colours(colours);
    RecordingPainter first;
    moving.paint(first, layer, 1.0, false, view);
    RecordingPainter later;
    for (int frame = 1; frame <= 5; ++frame) {
        static_cast<void>(moving.advance(start + std::chrono::milliseconds(50 * frame)));
        later.polylines.clear();
        moving.paint(later, layer, 1.0, false, view);
    }
    require(later.polylines.size() == 2U && std::abs(later.polylines[0].style.dash_phase - 4.0) < 0.5,
            "the marks follow their ticks");

    EffectView<NodeCanvas> dashes = view;
    dashes.style = CanvasFlowStyle::dashes;
    RecordingPainter dashed;
    instance.paint(dashed, layer, 1.0, true, dashes);
    require(dashed.polylines.size() == 2U && dashed.polylines[0].style.dashes[0] == 8.0 &&
                dashed.polylines[0].style.cap == StrokeCap::butt && dashed.polylines[0].style.width == 3.0,
            "dashes are half-pitch breaks a little wider than the link");

    const EffectView<NodeCanvas> idle = flow_view(0U);
    RecordingPainter nothing;
    instance.paint(nothing, layer, 1.0, false, idle);
    require(nothing.polylines.empty(), "no flowing link draws nothing");
    EffectView<NodeCanvas> unstyled = view;
    unstyled.style = CanvasFlowStyle::none;
    RecordingPainter none;
    instance.paint(none, layer, 1.0, false, unstyled);
    require(none.polylines.empty(), "flow none draws nothing");
}

} // namespace

int main() {
    anatomy_checks();
    light_recipes();
    dark_recipes();
    high_contrast_recipes();
    grid_variants();
    texture_clamp();
    background_commands();
    part_commands();
    flow_frames_wanted();
    flow_runs();
    std::cout << "gui_forms_canvas_look_tests passed\n";
    return 0;
}
