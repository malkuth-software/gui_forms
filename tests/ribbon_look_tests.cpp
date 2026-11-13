// The Ribbon's look through the theme (look integration; the Plan Paint
// port's defects 1 to 7): every part is keyed, so `ribbon::part(...)` rules
// reach the tab rail, the tabs, their labels and the command plane; an
// authored command face stands at rest over the built-in flat face; the
// tab row's metrics (rail and plane heights, padding, gap, minimum widths,
// margins) and per-state label fonts come from the theme; the selected tab
// stands taller and runs into the plane; tabs take authored widths, a
// disabled state and, optionally, selection on release; the shapes (folder
// taper, lip, edge shadow) paint where a theme turns them on; Plan Paint's
// package compiles with them; and the default theme draws the rendering
// reference's tab row, every tab a tapered folder tucked under its left
// neighbour, and sets Backstage's column apart from its page.

#include "../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/commands/command/command.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/backstage_view.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/ribbon.hpp"
#include "gui_forms/dirty.hpp"
#include "gui_forms/engine/part_raster/part_raster.hpp"
#include "gui_forms/engine/types/engine_types.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/resources/image_registry/image_registry.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/theme/gui_forms_theme/gui_forms_theme.hpp"
#include "gui_forms/theme/stock_anatomies/stock_anatomies.hpp"
#include "gui_forms/theme/theme/theme.hpp"
#include "gui_forms/theme/theme_compiler/theme_compiler.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/types/paint_types/paint_types.hpp"
#include "gui_forms/types/painter/painter.hpp"
#include "gui_forms/window/presentation/colour_scheme.hpp"
#include "gui_forms/window/presentation/presentation_types.hpp"
#include "gui_forms/window/window.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Records solid fills, gradients, lines and text runs with their colours and
// fonts, in paint order. A face a rule's primitive draws arrives as an
// engine part raster: its centre pixel stands for the fill.
class LookPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color color) override { fills.push_back(color); }
    void fill_rounded_rect(Rect, double, Color color) override { fills.push_back(color); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color color, double) override { lines.push_back(color); }
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop> stops) override {
        ++gradients;
        for (const GradientStop& stop : stops) fills.push_back(stop.color);
    }
    void draw_text_utf8(Point, std::string_view text, FontSpec font, Color color) override {
        texts.emplace_back(text);
        text_colours.push_back(color);
        text_fonts.push_back(font);
    }
    void draw_image(ImageId, Rect, double) override {}
    void draw_part_raster(const std::shared_ptr<const PartRaster>& raster, Point, double) override {
        if (!raster) return;
        const DeviceExtent extent = (*raster).extent();
        if (extent.width == 0U || extent.height == 0U) return;
        const std::size_t offset = static_cast<std::size_t>(extent.height / 2U) * (*raster).row_bytes() +
                                   static_cast<std::size_t>(extent.width / 2U) * 4U;
        const std::span<const std::uint8_t> pixels = (*raster).pixels();
        fills.push_back(Color::rgba(pixels[offset + 2U], pixels[offset + 1U], pixels[offset], pixels[offset + 3U]));
    }
    void draw_part_raster_scaled(const std::shared_ptr<const PartRaster>& raster, Rect, double opacity) override {
        draw_part_raster(raster, Point{}, opacity);
    }

    [[nodiscard]] bool filled(Color color) const {
        for (const Color& fill : fills) {
            if (fill == color) return true;
        }
        return false;
    }
    // A fill in `color`'s red, green and blue at any alpha.
    [[nodiscard]] bool filled_rgb(Color color) const {
        for (const Color& fill : fills) {
            if (fill.red == color.red && fill.green == color.green && fill.blue == color.blue && fill.alpha != 0U) {
                return true;
            }
        }
        return false;
    }
    [[nodiscard]] bool lined(Color color) const {
        const bool any = lines_in(color) > 0U;
        return any;
    }
    [[nodiscard]] std::size_t lines_in(Color color) const {
        std::size_t count = 0U;
        for (const Color& line : lines) {
            if (line == color) ++count;
        }
        return count;
    }
    [[nodiscard]] std::optional<std::size_t> text_index(std::string_view text) const {
        for (std::size_t index = 0U; index < texts.size(); ++index) {
            if (texts[index] == text) return index;
        }
        return std::nullopt;
    }

    std::vector<Color> fills{};
    std::vector<Color> lines{};
    std::size_t gradients{};
    std::vector<std::string> texts{};
    std::vector<Color> text_colours{};
    std::vector<FontSpec> text_fonts{};
};

ThemePackage look_package(std::string_view sheet) {
    const std::string hash = theme_language::package_sha256_hex(theme_language::package_sha256(sheet));
    ThemePackage made{};
    made.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"look\", \"version\": \"1.0.0\", "
                    "\"engine\": \">=0.3\", \"sheet\": {\"path\": \"look.theme.css\", \"sha256\": \"" +
                    hash + "\"}}";
    made.files.push_back(ThemePackageFile{"look.theme.css", std::string(sheet)});
    return made;
}

std::shared_ptr<const Theme> theme_from_sheet(std::string_view text, std::string_view name) {
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    const ThemeCompileResult result = compile_theme_package(look_package(text), library, images, options);
    // A literal colour warns that a derived variant cannot remap it (W201);
    // any other finding fails the sheet.
    bool clean = result.succeeded();
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == "W201") continue;
        clean = false;
        std::cerr << "  " << diagnostic.code << " " << diagnostic.name << '\n';
    }
    require(clean, name);
    std::shared_ptr<const Theme> theme = result.theme();
    return theme;
}

std::vector<RibbonTabSpec> two_tabs() {
    const std::shared_ptr<Command> paste = std::make_shared<Command>("paste", "Paste");
    std::vector<RibbonTabSpec> tabs;
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard"};
    clipboard.commands.push_back({.stable_id = "paste", .command = paste, .image = ImageId{5}});
    home.groups.push_back(std::move(clipboard));
    RibbonTabSpec view{.stable_id = "view", .text = "&View"};
    RibbonGroupSpec zoom{.stable_id = "zoom", .caption = "Zoom"};
    zoom.commands.push_back({.stable_id = "zoom-in", .command = std::make_shared<Command>("zoom-in", "Zoom in")});
    view.groups.push_back(std::move(zoom));
    tabs.push_back(std::move(home));
    tabs.push_back(std::move(view));
    return tabs;
}

std::shared_ptr<Ribbon> ribbon_with(std::vector<RibbonTabSpec> tabs) {
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(std::move(tabs));
    return ribbon;
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

bool near(double left, double right) {
    const double difference = left > right ? left - right : right - left;
    return difference < 0.001;
}

constexpr Color rail_blue = Color::rgba(0x93, 0xad, 0xd0);
constexpr Color selected_face = Color::rgba(0xe8, 0xf2, 0xff);
constexpr Color plane_blue = Color::rgba(0xca, 0xde, 0xfb);
constexpr Color selected_ink = Color::rgba(0x17, 0x3f, 0x79);
constexpr Color raised_face = Color::rgba(0x10, 0x20, 0x30);
constexpr Color keyline = Color::rgba(0x49, 0x6c, 0x9f);
constexpr Color shadow_ink = Color::rgba(24, 56, 97);

// Defect 1: the rail, the tabs, their labels and the plane take the rules
// naming them.
void test_parts_are_keyed() {
    const std::shared_ptr<const Theme> theme = theme_from_sheet(
        "ribbon::part(tab-rail) { background: #93add0; border: none; box-shadow: none; }\n"
        "ribbon::part(tab):state(selected) { background: #e8f2ff; border: none; box-shadow: none; }\n"
        "ribbon::part(tab-label):state(selected) { color: #173f79; }\n"
        "ribbon::part(command-plane) { background: #cadefb; border: none; box-shadow: none; }\n",
        "the keyed-parts sheet compiles cleanly");
    std::shared_ptr<Ribbon> ribbon = ribbon_with(two_tabs());
    (*ribbon).set_theme_override(theme);
    Window window(ribbon, {700.0, 115.0});
    window.perform_layout();
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 115.0}));
    require(painter.filled(rail_blue), "the tab rail takes its rule");
    require(painter.filled(selected_face), "the selected tab takes its state's rule");
    require(painter.filled(plane_blue), "the command plane takes its rule");
    const std::optional<std::size_t> home = painter.text_index("Home");
    const std::optional<std::size_t> view = painter.text_index("View");
    require(home && view && painter.text_colours[*home] == selected_ink && !(painter.text_colours[*view] == selected_ink),
            "the selected tab's label takes its state's ink, the other tab's does not");
    require(home && view && *view < *home, "the selected tab paints after the others, over the plane's edge");
}

// Defect 2: a face a theme authors stands at rest over the built-in flat
// face.
void test_authored_face_at_rest() {
    const std::shared_ptr<const Theme> theme = theme_from_sheet(
        "ribbon::part(command-face) { background-color: #102030; border: none; }\n",
        "the raised-face sheet compiles cleanly");
    std::shared_ptr<Ribbon> ribbon = ribbon_with(two_tabs());
    (*ribbon).set_theme_override(theme);
    Window window(ribbon, {700.0, 115.0});
    window.perform_layout();
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 115.0}));
    require(painter.filled(raised_face), "an authored command face paints at rest");
    std::shared_ptr<Ribbon> stock = ribbon_with(two_tabs());
    Window plain(stock, {700.0, 115.0});
    plain.perform_layout();
    LookPainter flat;
    static_cast<void>(plain.paint(flat, {0.0, 0.0, 700.0, 115.0}));
    require(!flat.filled(raised_face), "without the rule the face stays the stock flat face");
}

// Defects 3, 5 and 6: the tab row's metrics, the selected tab's rise and
// join, authored widths and per-state label fonts.
void test_theme_metrics() {
    const std::shared_ptr<const Theme> theme = theme_from_sheet(
        "ribbon::part(tab-rail) { height: 27px; }\n"
        "ribbon::part(command-plane) { height: 116px; }\n"
        "ribbon::part(application-button) { min-width: 61px; }\n"
        "ribbon::part(tab) { gap: 3px; padding-inline: 10px; min-width: 40px; margin-top: 3px; }\n"
        "ribbon::part(tab):state(selected) { margin-top: 0; margin-bottom: -2px; }\n"
        "ribbon::part(tab-label):state(selected) { font-weight: bold; font-size: 1.5em; }\n",
        "the metrics sheet compiles cleanly");
    std::vector<RibbonTabSpec> tabs = two_tabs();
    tabs[1].width = 63.0;
    std::shared_ptr<Ribbon> ribbon = ribbon_with(std::move(tabs));
    (*ribbon).set_theme_override(theme);
    Window window(ribbon, {700.0, 200.0});
    window.perform_layout();
    require(near((*ribbon).measure({700.0, 400.0}).height, 27.0 + 116.0),
            "the rail's and the plane's heights come from the theme");
    const Ribbon::Parts parts = (*ribbon).parts();
    require(near(parts.application_button.bounds.width, 61.0) || parts.application_button.bounds.width > 61.0,
            "the File tab is at least the theme's minimum width");
    require(near(parts.command_plane.bounds.y, 27.0) && near(parts.command_plane.bounds.height, 116.0),
            "the plane starts under the themed rail");
    const std::vector<Ribbon::Tab> realized = (*ribbon).realized_tabs();
    require(realized.size() == 2U, "both tabs are realized");
    if (realized.size() != 2U) return;
    const Rect home = realized[0].parts.tab.bounds;
    const Rect view = realized[1].parts.tab.bounds;
    require(near(home.y, 0.0) && near(home.height, 29.0),
            "the selected tab stands to the rail's top and runs two units into the plane");
    require(near(view.y, 3.0) && near(view.height, 24.0), "a tab at rest sits three units below the rail's top");
    require(near(view.width, 63.0), "an authored tab width is kept");
    require(near(view.x - (home.x + home.width), 3.0), "the tabs keep the theme's gap");
    require(home.width >= 40.0, "a tab is at least the theme's minimum width");
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 200.0}));
    const std::optional<std::size_t> home_label = painter.text_index("Home");
    const std::optional<std::size_t> view_label = painter.text_index("View");
    require(home_label && view_label &&
                painter.text_fonts[*home_label].size > painter.text_fonts[*view_label].size &&
                painter.text_fonts[*home_label].weight > painter.text_fonts[*view_label].weight,
            "the selected tab's label takes its state's larger, bolder font");
    // Selecting the other tab moves neither tab sideways: widths hold the
    // wider of the two fonts.
    (*ribbon).selected_tab = std::size_t{1U};
    window.perform_layout();
    const std::vector<Ribbon::Tab> after = (*ribbon).realized_tabs();
    require(after.size() == 2U && near(after[0].parts.tab.bounds.x, home.x) &&
                near(after[0].parts.tab.bounds.width, home.width) && near(after[1].parts.tab.bounds.x, view.x) &&
                near(after[0].parts.tab.bounds.y, 3.0) && near(after[1].parts.tab.bounds.y, 0.0),
            "selection raises the other tab without moving either sideways");
}

// The owner's tab capabilities: the folder taper, the lip and the edge
// shadow paint where a theme turns them on. The lip stays a capability that
// neither the stock theme nor Plan Paint uses (the owner's folder
// correction, 2026-11-09).
void test_tab_shapes() {
    const std::shared_ptr<const Theme> theme = theme_from_sheet(
        "ribbon::part(tab) { taper: 4px #496c9f; }\n"
        "ribbon::part(tab):state(selected) { flare: 3px #e8f2ff; edge-shadow: 5px rgba(24, 56, 97, 0.30); }\n",
        "the shapes sheet compiles cleanly");
    std::shared_ptr<Ribbon> ribbon = ribbon_with(two_tabs());
    (*ribbon).set_theme_override(theme);
    Window window(ribbon, {700.0, 115.0});
    window.perform_layout();
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 115.0}));
    require(painter.lined(keyline), "a tapered tab draws its slanted edge in the taper colour");
    require(painter.filled(selected_face), "the selected tab's lip fills in the flare colour");
    require(painter.filled_rgb(shadow_ink) && painter.gradients > 0U,
            "the edge shadow falls off from its colour beyond the trailing edge");
}

// Defect 4: a disabled tab shows disabled and cannot be selected.
void test_disabled_tab() {
    std::vector<RibbonTabSpec> tabs = two_tabs();
    tabs[1].enabled = false;
    std::shared_ptr<Ribbon> ribbon = ribbon_with(std::move(tabs));
    Window window(ribbon, {700.0, 115.0});
    window.perform_layout();
    std::vector<Ribbon::Tab> realized = (*ribbon).realized_tabs();
    require(realized.size() == 2U && realized[1].states.disabled && !realized[0].states.disabled,
            "a disabled tab is realized in the disabled state");
    if (realized.size() != 2U) return;
    const Point view = centre(realized[1].parts.tab.bounds);
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, view}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, view}));
    require((*ribbon).selected_tab.read() == std::size_t{0U}, "a click on a disabled tab selects nothing");
    require(!(*ribbon).on_semantic_child_action("ribbon.tab.view", SemanticAction::select, {}) &&
                (*ribbon).selected_tab.read() == std::size_t{0U},
            "accessibility cannot select a disabled tab");
    bool refused = false;
    try {
        (*ribbon).selected_tab = std::size_t{1U};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*ribbon).selected_tab.read() == std::size_t{0U},
            "the application cannot select a disabled tab");
    const std::vector<SemanticNode> nodes = (*ribbon).semantic_virtual_children();
    require(nodes.size() >= 2U && nodes[1].children.size() == 2U &&
                !has_semantic_state(nodes[1].children[1].states, SemanticState::enabled) &&
                nodes[1].children[1].actions.empty() &&
                has_semantic_state(nodes[1].children[0].states, SemanticState::enabled),
            "a disabled tab is published neither enabled nor actionable");
    std::vector<RibbonTabSpec> first_disabled = two_tabs();
    first_disabled[0].enabled = false;
    std::shared_ptr<Ribbon> other = ribbon_with(std::move(first_disabled));
    require((*other).selected_tab.read() == std::size_t{1U}, "the selection falls back past a disabled first tab");
}

// Defect 7: selection on release, as an option; on the press by default.
void test_release_activation() {
    std::shared_ptr<Ribbon> ribbon = ribbon_with(two_tabs());
    Window window(ribbon, {700.0, 115.0});
    window.perform_layout();
    const std::vector<Ribbon::Tab> realized = (*ribbon).realized_tabs();
    require(realized.size() == 2U, "both tabs are realized");
    if (realized.size() != 2U) return;
    const Point home = centre(realized[0].parts.tab.bounds);
    const Point view = centre(realized[1].parts.tab.bounds);
    require(!(*ribbon).tabs_activate_on_release.read(), "tabs select on the press by default");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, view}));
    require((*ribbon).selected_tab.read() == std::size_t{1U}, "by default the press selects");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, view}));
    (*ribbon).tabs_activate_on_release = true;
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, home}));
    require((*ribbon).selected_tab.read() == std::size_t{1U}, "on release, the press alone selects nothing");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, home}));
    require((*ribbon).selected_tab.read() == std::size_t{0U}, "the release over the pressed tab selects it");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, view}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, home}));
    require((*ribbon).selected_tab.read() == std::size_t{0U} && !(*ribbon).minimized.read(),
            "a release over another tab selects nothing");
}

// An authored width outside (0, maximum_tab_width] is refused.
void test_width_refusals() {
    std::vector<RibbonTabSpec> zero = two_tabs();
    zero[0].width = 0.0;
    std::vector<RibbonTabSpec> wide = two_tabs();
    wide[0].width = Ribbon::maximum_tab_width + 1.0;
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    bool zero_refused = false;
    try {
        (*ribbon).set_tabs(std::move(zero));
    } catch (const std::invalid_argument&) {
        zero_refused = true;
    }
    bool wide_refused = false;
    try {
        (*ribbon).set_tabs(std::move(wide));
    } catch (const std::invalid_argument&) {
        wide_refused = true;
    }
    require(zero_refused && wide_refused, "a tab width outside (0, maximum_tab_width] is refused");
}

// Plan Paint's package (themes/plan-paint/plan-paint.theme.css at 5520a92,
// the tab row) with the look integration's capabilities turned on, as the
// ledger's snippet gives them: it compiles cleanly, and reproduces the
// reference's row: 27 units of rail, a 116-unit plane, tabs three units
// apart, the selected tab joining the plane.
void test_plan_paint_package() {
    const std::shared_ptr<const Theme> theme = theme_from_sheet(
        "ribbon::part(tab-rail) { background-color: #9bb5d8; height: 27px; }\n"
        "ribbon::part(tab) {\n"
        "  background-color: #9bb6da;\n"
        "  background-image: linear-gradient(to bottom, rgba(34, 70, 116, 0.15) 0%, rgba(0, 0, 0, 0) 72%);\n"
        "  border: 0 solid transparent;\n"
        "  border-top-left-radius: 4px;\n"
        "  border-top-right-radius: 4px;\n"
        "  border-bottom-left-radius: 0;\n"
        "  border-bottom-right-radius: 0;\n"
        "  box-shadow: 2px 0 0 #496c9f, 3px 1px 2px rgba(24, 56, 97, 0.22), inset 0 1px 0 rgba(255, 255, 255, 0.49);\n"
        "  gap: 3px;\n"
        "  margin-top: 3px;\n"
        "  taper: 4px #496c9f;\n"
        "  edge-shadow: 5px rgba(24, 56, 97, 0.3);\n"
        "}\n"
        "ribbon::part(tab):hover { background-color: #acc6e7; }\n"
        "ribbon::part(tab):state(selected) {\n"
        "  background-color: #e8f2ff;\n"
        "  background-image: linear-gradient(to bottom, rgba(255, 255, 255, 0.57) 0%, rgba(0, 0, 0, 0) 72%);\n"
        "  box-shadow: 2px 0 0 #496c9f, 3px 1px 2px rgba(24, 56, 97, 0.22), inset 0 1px 0 #ffffff;\n"
        "  margin-top: 0;\n"
        "  margin-bottom: -2px;\n"
        "}\n"
        "ribbon::part(tab):disabled { background-color: #b9c4d2; }\n"
        "ribbon::part(tab-label) { color: #153765; }\n"
        "ribbon::part(tab-label):state(selected) { color: #173f79; font-weight: bold; font-size: 1.08em; }\n"
        "ribbon::part(application-button) {\n"
        "  background-image: linear-gradient(to bottom, #528be0 0%, #173f88 100%);\n"
        "  border: 0 solid transparent;\n"
        "  border-radius: 0;\n"
        "  box-shadow: none;\n"
        "  min-width: 61px;\n"
        "}\n"
        "ribbon::part(command-plane) {\n"
        "  background-image: linear-gradient(to bottom, #e8f2ff 0%, #cadefb 28%, #a5c4f1 62%, #769fdf 100%);\n"
        "  border-top: 1px solid #abc0d8;\n"
        "  border-bottom: 1px solid #91acca;\n"
        "  border-left: 0 solid transparent;\n"
        "  border-right: 0 solid transparent;\n"
        "  border-radius: 0;\n"
        "  box-shadow: inset 0 1px 0 rgba(255, 255, 255, 0.88), inset 0 -1px 0 rgba(245, 251, 255, 0.82);\n"
        "  height: 116px;\n"
        "}\n",
        "Plan Paint's package compiles cleanly with the tab capabilities");
    std::vector<RibbonTabSpec> tabs = two_tabs();
    tabs[0].width = 63.0;
    tabs[1].width = 100.0;
    std::shared_ptr<Ribbon> ribbon = ribbon_with(std::move(tabs));
    (*ribbon).set_theme_override(theme);
    Window window(ribbon, {700.0, 200.0});
    window.perform_layout();
    const std::vector<Ribbon::Tab> realized = (*ribbon).realized_tabs();
    require(realized.size() == 2U && near(realized[0].parts.tab.bounds.width, 63.0) &&
                near(realized[1].parts.tab.bounds.width, 100.0) && near(realized[0].parts.tab.bounds.height, 29.0) &&
                near(realized[1].parts.tab.bounds.x - (realized[0].parts.tab.bounds.x + 63.0), 3.0),
            "Plan Paint's row: authored widths, three units apart, the selected tab joining the plane");
    require(near((*ribbon).measure({700.0, 400.0}).height, 27.0 + 116.0), "Plan Paint's rail and plane heights");
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 200.0}));
    require(painter.lined(keyline) && painter.gradients > 0U, "Plan Paint's tabs draw their folder edges and shadow");
}

// The default theme draws the rendering reference's tab row (§4): a
// 27-unit rail, the tabs three units down, the selected one at the top
// running one unit into the plane, its label in #173F79; every tab, the
// selected one too, tapers on its right in the keyline and casts a shadow
// onto the next (the owner's folder correction, 2026-11-09).
void test_default_theme_row() {
    std::shared_ptr<Ribbon> ribbon = ribbon_with(two_tabs());
    (*ribbon).set_theme_override(gui_forms_theme());
    Window window(ribbon, {700.0, 200.0});
    window.perform_layout();
    const std::vector<Ribbon::Tab> realized = (*ribbon).realized_tabs();
    require(realized.size() == 2U && near(realized[0].parts.tab.bounds.y, 0.0) &&
                near(realized[0].parts.tab.bounds.height, 28.0) && near(realized[1].parts.tab.bounds.y, 3.0) &&
                near(realized[1].parts.tab.bounds.height, 24.0),
            "the default theme raises the selected tab into the plane");
    require(near((*ribbon).parts().command_plane.bounds.y, 27.0), "the default theme's rail is 27 units");
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 200.0}));
    const std::optional<std::size_t> home = painter.text_index("Home");
    require(home && painter.text_colours[*home] == selected_ink, "the selected label is the reference's ink");
    require(painter.lines_in(keyline) >= realized.size(), "every tab draws its tapered folder edge");
    require(painter.filled_rgb(shadow_ink) && painter.gradients > 0U,
            "every tab's shadow falls off beyond its tapered edge");
}

// The folders stack (owner, 2026-11-09): in the default theme each tab
// overlaps the next by its five-unit taper, never File; the selected tab
// lies over both neighbours and takes the pointer there, and otherwise the
// left tab, on top, does. A deep negative gap stops at half the narrower
// tab, so every tab keeps a face to click.
void test_overlapping_tabs() {
    std::shared_ptr<Ribbon> ribbon = ribbon_with(two_tabs());
    (*ribbon).set_theme_override(gui_forms_theme());
    Window window(ribbon, {700.0, 200.0});
    window.perform_layout();
    const std::vector<Ribbon::Tab> realized = (*ribbon).realized_tabs();
    require(realized.size() == 2U, "both tabs are realized");
    if (realized.size() != 2U) return;
    const Rect file = (*ribbon).parts().application_button.bounds;
    const Rect home = realized[0].parts.tab.bounds;
    const Rect view = realized[1].parts.tab.bounds;
    require(home.x >= file.x + file.width, "File never overlaps the first tab");
    require(near(view.x, home.x + home.width - 5.0), "each tab overlaps the next by the five-unit taper");
    const Point shared{view.x + 2.0, view.y + view.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, centre(view)}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, centre(view)}));
    require((*ribbon).selected_tab.read() == std::size_t{1U}, "the second tab is selected");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, shared}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, shared}));
    require((*ribbon).selected_tab.read() == std::size_t{1U}, "the selected tab lies over its left neighbour");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, centre(home)}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, centre(home)}));
    window.perform_layout();
    const Rect raised = (*ribbon).realized_tabs()[1].parts.tab.bounds;
    const Point overlap{raised.x + 2.0, raised.y + raised.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, overlap}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, overlap}));
    require((*ribbon).selected_tab.read() == std::size_t{0U}, "otherwise the left tab is on top where they overlap");
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 200.0}));
    require(painter.filled_rgb(shadow_ink) && painter.gradients > 0U, "the upper tab casts its shadow onto the next");
    const std::shared_ptr<const Theme> deep = theme_from_sheet("ribbon::part(tab) { gap: -64px; }",
                                                               "a deep negative gap compiles cleanly");
    (*ribbon).set_theme_override(deep);
    window.perform_layout();
    const std::vector<Ribbon::Tab> stacked = (*ribbon).realized_tabs();
    require(stacked.size() == 2U, "both tabs are realized under the deep gap");
    if (stacked.size() != 2U) return;
    const Rect first = stacked[0].parts.tab.bounds;
    const Rect second = stacked[1].parts.tab.bounds;
    const double deepest = std::min(first.width, second.width) * 0.5;
    require(near(second.x, first.x + first.width - deepest), "an overlap stops at half the narrower tab");
}

// RB decision 26: in the default theme Backstage's column takes the
// command plane's blue against the white page.
void test_backstage_column() {
    std::shared_ptr<BackstageView> view = make_control<BackstageView>(StableId("backstage"));
    BackstageItemSpec topic{};
    topic.stable_id = "info";
    topic.kind = BackstageItemKind::topic;
    topic.text = "Info";
    topic.page = make_control<Panel>(StableId("backstage.info"));
    (*view).set_items({topic});
    (*view).set_theme_override(gui_forms_theme());
    Window window(view, {600.0, 400.0});
    window.perform_layout();
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 600.0, 400.0}));
    // The column's gradient runs from #E8F2FF to #CADEFB: its raster's
    // centre lies between the two.
    bool column_blue = false;
    for (const Color& fill : painter.fills) {
        const bool red = fill.red >= 0xcaU && fill.red <= 0xe8U;
        const bool green = fill.green >= 0xdeU && fill.green <= 0xf2U;
        const bool blue = fill.blue >= 0xfbU;
        if (red && green && blue && fill.alpha == 0xffU) column_blue = true;
    }
    require(column_blue, "the column takes the plane's blue");
    require(painter.filled(Color::rgba(0xff, 0xff, 0xff)), "the page is white");
}

// RB decision 25: a contextual tab keeps its set's tint when a theme rule's
// primitive draws its face (the default theme styles every tab).
void test_contextual_tint() {
    std::vector<RibbonTabSpec> tabs = two_tabs();
    RibbonTabSpec format{.stable_id = "format", .text = "Format"};
    format.tab_set = "picture";
    RibbonGroupSpec arrange{.stable_id = "arrange", .caption = "Arrange"};
    arrange.commands.push_back({.stable_id = "front", .command = std::make_shared<Command>("front", "Front")});
    format.groups.push_back(std::move(arrange));
    tabs.push_back(std::move(format));
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(std::move(tabs), {{"picture", "Picture Tools"}});
    (*ribbon).set_theme_override(gui_forms_theme());
    (*ribbon).set_tab_set_availability("picture", RibbonTabSetAvailability::available);
    Window window(ribbon, {700.0, 160.0});
    window.perform_layout();
    const std::optional<AccentIndex> accent = (*ribbon).tab_set_accent("picture");
    require(accent.has_value(), "the shown set has an accent");
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 160.0}));
    require(accent && painter.filled((*gui_forms_theme()).accent(*accent)),
            "the contextual tab's themed face carries its set's tint");
}

// The derived dark variant turns a rule's dark ink to the dark ink: the
// selected tab's #173F79 label is light on the dark tab, while the File
// tab's white label stays white.
void test_dark_tab_ink() {
    std::shared_ptr<Ribbon> ribbon = ribbon_with(two_tabs());
    (*ribbon).set_theme_override(gui_forms_theme());
    Window window(ribbon, {700.0, 160.0});
    PresentationOverrides dark{};
    dark.colour_scheme = ColourScheme::dark;
    window.set_presentation_overrides(dark);
    window.perform_layout();
    LookPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 700.0, 160.0}));
    const std::optional<std::size_t> home = painter.text_index("Home");
    const std::optional<std::size_t> file = painter.text_index("File");
    require(home.has_value() && file.has_value(), "the dark ribbon paints its labels");
    if (!home || !file) return;
    const Color ink = painter.text_colours[*home];
    const int ink_sum = static_cast<int>(ink.red) + static_cast<int>(ink.green) + static_cast<int>(ink.blue);
    require(ink_sum > 3 * 128, "the selected tab's ink is light in dark");
    require(painter.text_colours[*file] == Color::rgba(0xff, 0xff, 0xff), "the File tab's white label stays white");
    require(!painter.lined(keyline) && !painter.lines.empty() && painter.gradients > 0U,
            "in dark the folder edge's keyline darkens with the face, and the shadow still falls");
}

} // namespace

int main() {
    test_parts_are_keyed();
    test_authored_face_at_rest();
    test_theme_metrics();
    test_tab_shapes();
    test_disabled_tab();
    test_release_activation();
    test_width_refusals();
    test_plan_paint_package();
    test_contextual_tint();
    test_dark_tab_ink();
    test_default_theme_row();
    test_overlapping_tabs();
    test_backstage_column();
    if (failures != 0) {
        std::cerr << failures << " ribbon look check(s) failed\n";
        return 1;
    }
    std::cout << "Ribbon look: keyed parts, authored faces at rest, theme metrics, shapes, disabled tabs, release "
                 "activation, width refusals, Plan Paint's package and the default row passed\n";
    return 0;
}
