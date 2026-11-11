// A project theme styles a form's ontology (owner rulings of 2026-11-04,
// G3): a Label's text and a Panel's or ScaledGroupBox's face take the
// colours of the theme rules that select them by class or stable ID, and
// the controls report those colours (Label::foreground, Panel::background)
// as they paint them. The `classes` property sets the classes a form
// document writes.

#include "../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
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

int failures = 0;

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

// Records the colour of every solid fill and every text run, in paint order.
class ColourPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color color) override { fills.push_back(color); }
    void fill_rounded_rect(Rect, double, Color color) override { fills.push_back(color); }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color color) override {
        texts.emplace_back(text);
        text_colours.push_back(color);
    }
    void draw_image(ImageId, Rect, double) override {}
    // A face a rule's primitive draws arrives as an engine part raster
    // (premultiplied BGRA8): its centre pixel stands for the fill. The faces
    // here are opaque, so premultiplied and straight colour agree.
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
    [[nodiscard]] std::optional<Color> text_colour(std::string_view text) const {
        for (std::size_t index = 0U; index < texts.size(); ++index) {
            if (texts[index] == text) return text_colours[index];
        }
        return std::nullopt;
    }

    std::vector<Color> fills{};
    std::vector<std::string> texts{};
    std::vector<Color> text_colours{};
};

// The package a program would ship: its manifest names the one sheet.
ThemePackage faces_package(std::string_view sheet) {
    const std::string hash = theme_language::package_sha256_hex(theme_language::package_sha256(sheet));
    ThemePackage made{};
    made.manifest = "{\"format\": \"gui-forms-theme/1\", \"name\": \"faces\", \"version\": \"1.0.0\", "
                    "\"engine\": \">=0.3\", \"sheet\": {\"path\": \"faces.theme.css\", \"sha256\": \"" +
                    hash + "\"}}";
    made.files.push_back(ThemePackageFile{"faces.theme.css", std::string(sheet)});
    return made;
}

std::shared_ptr<const Theme> theme_from_sheet(std::string_view text) {
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    // Onto windows-professional's recipes, which the face expectations
    // read (the default base is gui-forms, T4 phase 6).
    options.base = ThemeBase::windows_professional;
    const ThemeCompileResult result = compile_theme_package(faces_package(text), library, images, options);
    require(result.succeeded() && result.diagnostics().empty(), "the faces sheet compiles cleanly");
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        std::cerr << "  " << diagnostic.code << " " << diagnostic.name << '\n';
    }
    std::shared_ptr<const Theme> theme = result.theme();
    return theme;
}

constexpr Color heading_red = Color::rgba(0xc0, 0x10, 0x20);
constexpr Color paper_green = Color::rgba(0x20, 0xa0, 0x40);
constexpr Color group_blue = Color::rgba(0x20, 0x40, 0xc0);

void test_classes_property() {
    const std::shared_ptr<Label> label = make_control<Label>(StableId("probe.label"), "Probe");
    const BindingValue empty = (*label).property_value("classes").value_or(BindingValue{});
    const PropertyCollectionValue* none = std::get_if<PropertyCollectionValue>(&empty);
    require(none != nullptr && property_collection_items(*none).empty(), "classes default to none");
    std::vector<BindingValue> names{};
    names.emplace_back(std::string("page-heading"));
    names.emplace_back(std::string("loud"));
    names.emplace_back(std::string("page-heading"));
    (*label).set_property_value("classes", BindingValue{make_property_collection("String", BindingValueKind::text, names)});
    require((*label).classes.read().size() == 2U && (*label).has_class("page-heading") && (*label).has_class("loud"),
            "the classes property sets the classes, dropping a repeated name");
    const BindingValue read = (*label).property_value("classes").value_or(BindingValue{});
    const PropertyCollectionValue* held = std::get_if<PropertyCollectionValue>(&read);
    require(held != nullptr && property_collection_items(*held).size() == 2U, "the classes property reads them back");
    std::vector<BindingValue> invalid{};
    invalid.emplace_back(std::string("Not A Class"));
    bool refused = false;
    try {
        (*label).set_property_value("classes",
                              BindingValue{make_property_collection("String", BindingValueKind::text, invalid)});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*label).classes.read().size() == 2U, "an invalid class name is refused and changes nothing");
}

void test_rule_faces() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("probe.root"));
    const std::shared_ptr<Label> heading = make_control<Label>(StableId("probe.heading"), "Heading");
    (*heading).add_class("page-heading");
    (*heading).set_requested_bounds({10.0, 10.0, 200.0, 24.0});
    (*root).add_child(heading);
    const std::shared_ptr<Label> plain = make_control<Label>(StableId("probe.plain"), "Plain");
    (*plain).set_requested_bounds({10.0, 40.0, 200.0, 24.0});
    (*root).add_child(plain);
    const std::shared_ptr<Panel> paper = make_control<Panel>(StableId("probe.paper"));
    (*paper).add_class("paper");
    (*paper).set_requested_bounds({10.0, 70.0, 120.0, 60.0});
    (*root).add_child(paper);
    const std::shared_ptr<ScaledGroupBox> group =
        make_control<ScaledGroupBox>(StableId("probe.group"), "Group", Size{120.0, 60.0});
    (*group).set_requested_bounds({140.0, 70.0, 120.0, 60.0});
    (*root).add_child(group);

    Window window(root, Size{300.0, 160.0});
    // The floor: the same base with no rules of the package's own.
    window.set_theme(theme_from_sheet(""));
    window.perform_layout();
    const Color floor_text = (*plain).foreground();
    const Color floor_face = (*paper).background();
    // The text colour is a role, so forced high contrast can replace it by
    // meaning (a literal text colour warns, W201). The faces use the
    // `background` shorthand: as in CSS, background-color alone lies under
    // the recipe's gradient layers, which still cover it.
    window.set_theme(theme_from_sheet(":root { ink-on-paper: #c01020 on #ffffff; }\n"
                                      ".page-heading { color: role(ink-on-paper); }\n"
                                      ".paper::part(face) { background: #20a040; }\n"
                                      "#probe\\.group::part(face) { background: #2040c0; }\n"));
    window.perform_layout();
    ColourPainter painter{};
    window.paint(painter);
    const std::optional<Color> heading_painted = painter.text_colour("Heading");
    require(heading_painted && *heading_painted == heading_red, "the class rule's colour paints the heading's text");
    require((*heading).foreground() == heading_red, "Label::foreground reports the class rule's colour");
    require((*plain).foreground() == floor_text, "a label no rule selects keeps the theme's text colour");
    require(painter.filled(paper_green), "the class rule's background paints the panel's face");
    require((*paper).background() == paper_green, "Panel::background reports the class rule's background");
    require(floor_face != paper_green, "the floor face differs from the rule's");
    require(painter.filled(group_blue), "a stable-ID rule's background paints the group's face");
    require((*group).background() == group_blue, "the group reports the stable-ID rule's background");
    // An application's own colour in code still stands over the theme
    // (truly inline code, the owner's last layer).
    (*heading).fore_color = Color::rgba(1, 2, 3);
    require((*heading).foreground() == Color::rgba(1, 2, 3), "code's own foreground stands over the rule");
}

// The package's compile result, diagnostics and all.
ThemeCompileResult compiled_sheet(std::string_view text) {
    ImageRegistry images{};
    const ThemeLibrary library{};
    ThemeCompileOptions options{};
    options.controls = stock_control_anatomies();
    ThemeCompileResult result = compile_theme_package(faces_package(text), library, images, options);
    return result;
}

// How many of `result`'s diagnostics are `code` with a detail containing
// `detail`.
std::size_t count_diagnostics(const ThemeCompileResult& result, std::string_view code, std::string_view detail) {
    std::size_t count = 0U;
    for (const ThemeCompileDiagnostic& diagnostic : result.diagnostics()) {
        if (diagnostic.code == code && diagnostic.detail.find(detail) != std::string::npos) ++count;
    }
    return count;
}

constexpr Color well_purple = Color::rgba(0xa0, 0x10, 0x80);
constexpr Color ground_amber = Color::rgba(0xd0, 0x90, 0x10);
constexpr Color family_teal = Color::rgba(0x10, 0x70, 0x70);

// A themed TextBox's field is its well: `text-box::part(well)` paints its
// ground, as `:family(editor)` does (the well is keyed by its part's name,
// so a rule naming the part reaches it). Its face, edge and content are
// left alone by its themed look, so a rule styling them warns W112 with the
// part to style instead, and the rule is still kept.
void test_text_box_parts() {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("probe.root"));
    const std::shared_ptr<TextBox> field = make_control<TextBox>(StableId("probe.field"), "Field");
    (*field).set_requested_bounds({10.0, 10.0, 160.0, 24.0});
    (*root).add_child(field);
    Window window(root, Size{200.0, 60.0});
    window.set_theme(theme_from_sheet("text-box::part(well) { background: #a01080; }\n"));
    window.perform_layout();
    ColourPainter painter{};
    window.paint(painter);
    require(painter.filled(well_purple), "text-box::part(well) paints the field's ground");

    const ThemeCompileResult face = compiled_sheet(":root { ink-on-paper: #c01020 on #ffffff; }\n"
                                                   "text-box::part(face) { background: #a01080; }\n"
                                                   "text-box::part(content) { color: role(ink-on-paper); }\n"
                                                   "text-box::part(well) { background: #a01080; }\n");
    require(face.succeeded(), "a sheet styling a text box's unthemed parts still compiles");
    require(count_diagnostics(face, "W112", "text-box::part(well)") == 2U,
            "the face's background and the content's colour each warn W112, naming the well");
    require(face.diagnostics().size() == 2U, "the well's own rule warns nothing");
}

// The four containers side by side in a fresh window under `sheet`: whether
// painting it fills `colour`.
bool containers_fill(std::string_view sheet, Color colour) {
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("probe.root"));
    const std::shared_ptr<UserControl> user = make_control<UserControl>(StableId("probe.user"));
    (*user).set_requested_bounds({10.0, 10.0, 60.0, 40.0});
    (*root).add_child(user);
    const std::shared_ptr<ContainerControl> container = make_control<ContainerControl>(StableId("probe.container"));
    (*container).set_requested_bounds({80.0, 10.0, 60.0, 40.0});
    (*root).add_child(container);
    const std::shared_ptr<ScrollableControl> scrollable =
        make_control<ScrollableControl>(StableId("probe.scrollable"));
    (*scrollable).set_requested_bounds({150.0, 10.0, 60.0, 40.0});
    (*root).add_child(scrollable);
    const std::shared_ptr<SplitContainer> split = make_control<SplitContainer>(StableId("probe.split"));
    (*split).set_requested_bounds({220.0, 10.0, 60.0, 40.0});
    (*root).add_child(split);
    Window window(root, Size{300.0, 60.0});
    window.set_theme(theme_from_sheet(sheet));
    window.perform_layout();
    ColourPainter painter{};
    window.paint(painter);
    const bool filled = painter.filled(colour);
    return filled;
}

// The containers have a ground, `::part(face)` over the whole box, painted
// only when a rule names it: a rule on the panel family alone paints none,
// as before the part was declared.
void test_container_grounds() {
    const std::array<std::string_view, 4> types{{"user-control", "container-control", "scrollable-control",
                                                 "split-container"}};
    for (const std::string_view type : types) {
        const std::string sheet = std::string(type) + "::part(face) { background: #d09010; }\n";
        require(containers_fill(sheet, ground_amber), std::string(type) + "::part(face) paints the container's ground");
    }
    const std::shared_ptr<UserControl> alone = make_control<UserControl>(StableId("probe.alone"));
    Window bare(alone, Size{100.0, 60.0});
    bare.set_theme(theme_from_sheet(":family(panel) { background: #107070; }\n"));
    bare.perform_layout();
    ColourPainter unnamed{};
    bare.paint(unnamed);
    require(!unnamed.filled(family_teal), "a panel-family rule alone paints no container ground");
}

// A part the selector's type lacks leaves out only its own selector, as
// CSS's `::part()` matches nothing for an unknown part: the rest of the
// list still styles its controls.
void test_unknown_part_in_a_list() {
    const ThemeCompileResult result =
        compiled_sheet("user-control::part(lid), panel::part(face) { background: #d09010; }\n");
    require(result.succeeded(), "the list compiles");
    require(count_diagnostics(result, "W106", "lid") == 1U && result.diagnostics().size() == 1U,
            "the unknown part warns once (W106)");
    const std::shared_ptr<Panel> root = make_control<Panel>(StableId("probe.root"));
    Window window(root, Size{100.0, 60.0});
    window.set_theme(result.theme());
    window.perform_layout();
    ColourPainter painter{};
    window.paint(painter);
    require(painter.filled(ground_amber), "the list's other selector still styles the panel's face");
}

// Makes each docked panel's content: an empty panel.
class EmptyPanels final {
public:
    void create(DockPanelCreation& creation) {
        creation.content = make_control<Panel>(StableId("content." + creation.panel.value));
    }
};

constexpr Color caption_blue = Color::rgba(0x30, 0x60, 0xa0);

// A dock site's caption parts carry their anatomy names, so a theme's
// `dock-site::part(...)` rules reach them, as they reach any other
// control's named parts.
void test_dock_site_parts() {
    EmptyPanels panels{};
    DockWorkspace workspace("faces.workspace");
    const std::array<DockSide, 2> sides{{DockSide::left, DockSide::right}};
    const std::array<std::string_view, 2> names{{"left", "right"}};
    for (std::size_t index = 0U; index < sides.size(); ++index) {
        DockPanelRegistration registration{};
        registration.descriptor.id = DockPanelId(std::string(names[index]));
        registration.descriptor.kind = DockPanelKind::tool;
        registration.descriptor.minimum_size = {80.0, 60.0};
        registration.descriptor.default_placement.side = sides[index];
        registration.descriptor.default_placement.ratio = 0.3;
        registration.title = std::string(names[index]);
        registration.factory = Delegate<DockPanelCreation&>::bind<EmptyPanels, &EmptyPanels::create>(panels);
        require(workspace.register_panel(registration), "a tool panel registers");
        require(workspace.show_panel(DockPanelId(std::string(names[index]))).applied, "the tool panel shows");
    }
    const std::shared_ptr<DockSite> site = make_control<DockSite>(StableId("faces.site"), workspace);
    Window window(site, Size{600.0, 400.0});
    window.set_theme(theme_from_sheet("dock-site::part(caption) { background: #3060a0; }\n"));
    window.perform_layout();
    require((*site).realized_stacks().size() == 2U, "the two tool stacks are realized");
    ColourPainter painter{};
    window.paint(painter);
    require(painter.filled(caption_blue), "dock-site::part(caption) paints the tool stacks' captions");
}

constexpr Color thumb_amber = Color::rgba(0xd0, 0x90, 0x10);

// Whether a scrolled panel's own vertical bar paints its thumb in amber
// under `v-scroll-bar::part(thumb)`, with or without a back colour the
// program set on the panel (an authored colour, as SDR#'s panels set).
bool embedded_thumb_amber(bool authored_back) {
    const std::shared_ptr<Panel> panel = make_control<Panel>(StableId("probe.scrolled"));
    const std::shared_ptr<Button> child = make_control<Button>(StableId("probe.scrolled.child"), "Content");
    (*child).set_requested_bounds({10.0, 10.0, 60.0, 300.0});
    (*panel).add_child(child);
    (*panel).auto_scroll = true;
    if (authored_back) (*panel).back_color = Color::rgba(0x19, 0x19, 0x19);
    Window window(panel, Size{120.0, 100.0});
    window.set_theme(theme_from_sheet("v-scroll-bar::part(thumb) { background: #d09010 !important; }\n"));
    window.perform_layout();
    ColourPainter painter{};
    window.paint(painter);
    const bool filled = painter.filled(thumb_amber);
    return filled;
}

// A scrollable control's built-in bars are styled as the stock bars
// (PartPaint::control_type), so a sheet's v-scroll-bar rule reaches them.
void test_embedded_scroll_bar_rules() {
    require(embedded_thumb_amber(false), "v-scroll-bar::part(thumb) paints a panel's own thumb");
    require(embedded_thumb_amber(true), "an important thumb rule stands over the panel's own back colour");
}

} // namespace

int main() {
    test_classes_property();
    test_rule_faces();
    test_text_box_parts();
    test_container_grounds();
    test_unknown_part_in_a_list();
    test_dock_site_parts();
    test_embedded_scroll_bar_rules();
    if (failures != 0) {
        std::cerr << failures << " theme rule face check(s) failed\n";
        return 1;
    }
    return 0;
}
