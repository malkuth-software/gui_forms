// A project theme styles a form's ontology (owner rulings of 2026-11-04,
// G3): a Label's text and a Panel's or ScaledGroupBox's face take the
// colours of the theme rules that select them by class or stable ID, and
// the controls report those colours (Label::foreground, Panel::background)
// as they paint them. The `classes` property sets the classes a form
// document writes.

#include "../src/core/theme_language/package/theme_manifest.hpp"
#include "gui_forms/gui_forms.hpp"

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
    require((*label).classes().size() == 2U && (*label).has_class("page-heading") && (*label).has_class("loud"),
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
    require(refused && (*label).classes().size() == 2U, "an invalid class name is refused and changes nothing");
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

} // namespace

int main() {
    test_classes_property();
    test_rule_faces();
    if (failures != 0) {
        std::cerr << failures << " theme rule face check(s) failed\n";
        return 1;
    }
    return 0;
}
