// Ribbon phase 0 vocabulary (owner decisions RB-20 and RB-22): the dialog
// launcher, "more", overflow and customize marks; the accent index value and
// its provisional ramp; the split_button and menu_button roles; and the
// position-in-set, set-size and has-popup node fields, through to semantic
// snapshots.

#include "gui_forms/gui_forms.hpp"
#include "support/theme_definition_heap.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

struct Operation final {
    std::string kind;
    Rect bounds{};
    Color color{};
};

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect rect, Color color) override { operations.push_back({"fill", rect, color}); }
    void fill_rounded_rect(Rect rect, double, Color color) override {
        operations.push_back({"fill_rounded", rect, color});
    }
    void stroke_rect(Rect rect, Color color, double) override { operations.push_back({"stroke", rect, color}); }
    void draw_line(Point from, Point to, Color color, double) override {
        const Rect extent{std::min(from.x, to.x), std::min(from.y, to.y), std::abs(to.x - from.x),
                          std::abs(to.y - from.y)};
        operations.push_back({"line", extent, color});
    }
    void draw_text_utf8(Point, std::string_view, FontSpec, Color color) override {
        operations.push_back({"text", {}, color});
    }
    void draw_image(ImageId, Rect destination, double) override { operations.push_back({"image", destination, {}}); }

    std::vector<Operation> operations;
};

bool snake_case(std::string_view name) {
    if (name.empty() || name.front() == '_' || name.back() == '_') return false;
    for (const char character : name) {
        const bool lower = character >= 'a' && character <= 'z';
        const bool digit = character >= '0' && character <= '9';
        if (!lower && !digit && character != '_') return false;
    }
    return true;
}

void marks() {
    std::set<std::string> names;
    for (std::size_t index = 0; index < part_mark_count; ++index) {
        const std::string_view name = part_mark_name(static_cast<PartMark>(index));
        require(snake_case(name) && names.insert(std::string(name)).second, "every mark has a unique snake_case name");
    }
    require(part_mark_name(PartMark::dialog_launcher) == "dialog_launcher" && part_mark_name(PartMark::more) == "more" &&
                part_mark_name(PartMark::overflow) == "overflow" && part_mark_name(PartMark::customize) == "customize",
            "the four ribbon marks are admitted under their decided names");

    std::shared_ptr<Control> control = make_control<Control>(StableId("marks"));
    const PartMark ribbon_marks[] = {PartMark::dialog_launcher, PartMark::more, PartMark::overflow,
                                     PartMark::customize};
    std::vector<std::size_t> operation_counts;
    for (const PartMark mark : ribbon_marks) {
        PartPaint glyph;
        glyph.role = PartRole::glyph;
        glyph.bounds = {3.0, 5.0, 16.0, 12.0};
        glyph.mark = mark;
        RecordingPainter painter;
        paint_part(painter, *control, glyph);
        const ControlVisualRecipe& recipe =
            (*control).effective_theme().resolve(glyph.family, part_visual_context(*control, glyph.states));
        require(!painter.operations.empty(), "a ribbon mark draws something");
        for (const Operation& operation : painter.operations) {
            require(operation.color == recipe.glyph, "a ribbon mark draws in the recipe's glyph colour");
            require(operation.bounds.x >= glyph.bounds.x - 0.001 &&
                        operation.bounds.right() <= glyph.bounds.right() + 0.001 &&
                        operation.bounds.y >= glyph.bounds.y - 0.001 &&
                        operation.bounds.bottom() <= glyph.bounds.bottom() + 0.001,
                    "a ribbon mark stays inside its part");
        }
        operation_counts.push_back(painter.operations.size());
    }
    require(operation_counts[2] == 4U, "the overflow mark is two chevrons of two strokes each");
    PartPaint tiny;
    tiny.role = PartRole::glyph;
    tiny.bounds = {0.0, 0.0, 3.0, 3.0};
    tiny.mark = PartMark::more;
    RecordingPainter nothing;
    paint_part(nothing, *control, tiny);
    require(nothing.operations.empty(), "a mark too small to read draws nothing");
}

void accents() {
    std::shared_ptr<Control> control = make_control<Control>(StableId("accents"));
    const Theme& theme = (*control).effective_theme();
    const std::size_t ramp = theme.definition().accent_ramp.size();
    require(ramp == 6U, "the provisional ramp has six colours");
    require(theme.accent(AccentIndex{static_cast<std::uint8_t>(ramp)}) == theme.accent(AccentIndex{0U}),
            "an index past the ramp wraps around it");
    require(!(theme.accent(AccentIndex{1U}) == theme.accent(AccentIndex{0U})), "ramp colours differ");

    PartPaint face;
    face.role = PartRole::face;
    face.bounds = {0.0, 0.0, 60.0, 24.0};
    RecordingPainter plain;
    paint_part(plain, *control, face);
    face.accent = AccentIndex{2U};
    RecordingPainter tinted;
    paint_part(tinted, *control, face);
    require(tinted.operations.size() == plain.operations.size() + 1U &&
                tinted.operations.back().kind == "fill_rounded" &&
                tinted.operations.back().color == theme.accent(AccentIndex{2U}) &&
                tinted.operations.back().bounds == face.bounds,
            "an accented surface is tinted from the ramp over its material");
    PartPaint label;
    label.role = PartRole::label;
    label.bounds = {0.0, 0.0, 60.0, 24.0};
    label.text = "Format";
    label.accent = AccentIndex{2U};
    RecordingPainter text;
    paint_part(text, *control, label);
    for (const Operation& operation : text.operations) {
        require(operation.kind != "fill_rounded", "a text role ignores its accent");
    }

    PartGeometry first = PartGeometry::from_bounds({0.0, 0.0, 10.0, 10.0});
    PartGeometry second = first;
    second.accent = AccentIndex{1U};
    require(!(first == second), "published geometry compares its accent");

    const std::unique_ptr<ThemeDefinition> held_empty = test_support::professional_definition();

    ThemeDefinition& empty = *held_empty;
    empty.accent_ramp.clear();
    bool threw = false;
    try {
        static_cast<void>(Theme::create(empty));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "a theme without an accent ramp is refused");
}

void roles_and_fields() {
    std::set<std::string> names;
    for (std::size_t index = 0; index < semantic_role_count; ++index) {
        const std::string name = semantic_role_name(static_cast<SemanticRole>(index));
        require(snake_case(name) && names.insert(name).second, "every semantic role has a unique name");
    }
    require(std::string(semantic_role_name(SemanticRole::split_button)) == "split_button" &&
                std::string(semantic_role_name(SemanticRole::menu_button)) == "menu_button",
            "split_button and menu_button are admitted");

    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<DropDownButton> menu =
        make_control<DropDownButton>(StableId("root.menu"), "Paste", DropDownButtonMode::menu);
    std::shared_ptr<DropDownButton> split =
        make_control<DropDownButton>(StableId("root.split"), "Save", DropDownButtonMode::split);
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("root.list"));
    (*list).set_items({"Red", "Green", "Blue"});
    (*menu).set_bounds({0.0, 0.0, 100.0, 28.0});
    (*split).set_bounds({110.0, 0.0, 100.0, 28.0});
    (*list).set_bounds({0.0, 40.0, 200.0, 100.0});
    (*root).add_child(menu);
    (*root).add_child(split);
    (*root).add_child(list);
    Window window(root, {300.0, 160.0});
    window.perform_layout();
    const SemanticDescriptor menu_semantics = (*menu).semantic_descriptor();
    const SemanticDescriptor split_semantics = (*split).semantic_descriptor();
    require(menu_semantics.role == SemanticRole::menu_button && menu_semantics.has_popup,
            "a menu-mode DropDownButton is a menu button with an overlay");
    require(split_semantics.role == SemanticRole::split_button && split_semantics.has_popup,
            "a split-mode DropDownButton is a split button with an overlay");
    const std::vector<SemanticNode> rows = (*list).semantic_virtual_children();
    require(rows.size() == 3U && rows[1].position_in_set == std::size_t{2U} && rows[1].set_size == std::size_t{3U},
            "list items publish their position in the set and the set's size");

    const std::string json = window.semantic_snapshot().to_json();
    require(json.find("\"role\":\"split_button\"") != std::string::npos &&
                json.find("\"role\":\"menu_button\"") != std::string::npos &&
                json.find("\"has_popup\":true") != std::string::npos &&
                json.find("\"position_in_set\":2,\"set_size\":3") != std::string::npos,
            "semantic snapshots carry the new roles and fields");
}

void tool_strip_roles() {
    const std::shared_ptr<Command> choice = std::make_shared<Command>("choice", "Choice");
    std::shared_ptr<ToolStrip> strip = make_control<ToolStrip>(StableId("strip"));
    std::vector<ToolStripItemSpec> items;
    items.push_back({.stable_id = "bold", .text = "Bold"});
    items.push_back({.stable_id = "export", .kind = ToolStripItemKind::drop_down_button, .text = "Export",
                     .drop_down = {MenuItemSpec("export.pdf", MenuItemKind::command, choice, "PDF")}});
    items.push_back({.stable_id = "run", .kind = ToolStripItemKind::split_button, .text = "Run",
                     .drop_down = {MenuItemSpec("run.watch", MenuItemKind::command, choice, "Watch")}});
    (*strip).set_items(std::move(items));
    Window window(strip, {400.0, 25.0});
    window.perform_layout();
    const std::vector<SemanticNode> children = (*strip).semantic_virtual_children();
    require(children.size() == 3U && children[0].role == SemanticRole::button && !children[0].has_popup &&
                children[1].role == SemanticRole::menu_button && children[1].has_popup &&
                children[2].role == SemanticRole::split_button && children[2].has_popup,
            "a strip's drop-down and split buttons are menu and split buttons with overlays");

    window.resize({120.0, 25.0});
    window.perform_layout();
    const ToolStrip::Parts parts = (*strip).parts();
    require(parts.overflow_glyph.present, "a narrow strip shows its overflow button");
    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 120.0, 25.0}));
    std::size_t strokes = 0U;
    for (const Operation& operation : painter.operations) {
        const Rect glyph = parts.overflow_glyph.bounds;
        const bool inside = operation.bounds.x >= glyph.x - 0.001 && operation.bounds.right() <= glyph.right() + 0.001 &&
                            operation.bounds.y >= glyph.y - 0.001 && operation.bounds.bottom() <= glyph.bottom() + 0.001;
        if (operation.kind == "line" && inside) ++strokes;
    }
    require(strokes == 4U, "the overflow button shows the overflow mark, a double chevron");
}

} // namespace

int main() {
    marks();
    accents();
    roles_and_fields();
    tool_strip_roles();
    std::cout << "Ribbon vocabulary: marks, accent ramp, split and menu buttons, set positions and overlays passed\n";
    return 0;
}
