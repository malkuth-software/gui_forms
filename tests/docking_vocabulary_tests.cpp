// Docking and PageView vocabulary, phase V0 (owner decisions DK-35 and
// DK-40): the pin and dock-shape marks; text orientation in PartPaint,
// through the recording painter and replay; the admitted custom states
// `modified` and `pinned`; the pane and tab_panel roles, the close action,
// the tab-to-panel relation and the tab-list orientation, through to
// semantic snapshots; and tabs that no longer publish press.

#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <set>
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
    Point origin{};
    TextOrientation orientation{TextOrientation::horizontal};
    std::string text;
};

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect rect, Color color) override { operations.push_back({"fill", rect, color}); }
    void stroke_rect(Rect rect, Color color, double) override { operations.push_back({"stroke", rect, color}); }
    void draw_line(Point from, Point to, Color color, double) override {
        const Rect extent{std::min(from.x, to.x), std::min(from.y, to.y), std::abs(to.x - from.x),
                          std::abs(to.y - from.y)};
        operations.push_back({"line", extent, color});
    }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec, Color color) override {
        Operation operation{"text", {}, color, origin};
        operation.text = std::string(text);
        operations.push_back(operation);
    }
    void draw_turned_text_utf8(Point origin, std::string_view text, FontSpec, Color color,
                               TextOrientation orientation) override {
        Operation operation{"turned_text", {}, color, origin, orientation};
        operation.text = std::string(text);
        operations.push_back(operation);
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

bool inside(Rect inner, Rect outer) {
    const bool result = inner.x >= outer.x - 0.001 && inner.right() <= outer.right() + 0.001 &&
                        inner.y >= outer.y - 0.001 && inner.bottom() <= outer.bottom() + 0.001;
    return result;
}

RecordingPainter paint_glyph(const Control& control, PartMark mark, Rect bounds, AnatomyStateSet states,
                             DockShapeRegion region) {
    PartPaint glyph;
    glyph.role = PartRole::glyph;
    glyph.bounds = bounds;
    glyph.mark = mark;
    glyph.states = states;
    glyph.dock_region = region;
    RecordingPainter painter;
    paint_part(painter, control, glyph);
    return painter;
}

void marks() {
    std::set<std::string> names;
    for (std::size_t index = 0; index < part_mark_count; ++index) {
        const std::string_view name = part_mark_name(static_cast<PartMark>(index));
        require(snake_case(name) && names.insert(std::string(name)).second, "every mark has a unique snake_case name");
    }
    require(part_mark_name(PartMark::pin) == "pin" && part_mark_name(PartMark::dock_shape) == "dock_shape",
            "the pin and dock-shape marks are admitted under their decided names");

    std::shared_ptr<Control> control = make_control<Control>(StableId("marks"));
    const Rect box{0.0, 0.0, 16.0, 16.0};
    const ControlVisualRecipe& recipe =
        (*control).effective_theme().resolve(ControlVisualRole::panel, part_visual_context(*control, {}));

    AnatomyStateSet checked;
    checked.insert(AnatomyState::checked);
    const RecordingPainter pinned = paint_glyph(*control, PartMark::pin, box, checked, DockShapeRegion::tab_into);
    const RecordingPainter unpinned = paint_glyph(*control, PartMark::pin, box, {}, DockShapeRegion::tab_into);
    require(pinned.operations.size() == 5U && unpinned.operations.size() == 5U,
            "the pin is a barrel of three strokes, a collar and a needle");
    bool upright_needle = false;
    for (const Operation& operation : pinned.operations) {
        require(operation.color == recipe.glyph && inside(operation.bounds, box),
                "the pin draws inside its part in the glyph colour");
        if (operation.bounds.width == 0.0 && operation.bounds.y >= 8.0 && operation.bounds.height >= 5.0) {
            upright_needle = true;
        }
    }
    bool lying_needle = false;
    for (const Operation& operation : unpinned.operations) {
        require(operation.color == recipe.glyph && inside(operation.bounds, box),
                "the unpinned pin draws inside its part in the glyph colour");
        if (operation.bounds.height == 0.0 && operation.bounds.x < 8.0 && operation.bounds.width >= 5.0) {
            lying_needle = true;
        }
    }
    require(upright_needle, "a checked (pinned) pin stands upright, needle down");
    require(lying_needle, "an unchecked (unpinned) pin lies on its side, needle pointing left");

    struct RegionCase final {
        DockShapeRegion region;
        double minimum_x;
        double maximum_x;
        double minimum_y;
        double maximum_y;
    };
    // Where the filled region's centre lies in the 16-unit square.
    const std::array<RegionCase, 5> cases{{
        {DockShapeRegion::top, 6.0, 10.0, 0.0, 7.0},
        {DockShapeRegion::bottom, 6.0, 10.0, 9.0, 16.0},
        {DockShapeRegion::left, 0.0, 7.0, 6.0, 10.0},
        {DockShapeRegion::right, 9.0, 16.0, 6.0, 10.0},
        {DockShapeRegion::tab_into, 7.0, 9.0, 7.0, 9.0},
    }};
    std::set<std::string> filled_rects;
    for (const RegionCase& region : cases) {
        const RecordingPainter shape = paint_glyph(*control, PartMark::dock_shape, box, {}, region.region);
        require(shape.operations.size() == 5U, "a dock shape is a caption bar, three edges and one filled region");
        for (const Operation& operation : shape.operations) {
            require(operation.color == recipe.glyph && inside(operation.bounds, box),
                    "a dock shape draws inside its part in the glyph colour");
        }
        const Operation& filled = shape.operations.back();
        const double centre_x = filled.bounds.x + filled.bounds.width * 0.5;
        const double centre_y = filled.bounds.y + filled.bounds.height * 0.5;
        require(filled.kind == "fill" && centre_x >= region.minimum_x && centre_x <= region.maximum_x &&
                    centre_y >= region.minimum_y && centre_y <= region.maximum_y,
                "a dock shape fills the region its parameter names");
        filled_rects.insert(std::to_string(filled.bounds.x) + "," + std::to_string(filled.bounds.y) + "," +
                            std::to_string(filled.bounds.width) + "," + std::to_string(filled.bounds.height));
    }
    require(filled_rects.size() == 5U, "the five regions fill five different areas");

    const RecordingPainter tiny_pin = paint_glyph(*control, PartMark::pin, {0.0, 0.0, 5.0, 5.0}, {},
                                                  DockShapeRegion::tab_into);
    const RecordingPainter tiny_shape = paint_glyph(*control, PartMark::dock_shape, {0.0, 0.0, 7.0, 7.0}, {},
                                                    DockShapeRegion::left);
    require(tiny_pin.operations.empty() && tiny_shape.operations.empty(), "a mark too small to read draws nothing");

    // The underline mark (owner decision, 2026-10-28): one one-unit rule
    // along the bottom of its box, in the glyph colour.
    const RecordingPainter underline = paint_glyph(*control, PartMark::underline, {2.0, 10.0, 12.0, 1.0}, {},
                                                   DockShapeRegion::tab_into);
    require(part_mark_name(PartMark::underline) == "underline" && underline.operations.size() == 1U &&
                underline.operations[0].kind == "line" && underline.operations[0].color == recipe.glyph &&
                underline.operations[0].bounds.x == 2.0 && underline.operations[0].bounds.width == 12.0 &&
                underline.operations[0].bounds.y == 10.5 && underline.operations[0].bounds.height == 0.0,
            "the underline is one rule along the bottom of its box in the glyph colour");
}

PartPaint side_label(TextOrientation orientation, PartTextAlignment alignment) {
    PartPaint label;
    label.role = PartRole::label;
    label.bounds = {10.0, 20.0, 24.0, 120.0};
    label.text = "Toolbox";
    label.font = FontSpec{FontRole::control, 12.0, 400, false, 0.0};
    label.orientation = orientation;
    label.alignment = alignment;
    return label;
}

void text_orientation() {
    std::shared_ptr<Control> control = make_control<Control>(StableId("text"));
    PartPaint horizontal = side_label(TextOrientation::horizontal, PartTextAlignment::start);
    horizontal.bounds = {10.0, 20.0, 120.0, 24.0};
    RecordingPainter flat;
    paint_part(flat, *control, horizontal);
    require(flat.operations.size() == 1U && flat.operations[0].kind == "text" &&
                flat.operations[0].origin.x == 10.0,
            "horizontal text still draws as draw_text_utf8 from the start of the part");

    const double centre_x = 10.0 + 24.0 * 0.5;
    RecordingPainter up;
    paint_part(up, *control, side_label(TextOrientation::bottom_to_top, PartTextAlignment::start));
    require(up.operations.size() == 1U && up.operations[0].kind == "turned_text" &&
                up.operations[0].orientation == TextOrientation::bottom_to_top && up.operations[0].text == "Toolbox",
            "a bottom-to-top label draws as turned text");
    require(up.operations[0].origin.y == 140.0 && up.operations[0].origin.x > centre_x &&
                up.operations[0].origin.x < 34.0,
            "bottom to top starts at the bottom, its baseline right of centre so the glyph tops face left");

    RecordingPainter down;
    paint_part(down, *control, side_label(TextOrientation::top_to_bottom, PartTextAlignment::start));
    require(down.operations.size() == 1U && down.operations[0].orientation == TextOrientation::top_to_bottom &&
                down.operations[0].origin.y == 20.0 && down.operations[0].origin.x < centre_x &&
                down.operations[0].origin.x > 10.0,
            "top to bottom starts at the top, its baseline left of centre so the glyph tops face right");

    RecordingPainter centred;
    paint_part(centred, *control, side_label(TextOrientation::bottom_to_top, PartTextAlignment::center));
    const Size measured = centred.measure_text_utf8("Toolbox", side_label(TextOrientation::horizontal,
                                                                         PartTextAlignment::start).font);
    const double expected = 140.0 - (120.0 - measured.width) * 0.5;
    require(centred.operations.size() == 1U && std::abs(centred.operations[0].origin.y - expected) < 0.001,
            "alignment runs along the turned reading direction");
}

// A control that paints one turned label, to carry turned text through the
// window's recording painter, its retained display list and replay.
class SideTab final : public Control {
public:
    explicit SideTab(StableId stable_id) : Control(std::move(stable_id)) {}

    void on_paint(Painter& painter, Rect) override {
        PartPaint label = side_label(TextOrientation::bottom_to_top, PartTextAlignment::center);
        label.bounds = {0.0, 0.0, 24.0, 120.0};
        paint_part(painter, *this, label);
    }
};

void turned_text_is_retained_and_replayed() {
    std::shared_ptr<SideTab> tab = make_control<SideTab>(StableId("side-tab"));
    (*tab).set_requested_bounds({0.0, 0.0, 24.0, 120.0});
    Window window(tab, {24.0, 120.0});
    window.perform_layout();
    RecordingPainter replayed;
    static_cast<void>(window.paint(replayed, {0.0, 0.0, 24.0, 120.0}));
    bool turned = false;
    for (const Operation& operation : replayed.operations) {
        if (operation.kind == "turned_text" && operation.orientation == TextOrientation::bottom_to_top) turned = true;
        require(operation.kind != "text", "a turned run is never replayed as horizontal text");
    }
    require(turned, "the window replays a retained turned run through draw_turned_text_utf8");
    const VisualInspectionSnapshot snapshot = window.visual_inspection_snapshot();
    const VisualControlInspection* inspected = snapshot.find("side-tab");
    require(inspected != nullptr, "the side tab is inspected");
    bool reported = false;
    for (const VisualPaintOperationSnapshot& operation : (*inspected).display_operations) {
        if (operation.operation == VisualPaintOperation::draw_text &&
            operation.text_orientation == TextOrientation::bottom_to_top) {
            reported = true;
        }
    }
    require(reported, "visual inspection reports the run's orientation");
    require(snapshot.to_json().find("\"text_orientation\":\"bottom_to_top\"") != std::string::npos,
            "the inspection JSON carries the orientation");
}

constexpr std::array<PartDeclaration, 1> tab_parts{{
    {.name = "tab_face", .role = PartRole::face, .composed_from = {}},
}};
constexpr std::array<AnatomyState, 2> tab_states{{AnatomyState::hot, AnatomyState::selected}};
constexpr std::array<ItemSystemDeclaration, 1> tab_systems{{
    {.name = "page", .parts = tab_parts, .states = tab_states, .custom_states = admitted_custom_states},
}};
constexpr std::array<PartDeclaration, 1> strip_parts{{
    {.name = "region", .role = PartRole::region, .composed_from = {}},
}};
constexpr ControlAnatomy strip_anatomy{
    .control_id = "page_strip_fixture",
    .parts = strip_parts,
    .states = {},
    .custom_states = {},
    .item_systems = tab_systems,
};

void custom_states() {
    require(modified_custom_state.name == "modified" && pinned_custom_state.name == "pinned" &&
                admitted_custom_states.size() == 2U,
            "the custom states modified and pinned are admitted");
    for (const CustomStateDeclaration& state : admitted_custom_states) {
        require(snake_case(state.name) && !anatomy_state_from_name(state.name).has_value(),
                "an admitted custom state is snake_case and shadows no standard state");
    }
    const AnatomyValidation validation = validate_control_anatomy(strip_anatomy);
    require(validation.defect == AnatomyDefect::none, "an item system declaring the admitted states validates");
    const std::optional<CustomStateIndex> modified = find_item_custom_state(tab_systems[0], "modified");
    const std::optional<CustomStateIndex> pinned = find_item_custom_state(tab_systems[0], "pinned");
    require(modified && pinned && (*modified).value == 0U && (*pinned).value == 1U,
            "the admitted states are found by name in the item system that declares them");
    AnatomyStateSet item_states;
    item_states.insert(*pinned);
    require(item_states.contains(*pinned) && !item_states.contains(*modified),
            "an item carries the admitted states as custom states");
}

void roles_actions_and_fields() {
    require(std::string(semantic_role_name(SemanticRole::pane)) == "pane" &&
                std::string(semantic_role_name(SemanticRole::tab_panel)) == "tab_panel" &&
                std::string(semantic_action_name(SemanticAction::close)) == "close",
            "pane, tab_panel and close are admitted");
    std::set<std::string> actions;
    for (std::size_t index = 0; index < semantic_action_count; ++index) {
        const std::string name = semantic_action_name(static_cast<SemanticAction>(index));
        require(snake_case(name) && name != "unknown" && actions.insert(name).second,
                "every semantic action has a unique name");
    }
    require(std::string(semantic_orientation_name(SemanticOrientation::vertical)) == "vertical",
            "orientations have names");

    std::shared_ptr<TabControl> tabs = make_control<TabControl>(StableId("tabs"));
    (*tabs).set_requested_bounds({0.0, 0.0, 400.0, 200.0});
    std::shared_ptr<TabPage> first = make_control<TabPage>(StableId("tabs.first"), "Overview");
    std::shared_ptr<TabPage> second = make_control<TabPage>(StableId("tabs.second"), "Details");
    (*tabs).add_page(first);
    (*tabs).add_page(second);
    Window window(tabs, {400.0, 200.0});
    window.perform_layout();

    const std::vector<SemanticNode> items = (*tabs).semantic_virtual_children();
    require(items.size() == 2U, "a TabControl publishes one tab per page");
    for (const SemanticNode& item : items) {
        const bool press = std::find(item.actions.begin(), item.actions.end(), SemanticAction::press) !=
                           item.actions.end();
        const bool select = std::find(item.actions.begin(), item.actions.end(), SemanticAction::select) !=
                            item.actions.end();
        require(!press && select, "tabs publish select and no longer press");
    }
    require(items[0].controlled_panel == "tabs.first" && items[1].controlled_panel == "tabs.second",
            "each tab names its page as its panel");
    require((*first).semantic_descriptor().role == SemanticRole::tab_panel, "a tab page is a tab panel");
    require((*tabs).semantic_descriptor().orientation == SemanticOrientation::horizontal,
            "a tab row along the top is horizontal");

    const std::string json = window.semantic_snapshot().to_json();
    require(json.find("\"role\":\"tab_panel\"") != std::string::npos &&
                json.find("\"controlled_panel\":\"tabs.first\"") != std::string::npos &&
                json.find("\"orientation\":\"horizontal\"") != std::string::npos &&
                json.find("\"orientation\":null") != std::string::npos,
            "semantic snapshots carry the tab panel role, the relation and the orientation");
    require(window.perform_semantic_action("tabs.second.tab", SemanticAction::select) &&
                (*tabs).selected_index.read() == 1U,
            "selecting a tab still works");

    (*tabs).alignment = TabAlignment::left;
    window.perform_layout();
    require((*tabs).semantic_descriptor().orientation == SemanticOrientation::vertical,
            "a tab column along a side is vertical");
}

void ribbon_tabs() {
    const std::shared_ptr<Command> paste = std::make_shared<Command>("paste", "Paste");
    std::vector<RibbonTabSpec> specs;
    RibbonTabSpec home{.stable_id = "home", .text = "&Home"};
    RibbonGroupSpec clipboard{.stable_id = "clipboard", .caption = "Clipboard"};
    clipboard.commands.push_back({.stable_id = "paste", .command = paste});
    home.groups.push_back(std::move(clipboard));
    specs.push_back(std::move(home));
    specs.push_back({.stable_id = "insert", .text = "&Insert"});
    std::shared_ptr<Ribbon> ribbon = make_control<Ribbon>(StableId("ribbon"));
    (*ribbon).set_tabs(std::move(specs));
    Window window(ribbon, {600.0, 140.0});
    window.perform_layout();
    const std::vector<SemanticNode> nodes = (*ribbon).semantic_virtual_children();
    const SemanticNode* row = nullptr;
    const SemanticNode* plane = nullptr;
    for (const SemanticNode& node : nodes) {
        if (node.role == SemanticRole::tab_group) row = &node;
        if (node.stable_id == "ribbon.command-plane") plane = &node;
    }
    require(row != nullptr && plane != nullptr, "the ribbon publishes its tab row and command plane");
    require((*row).orientation == SemanticOrientation::horizontal, "the ribbon's tab row is horizontal");
    require((*plane).role == SemanticRole::tab_panel, "the command plane is the selected tab's panel");
    for (const SemanticNode& tab : (*row).children) {
        const bool press = std::find(tab.actions.begin(), tab.actions.end(), SemanticAction::press) !=
                           tab.actions.end();
        require(!press, "ribbon tabs publish select and no longer press");
    }
    require((*row).children.size() == 2U && (*row).children[0].controlled_panel == "ribbon.command-plane" &&
                (*row).children[1].controlled_panel.empty(),
            "only the selected ribbon tab names the command plane");
}

} // namespace

int main() {
    marks();
    text_orientation();
    turned_text_is_retained_and_replayed();
    custom_states();
    roles_actions_and_fields();
    ribbon_tabs();
    std::cout << "Docking vocabulary: pin and dock-shape marks, turned text, custom states, pane and tab panel "
                 "roles, close, relations and orientation passed\n";
    return 0;
}
