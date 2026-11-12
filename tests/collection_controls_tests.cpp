#include "gui_forms/collection_controls.hpp"
#include "gui_forms/commands.hpp"
#include "gui_forms/window.hpp"
#include "gui_forms_lab/correspondence_view.hpp"
#include "support/named_callbacks.hpp"
#include "support/professional_theme.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace gui_forms;
using namespace std::chrono_literals;

bool is_background_face(const char operation) {
    const bool result = operation == 'F' || operation == 'G';
    return result;
}

bool icon_label_inside_cell(const Point point) {
    const bool result = point.x >= 4.0 && point.x < 186.0;
    return result;
}

class ImageRecordingPainter final : public Painter {
public:
    Size measure_text_utf8(const std::string_view text, const FontSpec font) override {
        ++measurement_queries;
        measurement_bytes += text.size();
        Size result = Painter::measure_text_utf8(text, font);
        // Deliberately nonmonotonic fixture: some shorter ellipsis candidates
        // are wider than longer ones. Fit is required; maximality is not.
        if (nonmonotonic_widths && text.ends_with("…") && text.size() % 7U == 0U) result.width = 90.0;
        return result;
    }
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void clip_rounded_rect(Rect, double) override {}
    void fill_rect(Rect bounds, Color) override {
        fills.push_back(bounds);
        paint_order.push_back('F');
    }
    void fill_rounded_rect(Rect, double, Color) override {}
    void stroke_rect(Rect bounds, Color, double) override {
        strokes.push_back(bounds);
        paint_order.push_back('S');
    }
    void stroke_rounded_rect(Rect bounds, double, Color, double) override {
        rounded_strokes.push_back(bounds);
    }
    void fill_linear_gradient(Rect, Point, Point,
                              std::span<const GradientStop>) override {
        paint_order.push_back('G');
    }
    void draw_line(Point from, Point to, Color, double) override {
        lines.emplace_back(from, to);
        paint_order.push_back('L');
    }
    void draw_text_utf8(Point origin, std::string_view text, FontSpec font, Color) override {
        texts.emplace_back(text);
        fonts.push_back(font);
        text_origins.push_back(origin);
        paint_order.push_back('T');
    }
    void draw_image(ImageId image, Rect destination, double opacity) override {
        images.push_back(image);
        destinations.push_back(destination);
        opacities.push_back(opacity);
    }

    std::vector<ImageId> images{};
    std::vector<Rect> destinations{};
    std::vector<double> opacities{};
    std::vector<Rect> fills{};
    std::vector<Rect> strokes{};
    // The theme engine's part edges (selection plates, wells, tool tips).
    std::vector<Rect> rounded_strokes{};
    std::vector<std::string> texts{};
    std::vector<FontSpec> fonts{};
    std::vector<Point> text_origins{};
    std::vector<std::pair<Point, Point>> lines{};
    std::vector<char> paint_order{};
    std::size_t measurement_queries{};
    std::size_t measurement_bytes{};
    bool nonmonotonic_widths{};
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

Point semantic_center(const ObjectView& view, std::string_view stable_id);

class AppendTreeExpansion final {
public:
    explicit AppendTreeExpansion(std::string& trace) : trace_(trace) {}

    void operator()(const TreeExpansionChange& change) const {
        trace_ += change.stable_id +
            (change.expanded ? ":open\n" : ":closed\n");
    }

private:
    std::string& trace_;
};

class AppendTreeSelection final {
public:
    explicit AppendTreeSelection(std::string& trace) : trace_(trace) {}

    void operator()(const TreeSelectionChange& change) const {
        trace_ += change.current_id + ":selected\n";
    }

private:
    std::string& trace_;
};

class SemanticNodeHasStableId final {
public:
    explicit SemanticNodeHasStableId(std::string_view stable_id)
        : stable_id_(stable_id) {}

    bool operator()(const SemanticNode& node) const {
        const bool matches = node.stable_id == stable_id_;
        return matches;
    }

private:
    std::string_view stable_id_{};
};

class RecordObjectContextId final {
public:
    explicit RecordObjectContextId(std::string& stable_id)
        : stable_id_(stable_id) {}

    void operator()(const ObjectContextRequest& request) const {
        stable_id_ = request.stable_id;
    }

private:
    std::string& stable_id_;
};

bool semantic_node_is_selected(const SemanticNode& node) {
    const bool selected = has_semantic_state(node.states, SemanticState::selected);
    return selected;
}

class AppendCommandInvocation final {
public:
    explicit AppendCommandInvocation(std::string& trace) : trace_(trace) {}

    void operator()(const CommandInvocation& invocation) const {
        trace_ += invocation.command_id + "@" + invocation.source_id + "\n";
    }

private:
    std::string& trace_;
};

void test_breadcrumb_identity_overflow_edit_and_input() {
    std::shared_ptr<BreadcrumbTrail> trail = make_control<BreadcrumbTrail>(
        StableId("breadcrumb"), "breadcrumb.exact-editor");
    (*trail).set_requested_bounds({0.0, 0.0, 160.0, 28.0});
    (*trail).accessible_name = "Current location breadcrumb";
    (*trail).set_segments({
        {"path.root", "Home", "Navigate to Home"},
        {"path.one", "Projects", "Navigate to Projects"},
        {"path.two", "House Composite", "Navigate to House Composite"},
        {"path.three", "Frontend", "Navigate to Frontend"},
        {"path.current", "Source", "Current location"},
    });
    Window window(trail, {160.0, 28.0});
    // windows-professional's chevron faces and joint lines are checked here.
    window.set_theme(test_support::professional_theme());
    window.perform_layout();

    require((*trail).children().size() == 1U &&
                (*trail).children().front() == (*trail).editor(),
            "BreadcrumbTrail must own exactly one retained same-row editor rather than per-segment controls");
    require(!(*trail).hidden_segment_ids().empty(),
            "constrained BreadcrumbTrail must retain explicit middle-overflow identities");
    const std::vector<SemanticNode> constrained =
        (*trail).semantic_virtual_children();
    require(constrained.size() >= 4U &&
                constrained.front().stable_id == "path.root" &&
                constrained[1].stable_id == (*trail).overflow_stable_id() &&
                constrained[constrained.size() - 2U].stable_id == "path.current" &&
                constrained.back().stable_id == (*trail).edit_stable_id(),
            "BreadcrumbTrail overflow must preserve root/current identities and expose overflow/edit actuators");

    std::string activated{};
    std::size_t overflow_count{};
    std::string committed{};
    std::size_t cancelled{};
    std::size_t starts{};
    SubscriptionToken activation = (*trail).segment_activated().subscribe(
        test_support::RecordValue<std::string>(activated));
    SubscriptionToken overflow = (*trail).overflow_activated().subscribe(
        test_support::IncrementCounter<std::size_t>(overflow_count));
    SubscriptionToken commit = (*trail).edit_committed().subscribe(
        test_support::RecordValue<std::string>(committed));
    SubscriptionToken cancel = (*trail).edit_cancelled().subscribe(
        test_support::IncrementCounter<std::size_t>(cancelled));
    SubscriptionToken start = (*trail).edit_started().subscribe(
        test_support::IncrementCounter<std::size_t, const std::string&>(starts));
    std::size_t completions{};
    SubscriptionToken completion = (*trail).edit_completion_requested().subscribe(
        test_support::IncrementCounter<std::size_t>(completions));
    const bool operation_check_1 = (*trail).on_semantic_child_action(
                "path.root", SemanticAction::press, {});
    require(operation_check_1, "breadcrumb semantic segment press must share typed activation");
    const bool operation_check_2 = activated == "path.root";
    require(operation_check_2, "breadcrumb semantic segment press must share typed activation");
    const bool operation_check_3 = (*trail).on_semantic_child_action(
                (*trail).overflow_stable_id(), SemanticAction::show_menu, {});
    require(operation_check_3, "breadcrumb overflow semantic action must enter the operational overflow path");
    const bool operation_check_4 = overflow_count == 1U;
    require(operation_check_4, "breadcrumb overflow semantic action must enter the operational overflow path");

    const bool operation_check_5 = window.request_focus(trail);
    require(operation_check_5, "BreadcrumbTrail must accept retained keyboard focus");
    const bool operation_check_6 = window.dispatch_key({KeyAction::down, PhysicalKey::home});
    require(operation_check_6, "Home and Enter must activate the first stable breadcrumb segment");
    const bool operation_check_7 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_7, "Home and Enter must activate the first stable breadcrumb segment");
    const bool operation_check_8 = activated == "path.root";
    require(operation_check_8, "Home and Enter must activate the first stable breadcrumb segment");
    const bool operation_check_9 = window.dispatch_key({KeyAction::down, PhysicalKey::end});
    require(operation_check_9, "End and Enter must replace the trail presentation with its owned editor");
    const bool operation_check_10 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_10, "End and Enter must replace the trail presentation with its owned editor");
    const bool operation_check_11 = (*trail).editing.read();
    require(operation_check_11, "End and Enter must replace the trail presentation with its owned editor");
    const bool operation_check_12 = (*(*trail).editor()).visible.read();
    require(operation_check_12, "End and Enter must replace the trail presentation with its owned editor");
    const bool operation_check_13 = starts == 1U;
    require(operation_check_13, "End and Enter must replace the trail presentation with its owned editor");
    window.perform_layout();
    const Rect trail_bounds = (*trail).committed_arranged_bounds();
    const Rect editor_bounds = (*(*trail).editor()).committed_arranged_bounds();
    require(editor_bounds.x == 1.0 && editor_bounds.y == 1.0 &&
                editor_bounds.width == trail_bounds.width - 2.0 &&
                editor_bounds.height == trail_bounds.height - 2.0,
            "breadcrumb editing must preserve outer identity and row geometry");
    (*(*trail).editor()).text = "/Users/example/Projects";
    (*trail).tab_completion_available = true;
    const bool operation_check_14 = window.dispatch_key({KeyAction::down, PhysicalKey::tab});
    require(operation_check_14, "Tab must enter the typed completion route without traversing focus when a suggestion is available");
    const bool operation_check_15 = completions == 1U;
    require(operation_check_15, "Tab must enter the typed completion route without traversing focus when a suggestion is available");
    const bool operation_check_16 = (*trail).editing.read();
    require(operation_check_16, "Tab must enter the typed completion route without traversing focus when a suggestion is available");
    const bool operation_check_17 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_17, "path commit must publish exact editor text while the caller decides admissibility");
    const bool operation_check_18 = committed == "/Users/example/Projects";
    require(operation_check_18, "path commit must publish exact editor text while the caller decides admissibility");
    const bool operation_check_19 = (*trail).editing.read();
    require(operation_check_19, "path commit must publish exact editor text while the caller decides admissibility");
    const bool operation_check_20 = window.dispatch_key({KeyAction::down, PhysicalKey::escape});
    require(operation_check_20, "Escape must roll back presentation and restore the breadcrumb row");
    const bool operation_check_21 = !(*trail).editing.read();
    require(operation_check_21, "Escape must roll back presentation and restore the breadcrumb row");
    const bool operation_check_22 = cancelled == 1U;
    require(operation_check_22, "Escape must roll back presentation and restore the breadcrumb row");

    ImageRecordingPainter painter{};
    window.paint(painter, {0.0, 0.0, 160.0, 28.0});
    const std::vector<char>::const_reverse_iterator final_background = std::find(
        painter.paint_order.crbegin(), painter.paint_order.crend(), 'G');
    const std::vector<char>::const_iterator final_face = final_background.base();
    const std::size_t joint_lines = static_cast<std::size_t>(std::count(
        final_face, painter.paint_order.cend(), 'L'));
    require(final_face != painter.paint_order.begin() &&
                joint_lines == (constrained.size() - 1U) * 2U &&
                std::find_if(final_face, painter.paint_order.cend(),
                    is_background_face) ==
                    painter.paint_order.end(),
            "BreadcrumbTrail must paint two connected lines per shared chevron edge after every overlapping face");

    bool duplicate_rejected{};
    try {
        (*trail).set_segments({{"duplicate", "One", {}},
                               {"duplicate", "Two", {}}});
    } catch (const std::invalid_argument&) {
        duplicate_rejected = true;
    }
    require(duplicate_rejected,
            "BreadcrumbTrail must reject duplicate caller identities");
}

void test_breadcrumb_readable_raised_geometry() {
    const std::shared_ptr<BreadcrumbTrail> trail = make_control<BreadcrumbTrail>(StableId("raised.trail"));
    (*trail).set_requested_bounds({0, 0, 500, 32});
    (*trail).set_segments({{"root", "Home", {}}, {"leaf", "Documents", {}}});
    Window window(trail, {500, 32});
    window.perform_layout();
    const double original_width = (*trail).semantic_virtual_children().front().bounds.width;
    (*(*trail).editor()).text = "draft path";
    (*trail).font = FontSpec{FontRole::content, 13, 400, false};
    (*trail).appearance = BreadcrumbAppearance::raised;
    window.perform_layout();
    const std::vector<SemanticNode> nodes = (*trail).semantic_virtual_children();
    require(nodes.front().bounds.width > original_width &&
            (*(*trail).editor()).font.read().size == 13 && (*(*trail).editor()).text.read() == "draft path",
            "larger breadcrumb type must expand hit geometry and preserve the inline editor draft");
    std::string activated{};
    SubscriptionToken token = (*trail).segment_activated().subscribe(test_support::RecordValue<std::string>(activated));
    const Rect first = nodes.front().bounds;
    const Point nose{first.x + first.width - 2.0, first.y + first.height * 0.5};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, nose}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, nose}));
    require(activated == "root", "a chevron's pointed nose must activate its own segment, not the next one");
    const Point next_face{first.x + first.width - 2.0, first.y + 2.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, next_face}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, next_face}));
    require(activated == "leaf", "the area above a chevron tip must belong to the following visible face");
    ImageRecordingPainter painter{};
    window.paint(painter, {0, 0, 500, 32});
    require(!painter.fonts.empty() && painter.fonts.back().size == 13 &&
            std::count(painter.paint_order.begin(), painter.paint_order.end(), 'G') > 3,
            "raised segments must render authored type and depth rather than flat separator text");
    bool rejected{};
    try { (*trail).font = FontSpec{FontRole::content, -1, 400, false}; }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && (*trail).font.read().size == 13, "invalid breadcrumb font must leave the last valid metrics intact");
}

void test_tree_visibility_identity_and_navigation() {
    std::shared_ptr<gui_forms::TreeView> tree = make_control<TreeView>(StableId("tree"));
    (*tree).show_expanders = false;
    require(!(*tree).show_expanders.read(),
            "TreeView must retain explicit expander visibility policy");
    (*tree).show_expanders = true;
    (*tree).set_requested_bounds({0.0, 0.0, 240.0, 140.0});
    (*tree).set_items({
        {"tree.local", "Local", 0, true, true},
        {"tree.home", "quentin", 1, true, true},
        {"tree.desktop", "Desktop", 2},
        {"tree.work", "Work", 2, true, false},
        {"tree.projects", "Projects", 3},
        {"tree.reference", "Reference", 3},
        {"tree.volumes", "Volumes", 0, true, false},
        {"tree.disk", "Macintosh HD", 1},
    });
    (*tree).selected_id = "tree.work";
    Window window(tree, {240.0, 140.0});
    require((*tree).children().empty(),
            "TreeView must not allocate one retained control per logical item");
    const std::vector<SemanticNode> collapsed = (*tree).semantic_virtual_children();
    require(collapsed.size() == 5U && collapsed.back().stable_id == "tree.volumes",
            "collapsed TreeView descendants must leave the visible semantic window");

    std::string trace{};
    SubscriptionToken expansion = (*tree).expansion_changed().subscribe(
        AppendTreeExpansion(trace));
    SubscriptionToken selection = (*tree).selection_changed().subscribe(
        AppendTreeSelection(trace));
    (*tree).set_expanded("tree.work", true);
    require((*tree).semantic_virtual_children().size() == 5U,
            "TreeView semantic realization must remain clipped to visible rows");
    const bool operation_check_23 = window.request_focus(tree);
    require(operation_check_23, "TreeView must accept retained focus");
    const bool operation_check_24 = window.dispatch_key({KeyAction::down, PhysicalKey::right});
    require(operation_check_24, "TreeView Right must enter the first expanded child");
    const bool operation_check_25 = (*tree).selected_id.read() == "tree.projects";
    require(operation_check_25, "TreeView Right must enter the first expanded child");
    const bool operation_check_26 = window.dispatch_text({"ref"});
    require(operation_check_26, "TreeView type-to-select must use stable visible-row navigation");
    const bool operation_check_27 = (*tree).selected_id.read() == "tree.reference";
    require(operation_check_27, "TreeView type-to-select must use stable visible-row navigation");
    require(trace == "tree.work:open\ntree.projects:selected\ntree.reference:selected\n",
            "TreeView expansion and selection events must be deterministic");
}

void test_tree_model_validation() {
    std::shared_ptr<gui_forms::TreeView> tree = make_control<TreeView>(StableId("tree.invalid"));
    bool rejected{};
    try {
        (*tree).set_items({{"duplicate", "One", 0},
                         {"duplicate", "Two", 2}});
    } catch (const std::invalid_argument&) {
        rejected = true;
    }
    require(rejected, "TreeView must reject duplicate IDs and invalid depth jumps");
}

void test_tree_and_object_view_consume_keyed_image_list() {
    std::shared_ptr<gui_forms::Panel> root = make_control<Panel>(StableId("collections.images.root"));
    std::shared_ptr<gui_forms::TreeView> tree = make_control<TreeView>(StableId("collections.images.tree"));
    std::shared_ptr<gui_forms::ObjectView> objects = make_control<ObjectView>(StableId("collections.images.objects"));
    (*tree).set_requested_bounds({0.0, 0.0, 180.0, 80.0});
    (*objects).set_requested_bounds({180.0, 0.0, 180.0, 100.0});
    (*root).add_child(tree);
    (*root).add_child(objects);
    Window window(root, {360.0, 100.0});
    const std::array<std::byte, 4> pixel{
        std::byte{0x33}, std::byte{0x77}, std::byte{0xcc}, std::byte{0xff}};
    const ImageLoadResult loaded = window.load_bgra32_premultiplied(
        1U, 1U, 4U, pixel);
    require(static_cast<bool>(loaded), "collection image fixture must load");
    std::shared_ptr<gui_forms::ImageList> images = std::make_shared<ImageList>(window, Size{18.0, 18.0});
    (*images).add_image("folder", loaded.image);
    (*tree).image_list = images;
    (*objects).image_list = images;
    (*tree).set_items({{"tree.image", "Folder", 0U, false, false, true,
                      "FOLDER"}});
    (*objects).set_items({{"object.image",
                         "An impossibly long local object name that cannot fit",
                         "1 item", "Fixture",
                         ObjectGlyph::folder, true, "folder"}});
    window.perform_layout();
    ImageRecordingPainter painter{};
    window.paint(painter, {0.0, 0.0, 360.0, 100.0});
    require(painter.images.size() == 2U &&
                painter.images[0] == loaded.image &&
                painter.images[1] == loaded.image &&
                !painter.destinations[0].empty() &&
                !painter.destinations[1].empty(),
            "TreeView and ObjectView must paint shared keyed ImageList resources instead of private glyph paths");
    require(painter.texts.size() == 3U && painter.texts.front() == "Folder" &&
                painter.texts[1] + " " + painter.texts[2] ==
                    "An impossibly long local object name that cannot fit",
            "ObjectView icon labels must use the admitted second line before eliding complete text");
}

void test_object_label_wrapping_focus_and_full_name_inspection() {
    std::shared_ptr<gui_forms::ObjectView> objects =
        make_control<ObjectView>(StableId("objects.labels"));
    (*objects).set_requested_bounds({0.0, 0.0, 190.0, 170.0});
    (*objects).icon_cell_size = {90.0, 78.0};
    (*objects).show_secondary_text = false;
    const std::string complete_name =
        "Résumé資料 archive evidence package with immutable identity.txt";
    (*objects).set_items({
        {"labels.long", complete_name, {}, "Complete UTF-8 fixture name",
         ObjectGlyph::document},
        {"labels.wrap", "North Shore Recordings", {}, "Two-line fixture",
         ObjectGlyph::folder},
    });
    Window window(objects, {190.0, 170.0});
    window.perform_layout();

    ImageRecordingPainter rest{};
    window.paint(rest, {0.0, 0.0, 190.0, 170.0});
    require(rest.texts.size() == 4U &&
                rest.texts[0] == "Résumé資料" &&
                rest.texts[1].ends_with("…") &&
                rest.texts[2] == "North Shore" &&
                rest.texts[3] == "Recordings",
            "icon labels must paint at most two centered UTF-8-safe lines and elide only retained overflow");
    require(rest.text_origins[1].y > rest.text_origins[0].y &&
                rest.text_origins[3].y > rest.text_origins[2].y &&
                std::all_of(rest.text_origins.begin(), rest.text_origins.end(),
                    icon_label_inside_cell),
            "two-line icon labels must retain stable baselines inside their cells");

    const Point first = semantic_center(*objects, "labels.long");
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, first}));
    ImageRecordingPainter hovered{};
    window.paint(hovered, {0.0, 0.0, 190.0, 170.0});
    std::string inspected_name{};
    for (std::size_t index = 4U; index < hovered.texts.size(); ++index) {
        if (!inspected_name.empty()) inspected_name += ' ';
        inspected_name += hovered.texts[index];
    }
    // The inspection surface is a tool tip part, its edge the last one drawn.
    require(hovered.rounded_strokes.size() > rest.rounded_strokes.size(),
            "hovered truncated item must draw one inspection surface");
    const Rect inspection_border = hovered.rounded_strokes.back();
    require(inspected_name == complete_name &&
                inspection_border.x >= 4.0 &&
                inspection_border.x + inspection_border.width <= 186.0 &&
                inspection_border.y >= 4.0 &&
                inspection_border.y + inspection_border.height <= 166.0,
            "hovered truncated item must disclose its complete name in a bounded wrapped inspection surface");
    require((*objects).selected_ids().empty(),
            "full-name hover inspection must not mutate selection authority");

    PointerEvent down{PointerAction::down, PointerButton::primary, first};
    PointerEvent up{PointerAction::up, PointerButton::primary, first};
    const bool operation_check_28 = window.dispatch_pointer(down);
    require(operation_check_28, "pointer fixture must focus and select its item");
    const bool operation_check_29 = window.dispatch_pointer(up);
    require(operation_check_29, "pointer fixture must focus and select its item");
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::leave, PointerButton::none, {189.0, 169.0}}));
    ImageRecordingPainter pointer_focused{};
    window.paint(pointer_focused, {0.0, 0.0, 190.0, 170.0});
    // The selection plate adds one part edge to the glyph wells' edges.
    require(pointer_focused.lines.empty() &&
                pointer_focused.rounded_strokes.size() == rest.rounded_strokes.size() + 1U,
            "pointer focus must preserve the selection boundary without masquerading as keyboard focus");

    const bool operation_check_30 = window.dispatch_key({KeyAction::down, PhysicalKey::right,
                                 Modifier::control});
    require(operation_check_30, "modified keyboard navigation must move focus independently of stable selection");
    const bool operation_check_31 = (*objects).focused_id() == "labels.wrap";
    require(operation_check_31, "modified keyboard navigation must move focus independently of stable selection");
    const bool operation_check_32 = (*objects).selected_id.read() == "labels.long";
    require(operation_check_32, "modified keyboard navigation must move focus independently of stable selection");
    ImageRecordingPainter keyboard_short{};
    window.paint(keyboard_short, {0.0, 0.0, 190.0, 170.0});
    // The keyboard focus is the theme's focus ring beside the selection
    // plate's edge: one more rounded stroke, or, for a dotted ring (the
    // default theme's), its dots, painted as fills.
    const std::size_t plate_strokes = rest.rounded_strokes.size() + 1U;
    const bool stroked_ring = keyboard_short.rounded_strokes.size() == plate_strokes + 1U;
    const std::ptrdiff_t focused_fills = std::count(keyboard_short.paint_order.begin(),
                                                    keyboard_short.paint_order.end(), 'F');
    const std::ptrdiff_t unfocused_fills = std::count(pointer_focused.paint_order.begin(),
                                                      pointer_focused.paint_order.end(), 'F');
    const bool dotted_ring = keyboard_short.rounded_strokes.size() == plate_strokes && focused_fills > unfocused_fills;
    require(stroked_ring || dotted_ring,
            "selection boundary and dotted keyboard focus must remain visibly distinct on separate cells");

    const bool operation_check_33 = window.dispatch_key({KeyAction::down, PhysicalKey::left,
                                 Modifier::control});
    require(operation_check_33, "keyboard fixture must return independent focus without rewriting selection");
    const bool operation_check_34 = (*objects).focused_id() == "labels.long";
    require(operation_check_34, "keyboard fixture must return independent focus without rewriting selection");
    const bool operation_check_35 = (*objects).selected_id.read() == "labels.long";
    require(operation_check_35, "keyboard fixture must return independent focus without rewriting selection");
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none,
         semantic_center(*objects, "labels.wrap")}));
    ImageRecordingPainter keyboard_long{};
    window.paint(keyboard_long, {0.0, 0.0, 190.0, 170.0});
    std::string keyboard_inspected_name{};
    for (std::size_t index = 4U; index < keyboard_long.texts.size(); ++index) {
        if (!keyboard_inspected_name.empty()) keyboard_inspected_name += ' ';
        keyboard_inspected_name += keyboard_long.texts[index];
    }
    require(keyboard_long.rounded_strokes.size() >= rest.rounded_strokes.size() + 2U &&
                keyboard_inspected_name == complete_name,
            "non-truncated hover must not suppress complete-name inspection for the keyboard-focused item");

    (*objects).view_mode = ObjectViewMode::details;
    ImageRecordingPainter details{};
    window.paint(details, {0.0, 0.0, 190.0, 170.0});
    require(!details.texts.empty() && details.texts.front().ends_with("…") &&
                details.text_origins.front().x == 42.0,
            "details-mode names must remain single-line and width-bounded before the metadata column");

    std::shared_ptr<gui_forms::ObjectView> grapheme_objects =
        make_control<ObjectView>(StableId("objects.graphemes"));
    (*grapheme_objects).set_requested_bounds({0.0, 0.0, 94.0, 90.0});
    (*grapheme_objects).icon_cell_size = {90.0, 78.0};
    (*grapheme_objects).show_secondary_text = false;
    const std::string joined_family =
        "👨‍👩‍👧‍👦‍👨‍👩‍👧‍👦";
    const TextStore joined_family_store(joined_family);
    require(joined_family_store.grapheme_count() == GraphemeIndex(1U),
            "Unicode 17 fixture must be one extended grapheme cluster");
    (*grapheme_objects).set_items({
        {"graphemes.long", "Prefix " + joined_family + " suffix", {},
         "ZWJ truncation fixture", ObjectGlyph::document},
    });
    Window grapheme_window(grapheme_objects, {94.0, 90.0});
    grapheme_window.perform_layout();
    ImageRecordingPainter graphemes{};
    grapheme_window.paint(graphemes, {0.0, 0.0, 94.0, 90.0});
    require(graphemes.texts.size() == 2U &&
                graphemes.texts[0] == "Prefix" &&
                graphemes.texts[1] == "…",
            "icon-label elision must drop an overwide grapheme whole rather than split its ZWJ sequence");
}

void test_object_virtualization_view_preservation_and_input() {
    std::shared_ptr<gui_forms::ObjectView> objects = make_control<ObjectView>(StableId("objects"));
    (*objects).show_secondary_text = false;
    require(!(*objects).show_secondary_text.read(),
            "ObjectView must retain secondary-text visibility policy");
    (*objects).show_secondary_text = true;
    (*objects).set_requested_bounds({0.0, 0.0, 420.0, 190.0});
    std::vector<ObjectViewItem> model{};
    model.reserve(1000U);
    for (std::size_t index = 0; index < 1000U; ++index) {
        model.push_back({"object." + std::to_string(index),
                         index == 777U ? "Quartz report" :
                             "Object " + std::to_string(index),
                         std::to_string(index) + " KB",
                         "Deterministic fixture object",
                         index % 5U == 0U ? ObjectGlyph::folder
                                          : ObjectGlyph::document});
    }
    (*objects).set_items(std::move(model));
    (*objects).selected_id = "object.5";
    Window window(objects, {420.0, 190.0});
    require((*objects).children().empty(),
            "ObjectView must not allocate one retained control per logical item");
    require((*objects).semantic_virtual_children().size() <= 12U,
            "ObjectView semantic realization must remain bounded for 1000 items");
    const bool operation_check_36 = window.request_focus(objects);
    require(operation_check_36, "ObjectView must accept retained focus");
    const bool operation_check_37 = window.dispatch_key({KeyAction::down, PhysicalKey::right});
    require(operation_check_37, "ObjectView Right must use deterministic spatial navigation");
    const bool operation_check_38 = (*objects).selected_id.read() == "object.6";
    require(operation_check_38, "ObjectView Right must use deterministic spatial navigation");
    const bool operation_check_39 = window.dispatch_text({"quartz"});
    require(operation_check_39, "ObjectView type-to-select must reach an unrealized stable item");
    const bool operation_check_40 = (*objects).selected_id.read() == "object.777";
    require(operation_check_40, "ObjectView type-to-select must reach an unrealized stable item");
    (*objects).view_mode = ObjectViewMode::details;
    require((*objects).selected_id.read() == "object.777" &&
                (*objects).semantic_virtual_children().size() <= 8U,
            "ObjectView mode changes must preserve stable selection and bounded semantics");

    std::string activated{};
    SubscriptionToken activation = (*objects).item_activated().subscribe(
        test_support::RecordValue<std::string>(activated));
    const bool operation_check_41 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_41, "ObjectView Enter must activate the focused stable item");
    const bool operation_check_42 = activated == "object.777";
    require(operation_check_42, "ObjectView Enter must activate the focused stable item");
    activated.clear();
    const std::vector<SemanticNode> visible = (*objects).semantic_virtual_children();
    const std::vector<SemanticNode>::const_iterator target = std::find_if(
        visible.begin(), visible.end(),
        SemanticNodeHasStableId("object.777"));
    require(target != visible.end(), "focused ObjectView item must be realized");
    const Point center{(*target).bounds.x + (*target).bounds.width * .5,
                       (*target).bounds.y + (*target).bounds.height * .5};
    require((*objects).item_id_at(center) == "object.777" &&
                (*objects).item_id_at({421.0, 191.0}).empty(),
            "ObjectView must map root-client drag coordinates to stable item identity");
    PointerEvent single_down{PointerAction::down, PointerButton::primary, center};
    PointerEvent single_up{PointerAction::up, PointerButton::primary, center};
    const bool operation_check_43 = window.dispatch_pointer(single_down);
    require(operation_check_43, "ObjectView single click must select without invoking the default action");
    const bool operation_check_44 = window.dispatch_pointer(single_up);
    require(operation_check_44, "ObjectView single click must select without invoking the default action");
    const bool operation_check_45 = activated.empty();
    require(operation_check_45, "ObjectView single click must select without invoking the default action");
    PointerEvent double_down{PointerAction::down, PointerButton::primary, center};
    double_down.click_count = 2U;
    PointerEvent double_up{PointerAction::up, PointerButton::primary, center};
    double_up.click_count = 2U;
    const bool operation_check_46 = window.dispatch_pointer(double_down);
    require(operation_check_46, "ObjectView native double click must invoke exactly one default action");
    const bool operation_check_47 = window.dispatch_pointer(double_up);
    require(operation_check_47, "ObjectView native double click must invoke exactly one default action");
    const bool operation_check_48 = activated == "object.777";
    require(operation_check_48, "ObjectView native double click must invoke exactly one default action");
    const bool operation_check_49 = (*objects).on_semantic_child_action("object.777", SemanticAction::select, {});
    require(operation_check_49, "ObjectView semantic selection must share the ordinary selection path");
    const bool operation_check_50 = (*objects).selected_id.read() == "object.777";
    require(operation_check_50, "ObjectView semantic selection must share the ordinary selection path");
}

Point semantic_center(const ObjectView& view, std::string_view stable_id) {
    const std::vector<SemanticNode> nodes = view.semantic_virtual_children();
    const std::vector<SemanticNode>::const_iterator found = std::find_if(
        nodes.begin(), nodes.end(), SemanticNodeHasStableId(stable_id));
    require(found != nodes.end(), "test item must be semantically realized");
    return {(*found).bounds.x + (*found).bounds.width * .5,
            (*found).bounds.y + (*found).bounds.height * .5};
}

void pointer_click(Window& window, Point point, PointerButton button,
                   Modifier modifiers = Modifier::none) {
    PointerEvent down{PointerAction::down, button, point, {}, modifiers};
    PointerEvent up{PointerAction::up, button, point, {}, modifiers};
    const bool operation_check_51 = window.dispatch_pointer(down);
    require(operation_check_51, "collection pointer click must be retained and handled");
    const bool operation_check_52 = window.dispatch_pointer(up);
    require(operation_check_52, "collection pointer click must be retained and handled");
    // A secondary click is followed by the host's context request, as
    // Windows' WM_CONTEXTMENU follows the release.
    if (button == PointerButton::secondary) {
        const bool requested = window.dispatch_context_request(ContextRequestOrigin::pointer, point);
        require(requested, "the collection must take the pointer context request");
    }
}

void require_selection(const ObjectView& view,
                       std::initializer_list<std::string_view> expected,
                       const char* message) {
    if (view.selected_ids().size() != expected.size()) {
        throw std::runtime_error(message);
    }
    std::size_t index{};
    for (const std::string_view id : expected) {
        if (view.selected_ids()[index++] != id) {
            throw std::runtime_error(message);
        }
    }
}

void test_object_multiselection_pointer_keyboard_and_semantics() {
    std::shared_ptr<gui_forms::ObjectView> objects = make_control<ObjectView>(StableId("objects.multi"));
    (*objects).set_requested_bounds({0.0, 0.0, 320.0, 270.0});
    (*objects).icon_cell_size = {100.0, 80.0};
    std::vector<ObjectViewItem> model{};
    for (std::size_t index = 0; index < 8U; ++index) {
        model.push_back({"multi." + std::to_string(index),
                         "Item " + std::to_string(index), {}, {},
                         ObjectGlyph::document});
    }
    (*objects).set_items(model);
    Window window(objects, {320.0, 270.0});
    const bool operation_check_53 = window.request_focus(objects);
    require(operation_check_53, "multi-selection collection must accept focus");

    std::vector<ObjectSelectionChange> changes{};
    SubscriptionToken changed = (*objects).selection_changed().subscribe(
        test_support::PushBack<std::vector<ObjectSelectionChange>,
                               const ObjectSelectionChange&>(changes));

    pointer_click(window, semantic_center(*objects, "multi.1"),
                  PointerButton::primary);
    require_selection(*objects, {"multi.1"},
                      "plain click must establish one stable selection");
    require((*objects).selected_id.read() == "multi.1" &&
                (*objects).selection_anchor_id() == "multi.1",
            "plain click must establish primary and range-anchor identities");

    pointer_click(window, semantic_center(*objects, "multi.3"),
                  PointerButton::primary, Modifier::meta);
    require_selection(*objects, {"multi.1", "multi.3"},
                      "Command-click must toggle without discarding selection");
    require((*objects).selected_id.read() == "multi.3",
            "newly toggled item must become the primary selection");

    pointer_click(window, semantic_center(*objects, "multi.6"),
                  PointerButton::primary, Modifier::shift);
    require_selection(*objects, {"multi.3", "multi.4", "multi.5", "multi.6"},
                      "Shift-click must replace selection with an inclusive visual range");
    require((*objects).selection_anchor_id() == "multi.3" &&
                (*objects).focused_id() == "multi.6",
            "range selection must preserve anchor and move independent focus");

    std::string context_id{};
    SubscriptionToken context = (*objects).context_requested().subscribe(
        RecordObjectContextId(context_id));
    pointer_click(window, semantic_center(*objects, "multi.4"),
                  PointerButton::secondary);
    require_selection(*objects, {"multi.3", "multi.4", "multi.5", "multi.6"},
                      "right click inside multiselection must preserve it");
    require(context_id == "multi.4",
            "right click must target the item without rewriting selection authority");
    const std::vector<SemanticNode> semantic_menu_nodes = (*objects).semantic_virtual_children();
    const std::vector<SemanticNode>::const_iterator semantic_menu_node = std::find_if(
        semantic_menu_nodes.begin(), semantic_menu_nodes.end(),
        SemanticNodeHasStableId("multi.4"));
    require(semantic_menu_node != semantic_menu_nodes.end() &&
                std::find((*semantic_menu_node).actions.begin(),
                          (*semantic_menu_node).actions.end(),
                          SemanticAction::show_menu) != (*semantic_menu_node).actions.end(),
            "object rows must publish a distinct semantic Show Menu action");
    context_id.clear();
    const bool operation_check_54 = (*objects).on_semantic_child_action(
                "multi.4", SemanticAction::show_menu, {});
    require(operation_check_54, "semantic Show Menu must enter the same stable context-request path");
    const bool operation_check_55 = context_id == "multi.4";
    require(operation_check_55, "semantic Show Menu must enter the same stable context-request path");
    pointer_click(window, semantic_center(*objects, "multi.0"),
                  PointerButton::secondary);
    require_selection(*objects, {"multi.0"},
                      "right click outside selection must select its context target");

    const bool operation_check_56 = window.dispatch_key({KeyAction::down, PhysicalKey::a, Modifier::control});
    require(operation_check_56, "Control+A must be consumed by the focused collection");
    require((*objects).selected_ids().size() == 8U,
            "Select All must select every logical enabled item");
    const std::vector<SemanticNode> all_nodes = (*objects).semantic_virtual_children();
    require(std::all_of(all_nodes.begin(), all_nodes.end(),
                        semantic_node_is_selected),
            "every realized member of a multiselection must publish selected semantics");

    const bool operation_check_57 = window.dispatch_key({KeyAction::down, PhysicalKey::right,
                                 Modifier::control});
    require(operation_check_57, "Control+Arrow must move focus without mutating selection");
    const bool operation_check_58 = (*objects).focused_id() == "multi.1";
    require(operation_check_58, "Control+Arrow must move focus without mutating selection");
    const bool operation_check_59 = (*objects).selected_ids().size() == 8U;
    require(operation_check_59, "Control+Arrow must move focus without mutating selection");
    const bool operation_check_60 = window.dispatch_key({KeyAction::down, PhysicalKey::space,
                                 Modifier::control});
    require(operation_check_60, "Control+Space must toggle the focused stable item");
    require_selection(*objects,
                      {"multi.0", "multi.2", "multi.3", "multi.4",
                       "multi.5", "multi.6", "multi.7"},
                      "Control+Space must remove only the focused item");

    const Point background{310.0, 266.0};
    pointer_click(window, background, PointerButton::primary);
    require((*objects).selected_ids().empty() && (*objects).selected_id.read().empty(),
            "plain background click must clear selection without losing focus");

    (*objects).set_selected_ids({"multi.6", "multi.2"}, "multi.6");
    std::reverse(model.begin(), model.end());
    (*objects).set_items(model);
    require_selection(*objects, {"multi.6", "multi.2"},
                      "model replacement must retain selected stable IDs in new visual order");
    (*objects).view_mode = ObjectViewMode::details;
    require_selection(*objects, {"multi.6", "multi.2"},
                      "view projection changes must preserve the complete selection set");
    require(!changes.empty() && changes.back().current_ids.size() == 2U,
            "selection events must carry deterministic previous/current snapshots");
}

void test_shared_command_binding() {
    std::shared_ptr<gui_forms::Command> command = std::make_shared<Command>("view.mode", "Icons  ▼");
    std::shared_ptr<gui_forms::Button> ribbon = make_control<Button>(StableId("ribbon.view"), "stale");
    std::shared_ptr<gui_forms::Button> status = make_control<Button>(StableId("status.view"), "stale");
    CommandBinding ribbon_binding(command, ribbon);
    CommandBinding status_binding(command, status);
    require((*ribbon).text.read() == "Icons  ▼" && (*status).text.read() == "Icons  ▼",
            "shared command must initialize every bound presentation");
    std::string trace{};
    SubscriptionToken invoked = (*command).invoked().subscribe(
        AppendCommandInvocation(trace));
    const bool operation_check_61 = (*ribbon).perform_click();
    require(operation_check_61, "enabled command presentations must accept public click execution");
    const bool operation_check_62 = (*status).perform_click();
    require(operation_check_62, "enabled command presentations must accept public click execution");
    require(trace == "view.mode@ribbon.view\nview.mode@status.view\n",
            "bound presentations must converge on one ordered command path");
    (*command).set_enabled(false);
    const bool operation_check_63 = !(*ribbon).perform_click();
    require(operation_check_63, "disabled command presentation must reject public click execution");
    require(!(*ribbon).enabled.read() && !(*status).enabled.read() &&
                trace == "view.mode@ribbon.view\nview.mode@status.view\n",
            "disabled command state must synchronize and reject execution");

    std::shared_ptr<gui_forms::Command> local_command = std::make_shared<Command>("local.enabled", "Local");
    std::shared_ptr<gui_forms::Button> local_button = make_control<Button>(StableId("local.enabled.button"),
                                             "Local");
    (*local_button).enabled = false;
    CommandBindingOptions local_options{};
    local_options.synchronize_enabled = false;
    CommandBinding local_binding(local_command, local_button, local_options);
    (*local_command).set_enabled(false);
    (*local_command).set_enabled(true);
    require(!(*local_button).enabled.read(),
            "command bindings may preserve locally managed enabled state");

    const CommandBindingOptions legacy_aggregate{false, true, true};
    require(!legacy_aggregate.synchronize_text &&
                legacy_aggregate.synchronize_visibility &&
                legacy_aggregate.synchronize_accessible_description &&
                legacy_aggregate.synchronize_enabled,
            "new command options must preserve the established aggregate field order");
}

std::vector<CorrespondenceItem> correspondence_fixture(std::size_t count) {
    std::vector<CorrespondenceItem> items{};
    items.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        items.push_back({
            "correspondence." + std::to_string(index),
            "Evidence record " + std::to_string(index),
            "Projects / deterministic fixture",
            "Exact stored reason " + std::to_string(index),
            "Matched excerpt for record " + std::to_string(index),
            std::to_string(100U - index % 80U) + "%",
            "Provider information",
            {"Local index", "Text extractor"},
            "Generation 86 · factual fixture evidence",
            index % 4U == 0U ? ObjectGlyph::image : ObjectGlyph::document,
            true,
            index == 12U,
            index == 13U,
        });
    }
    return items;
}

void test_correspondence_virtualization_and_anchor_stability() {
    std::shared_ptr<gui_forms::CorrespondenceView> customization = make_control<CorrespondenceView>(
        StableId("correspondence.customization"));
    (*customization).status_rail_width = 7.0;
    require((*customization).status_rail_width.read() == 7.0,
            "CorrespondenceView must retain explicit status-rail width");
    bool rejected_rail = false;
    try {
        (*customization).status_rail_width = 0.0;
    } catch (const std::invalid_argument&) {
        rejected_rail = true;
    }
    require(rejected_rail,
            "CorrespondenceView must reject nonvisual status-rail widths");

    std::shared_ptr<gui_forms::CorrespondenceView> records = make_control<CorrespondenceView>(
        StableId("correspondence.records"));
    (*records).set_requested_bounds({0.0, 0.0, 820.0, 230.0});
    (*records).set_items(correspondence_fixture(10000U));
    (*records).accessible_name = "Evidence correspondence";
    Window window(records, {820.0, 230.0});
    window.perform_layout();
    require((*records).children().empty(),
            "CorrespondenceView must not allocate one retained control per logical item");
    require((*records).realized_count() <= 8U &&
                (*records).semantic_virtual_children().size() <= 8U,
            "variable-height correspondence realization must remain viewport bounded");

    (*records).scroll_offset = 45.0 * 10.0;
    const double anchored_y = (*(*records).item_bounds("correspondence.10")).y;
    (*records).pinned_id = "correspondence.2";
    require((*records).pinned_id.read() == "correspondence.2" &&
                (*records).expanded("correspondence.2") &&
                std::abs((*(*records).item_bounds("correspondence.10")).y -
                         anchored_y) < 0.01,
            "expanding a row above the viewport must preserve the visible scroll anchor");

    const std::optional<Rect> hover_bounds = (*records).item_bounds("correspondence.12");
    require(hover_bounds && (*hover_bounds).y >= 0.0 &&
                (*hover_bounds).y < 230.0,
            "hover test row must be physically realized");
    const Point hover_center{(*hover_bounds).x + (*hover_bounds).width * .5,
                             (*hover_bounds).y + (*hover_bounds).height * .5};
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, hover_center}));
    require((*records).hovered_id() == "correspondence.12" &&
                !(*records).expanded("correspondence.12") &&
                window.next_wake().has_value(),
            "pointer arrival must schedule rather than immediately fake hover intent");
    const double before_expand = (*(*records).item_bounds("correspondence.12")).y;
    static_cast<void>(window.poll_frame_schedule(*window.next_wake()));
    require((*records).expanded("correspondence.12") &&
                (*records).pinned_id.read() == "correspondence.2" &&
                std::abs((*(*records).item_bounds("correspondence.12")).y -
                         before_expand) < 0.01,
            "hover intent must preserve its pointer target while a distinct pinned row persists");
    static_cast<void>(window.dispatch_pointer(
        {PointerAction::move, PointerButton::none, {900.0, 300.0}}));
    require(!(*records).expanded("correspondence.12") &&
                (*records).expanded("correspondence.2"),
            "pointer leave must collapse only transient inspection");
}

void test_correspondence_keyboard_pin_semantics_and_activation() {
    std::shared_ptr<gui_forms::CorrespondenceView> records = make_control<CorrespondenceView>(
        StableId("correspondence.keyboard"));
    (*records).set_requested_bounds({0.0, 0.0, 820.0, 360.0});
    (*records).set_items(correspondence_fixture(20U));
    (*records).selected_id = "correspondence.10";
    (*records).pinned_id = "correspondence.0";
    Window window(records, {820.0, 360.0});
    const bool operation_check_64 = window.request_focus(records);
    require(operation_check_64, "keyboard focus must expand independently from one persistent pin");
    const bool operation_check_65 = (*records).focused_id() == "correspondence.10";
    require(operation_check_65, "keyboard focus must expand independently from one persistent pin");
    const bool operation_check_66 = (*records).expanded("correspondence.10");
    require(operation_check_66, "keyboard focus must expand independently from one persistent pin");
    const bool operation_check_67 = (*records).expanded("correspondence.0");
    require(operation_check_67, "keyboard focus must expand independently from one persistent pin");
    const bool operation_check_68 = window.dispatch_key({KeyAction::down, PhysicalKey::down});
    require(operation_check_68, "Down must move stable focus/selection and expand the keyboard-active row");
    const bool operation_check_69 = (*records).focused_id() == "correspondence.11";
    require(operation_check_69, "Down must move stable focus/selection and expand the keyboard-active row");
    const bool operation_check_70 = (*records).selected_id.read() == "correspondence.11";
    require(operation_check_70, "Down must move stable focus/selection and expand the keyboard-active row");
    const bool operation_check_71 = (*records).expanded("correspondence.11");
    require(operation_check_71, "Down must move stable focus/selection and expand the keyboard-active row");
    const bool operation_check_72 = window.dispatch_key({KeyAction::down, PhysicalKey::space});
    require(operation_check_72, "Space must replace the sole persistent pin without losing focus");
    const bool operation_check_73 = (*records).pinned_id.read() == "correspondence.11";
    require(operation_check_73, "Space must replace the sole persistent pin without losing focus");
    const bool operation_check_74 = !(*records).expanded("correspondence.0");
    require(operation_check_74, "Space must replace the sole persistent pin without losing focus");

    std::string activated{};
    SubscriptionToken activation = (*records).item_activated().subscribe(
        test_support::RecordValue<std::string>(activated));
    const bool operation_check_75 = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(operation_check_75, "Enter must activate rather than toggle the active correspondence pin");
    const bool operation_check_76 = activated == "correspondence.11";
    require(operation_check_76, "Enter must activate rather than toggle the active correspondence pin");
    const std::vector<SemanticNode> nodes = (*records).semantic_virtual_children();
    const std::vector<SemanticNode>::const_iterator active = std::find_if(
        nodes.begin(), nodes.end(),
        SemanticNodeHasStableId("correspondence.11"));
    require(active != nodes.end() &&
                has_semantic_state((*active).states, SemanticState::expanded) &&
                (*active).children.size() == 5U &&
                (*active).children[0].stable_id ==
                    "correspondence.11.activate" &&
                (*active).children[1].stable_id ==
                    "correspondence.11.percentage" &&
                (*active).children[4].role == SemanticRole::group,
            "expanded virtual semantics must expose default action, factual metric, metadata, excerpt, and provider lanes");
    activated.clear();
    const bool operation_check_77 = (*records).on_semantic_child_action(
                "correspondence.11.activate", SemanticAction::press, {});
    require(operation_check_77, "the explicit correspondence default-action child must share item activation");
    const bool operation_check_78 = activated == "correspondence.11";
    require(operation_check_78, "the explicit correspondence default-action child must share item activation");
    const bool operation_check_79 = (*records).on_semantic_child_action(
                "correspondence.12", SemanticAction::expand, {});
    require(operation_check_79, "semantic expansion must enter the same one-pin state path for unavailable evidence");
    const bool operation_check_80 = (*records).pinned_id.read() == "correspondence.12";
    require(operation_check_80, "semantic expansion must enter the same one-pin state path for unavailable evidence");
    const bool operation_check_81 = (*records).expanded("correspondence.12");
    require(operation_check_81, "semantic expansion must enter the same one-pin state path for unavailable evidence");
}

PartGeometry correspondence_item_part(const Control& view, std::size_t item, std::string_view name) {
    const ItemSystemDeclaration& system = view.anatomy().item_systems.front();
    std::vector<PartGeometry> geometry(system.parts.size());
    view.fill_realized_item_geometry(item, geometry);
    for (std::size_t index = 0U; index < system.parts.size(); ++index) {
        if (system.parts[index].name == name) return geometry[index];
    }
    return {};
}

// Sweep defect D9: in a narrow card the title, the state word and the
// metric share one line and must not overlap; the secondary line ends
// before the metric; the excerpt box stays inside a short expanded card.
void test_correspondence_narrow_card_layout() {
    std::shared_ptr<gui_forms::CorrespondenceView> records = make_control<CorrespondenceView>(
        StableId("correspondence.narrow"));
    (*records).set_requested_bounds({0.0, 0.0, 280.0, 300.0});
    (*records).compact_height = 44.0;
    (*records).expanded_height = 124.0;
    (*records).set_items(correspondence_fixture(3U));
    (*records).pinned_id = "correspondence.0";
    Window window(records, {280.0, 300.0});
    window.flush();
    require((*records).expanded("correspondence.0"), "the pinned card must be expanded");
    const PartGeometry card = correspondence_item_part(*records, 0U, "card");
    const PartGeometry title = correspondence_item_part(*records, 0U, "title");
    const PartGeometry secondary = correspondence_item_part(*records, 0U, "secondary");
    const PartGeometry metric = correspondence_item_part(*records, 0U, "metric");
    const PartGeometry evidence = correspondence_item_part(*records, 0U, "evidence");
    require(title.present && metric.present && secondary.present && evidence.present,
            "a narrow expanded card must publish its title, secondary, metric and evidence parts");
    // The state word sits between the title and the metric, so the title
    // ends at least one gap and the word's width before the metric.
    require(title.bounds.x + title.bounds.width + 16.0 < metric.bounds.x,
            "a pinned card's title must end before the state word and the metric");
    require(secondary.bounds.x + secondary.bounds.width < metric.bounds.x,
            "the secondary line must end before the metric");
    require(metric.bounds.x + metric.bounds.width <= card.bounds.x + card.bounds.width,
            "the metric must stay inside the card");
    require(evidence.bounds.y + evidence.bounds.height <= card.bounds.y + card.bounds.height,
            "the excerpt box must stay inside the expanded card");
}

void test_correspondence_background_pointer_contract() {
    std::shared_ptr<gui_forms::CorrespondenceView> records =
        make_control<CorrespondenceView>(StableId("correspondence.background"));
    (*records).set_requested_bounds({0.0, 0.0, 820.0, 360.0});
    (*records).set_items(correspondence_fixture(1U));
    (*records).selected_id = "correspondence.0";
    Window window(records, {820.0, 360.0});
    window.perform_layout();

    std::vector<ObjectContextRequest> requests{};
    SubscriptionToken context = (*records).context_requested().subscribe(
        test_support::PushBack<std::vector<ObjectContextRequest>, const ObjectContextRequest&>(requests));
    const Rect bounds = (*records).absolute_bounds();
    const Point background{bounds.x + bounds.width * .5,
                           bounds.y + bounds.height - 12.0};
    const bool operation_check_82 = window.dispatch_pointer(
                {PointerAction::down, PointerButton::primary, background});
    require(operation_check_82, "primary correspondence background click must focus the collection and clear selection without opening a menu");
    const bool operation_check_83 = window.dispatch_pointer(
                    {PointerAction::up, PointerButton::primary, background});
    require(operation_check_83, "primary correspondence background click must focus the collection and clear selection without opening a menu");
    const bool operation_check_84 = (*records).selected_id.read().empty();
    require(operation_check_84, "primary correspondence background click must focus the collection and clear selection without opening a menu");
    const bool operation_check_85 = requests.empty();
    require(operation_check_85, "primary correspondence background click must focus the collection and clear selection without opening a menu");
    const bool operation_check_86 = window.focused_control() == records;
    require(operation_check_86, "primary correspondence background click must focus the collection and clear selection without opening a menu");

    (*records).selected_id = "correspondence.0";
    const bool operation_check_87 = window.dispatch_pointer(
                {PointerAction::down, PointerButton::secondary, background});
    require(operation_check_87, "secondary correspondence background click must clear selection and emit one exact empty-target context request");
    const bool operation_check_88 = window.dispatch_pointer(
                    {PointerAction::up, PointerButton::secondary, background});
    static_cast<void>(window.dispatch_context_request(ContextRequestOrigin::pointer, background));
    require(operation_check_88, "secondary correspondence background click must clear selection and emit one exact empty-target context request");
    const bool operation_check_89 = (*records).selected_id.read().empty();
    require(operation_check_89, "secondary correspondence background click must clear selection and emit one exact empty-target context request");
    const bool operation_check_90 = requests.size() == 1U;
    require(operation_check_90, "secondary correspondence background click must clear selection and emit one exact empty-target context request");
    const bool operation_check_91 = requests.front().stable_id.empty();
    require(operation_check_91, "secondary correspondence background click must clear selection and emit one exact empty-target context request");
    const bool operation_check_92 = requests.front().screen_position.x == background.x;
    require(operation_check_92, "secondary correspondence background click must clear selection and emit one exact empty-target context request");
    const bool operation_check_93 = requests.front().screen_position.y == background.y;
    require(operation_check_93, "secondary correspondence background click must clear selection and emit one exact empty-target context request");
}

std::vector<ObjectDetailsColumn> details_test_columns() {
    std::vector<ObjectDetailsColumn> result{};
    result.push_back({.id = {"name"}, .label = "Object name", .width = 150.0});
    result.push_back({.id = {"fact"}, .label = "Observed fact", .width = 120.0,
                      .alignment = ObjectColumnAlignment::right});
    result.push_back({.id = {"state"}, .label = "Availability", .width = 100.0, .sortable = false});
    return result;
}

std::vector<ObjectViewItem> details_test_items(std::size_t count) {
    std::vector<ObjectViewItem> result{};
    result.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) {
        const std::string number = std::to_string(index);
        ObjectViewItem item{};
        item.stable_id = "detail." + number;
        item.name = "Object " + number;
        // Intentionally not positional: publication must normalize by ID.
        item.cells.push_back({{"state"}, "Unavailable", ObjectCellAvailability::unavailable});
        item.cells.push_back({{"name"}, "Unicode é 👩‍💻 long factual object " + number});
        item.cells.push_back({{"fact"}, number});
        result.push_back(std::move(item));
    }
    return result;
}

struct DetailsSortRecorder final {
    std::vector<ObjectDetailsSort>& requests;
    void operator()(const ObjectDetailsSort& request) const { requests.push_back(request); }
};

struct DetailsReplacingSortHandler final {
    ObjectView& view;
    std::string& observed_column;
    void operator()(const ObjectDetailsSort& request) const {
        view.set_details_model({}, {});
        observed_column = request.column.value;
    }
};

enum class DetailsInvalidModelCase : std::uint8_t {
    duplicate_row, unknown_cell, invalid_availability, invalid_alignment,
    duplicate_column, invalid_width, invalid_label, missing_disclosure,
};

enum class DetailsEntryMutation : std::uint8_t {
    replace, replace_same_shape, dispose, detach, resize, revoke, transfer,
};

struct DetailsEntryMutator final {
    ObjectView& view;
    Panel& parent;
    Window& window;
    DetailsEntryMutation mutation;
    bool& invoked;

    void apply() const {
        if (invoked) return;
        invoked = true;
        if (mutation == DetailsEntryMutation::replace) view.set_details_model({}, {});
        else if (mutation == DetailsEntryMutation::replace_same_shape) {
            view.set_details_model(details_test_columns(), details_test_items(2U));
        }
        else if (mutation == DetailsEntryMutation::dispose) view.dispose();
        else if (mutation == DetailsEntryMutation::detach) {
            const Control::Ptr removed = parent.remove_child(view.runtime_id());
        } else if (mutation == DetailsEntryMutation::resize) {
            view.set_details_column_width({"name"}, 180.0);
        } else if (mutation == DetailsEntryMutation::transfer) {
            window.capture_pointer(parent.shared_from_this(), 1U);
        } else window.release_pointer();
    }

    void operator()(const bool focused) const {
        if (focused) apply();
    }

    void operator()(const PointerCaptureChange& change) const {
        if (change.captured && change.control_id == view.runtime_id()) apply();
    }
};

void test_details_header_entry_reentrancy() {
    const std::array<DetailsEntryMutation, 7U> mutations{
        DetailsEntryMutation::replace, DetailsEntryMutation::replace_same_shape,
        DetailsEntryMutation::dispose, DetailsEntryMutation::detach,
        DetailsEntryMutation::resize, DetailsEntryMutation::revoke, DetailsEntryMutation::transfer};
    for (const bool capture_callback : {false, true}) {
        for (const DetailsEntryMutation mutation : mutations) {
            if (!capture_callback && (mutation == DetailsEntryMutation::revoke ||
                mutation == DetailsEntryMutation::transfer)) continue;
            const std::shared_ptr<Panel> root = make_control<Panel>(StableId("details.entry.root"));
            const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.entry"));
            ObjectView& view = *owner;
            view.set_requested_bounds({0.0, 0.0, 230.0, 190.0});
            view.view_mode = ObjectViewMode::details;
            view.set_details_model(details_test_columns(), details_test_items(2U));
            (*root).add_child(owner);
            Window window(root, {230.0, 190.0});
            window.flush();
            bool invoked = false;
            SubscriptionToken subscription{};
            const DetailsEntryMutator mutator{view, *root, window, mutation, invoked};
            if (capture_callback) {
                window.capture_pointer(root, 1U);
                subscription = window.pointer_capture_changed().subscribe(mutator);
            } else subscription = view.focus_observed().subscribe(mutator);
            PointerEvent down{PointerAction::down, PointerButton::primary, {154.0, 15.0}};
            // Direct delivery isolates ObjectView's own callback boundaries.
            view.on_pointer(down);
            require(invoked && down.handled && !view.has_pointer_capture(),
                "focus/capture mutation must retire the header-edge press without stale capture");
            if (mutation == DetailsEntryMutation::transfer) {
                require((*root).has_pointer_capture(), "retired gesture must preserve another owner's capture");
            }
            if (mutation == DetailsEntryMutation::resize || mutation == DetailsEntryMutation::revoke ||
                mutation == DetailsEntryMutation::replace_same_shape || mutation == DetailsEntryMutation::transfer) {
                PointerEvent move{PointerAction::move, PointerButton::none, {204.0, 15.0}};
                view.on_pointer(move);
                const double expected = mutation == DetailsEntryMutation::resize ? 180.0 : 150.0;
                require(view.details_columns()[0U].width == expected,
                    "retired callback gesture must not resume a resize on pointer move");
            }
        }
    }
}

enum class DetailsModeMutation : std::uint8_t { dispose, detach, mode, model };

struct DetailsModeReleaseMutator final {
    ObjectView& view;
    Panel& parent;
    DetailsModeMutation mutation;
    bool& invoked;
    bool& saw_committed_mode;

    void operator()(const PointerCaptureChange& change) const {
        if (change.captured || invoked) return;
        invoked = true;
        saw_committed_mode = view.view_mode.read() == ObjectViewMode::icons && !view.has_pointer_capture();
        if (mutation == DetailsModeMutation::dispose) view.dispose();
        else if (mutation == DetailsModeMutation::detach) {
            const Control::Ptr removed = parent.remove_child(view.runtime_id());
        } else if (mutation == DetailsModeMutation::mode) {
            view.view_mode = ObjectViewMode::details;
        } else {
            view.set_details_model(details_test_columns(), details_test_items(3U));
            view.set_top_row(1U);
        }
    }
};

void test_details_mode_release_reentrancy() {
    bool all_passed = true;
    for (const DetailsModeMutation mutation : {DetailsModeMutation::dispose,
        DetailsModeMutation::detach, DetailsModeMutation::mode, DetailsModeMutation::model}) {
        const std::shared_ptr<Panel> root = make_control<Panel>(StableId("details.mode.root"));
        const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.mode"));
        ObjectView& view = *owner;
        view.set_requested_bounds({0.0, 0.0, 230.0, 190.0});
        view.view_mode = ObjectViewMode::details;
        view.set_details_model(details_test_columns(), details_test_items(20U));
        (*root).add_child(owner);
        Window window(root, {230.0, 190.0});
        window.flush();
        view.set_top_row(5U);
        PointerEvent down{PointerAction::down, PointerButton::primary, {154.0, 15.0}};
        view.on_pointer(down);
        require(view.has_pointer_capture(), "mode fixture must start with resize capture");
        bool invoked = false;
        bool saw_committed_mode = false;
        SubscriptionToken observer = window.pointer_capture_changed().subscribe(
            DetailsModeReleaseMutator{view, *root, mutation, invoked, saw_committed_mode});
        bool threw = false;
        try { view.view_mode = ObjectViewMode::icons; }
        catch (const std::logic_error&) { threw = true; }
        const ObjectViewMode expected_mode = mutation == DetailsModeMutation::mode ?
            ObjectViewMode::details : ObjectViewMode::icons;
        bool passed = invoked && saw_committed_mode && !threw &&
            view.view_mode.read() == expected_mode && !view.has_pointer_capture();
        if (mutation == DetailsModeMutation::model) {
            passed = passed && view.items().size() == 3U && view.top_row() == 1U;
        }
        if (mutation == DetailsModeMutation::dispose) passed = passed && !view.is_alive();
        if (mutation == DetailsModeMutation::detach) passed = passed && view.attached_window() == nullptr;
        if (!passed) {
            std::cerr << "Details mode release mutation=" << static_cast<unsigned int>(mutation)
                      << " committed=" << saw_committed_mode << " threw=" << threw << '\n';
            all_passed = false;
        }
    }
    require(all_passed, "mode setter must publish before capture callbacks and preserve nested changes");
}

enum class DetailsSortReleaseMutation : std::uint8_t { none, dispose, detach, model, mode };

struct DetailsSortReleaseObserver final {
    ObjectView& view;
    Panel& parent;
    DetailsSortReleaseMutation mutation;
    bool& released;

    void operator()(const PointerCaptureChange& change) const {
        if (change.captured || released) return;
        released = true;
        if (mutation == DetailsSortReleaseMutation::dispose) view.dispose();
        else if (mutation == DetailsSortReleaseMutation::detach) {
            const Control::Ptr removed = parent.remove_child(view.runtime_id());
        } else if (mutation == DetailsSortReleaseMutation::model) {
            view.set_details_model(details_test_columns(), details_test_items(2U));
        } else if (mutation == DetailsSortReleaseMutation::mode) view.view_mode = ObjectViewMode::icons;
    }
};

struct DetailsRetiredSortObserver final {
    ObjectView& view;
    bool& released;
    std::size_t& calls;

    void operator()(const ObjectDetailsSort& request) const {
        require(released && !view.has_pointer_capture() && request.column.value == "name",
            "sort must own its identity and follow resize capture retirement");
        ++calls;
    }
};

void test_details_sort_retires_capture() {
    for (const DetailsSortReleaseMutation mutation : {DetailsSortReleaseMutation::none,
        DetailsSortReleaseMutation::dispose, DetailsSortReleaseMutation::detach,
        DetailsSortReleaseMutation::model, DetailsSortReleaseMutation::mode}) {
        const std::shared_ptr<Panel> root = make_control<Panel>(StableId("details.sort.release.root"));
        const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.sort.release"));
        ObjectView& view = *owner;
        view.set_requested_bounds({0.0, 0.0, 230.0, 190.0});
        view.view_mode = ObjectViewMode::details;
        view.set_details_model(details_test_columns(), details_test_items(8U));
        (*root).add_child(owner);
        Window window(root, {230.0, 190.0});
        window.flush();
        PointerEvent down{PointerAction::down, PointerButton::primary, {154.0, 15.0}};
        view.on_pointer(down);
        require(view.has_pointer_capture(), "sort fixture requires active header resize");
        bool released = false;
        std::size_t calls = 0U;
        SubscriptionToken capture_observer = window.pointer_capture_changed().subscribe(
            DetailsSortReleaseObserver{view, *root, mutation, released});
        SubscriptionToken sort_observer = view.sort_requested().subscribe(
            DetailsRetiredSortObserver{view, released, calls});
        KeyEvent enter{KeyAction::down, PhysicalKey::enter};
        view.on_key(enter);
        const std::size_t expected_calls = mutation == DetailsSortReleaseMutation::none ? 1U : 0U;
        require(enter.handled && released && !view.has_pointer_capture() && calls == expected_calls,
            "Enter during resize must retire capture and suppress a sort whose release context changed");
        if (mutation == DetailsSortReleaseMutation::none || mutation == DetailsSortReleaseMutation::model) {
            PointerEvent move{PointerAction::move, PointerButton::none, {204.0, 15.0}};
            view.on_pointer(move);
            require(view.details_columns()[0U].width == 150.0, "sort retirement must prevent resumed resize");
        }
    }
}

struct DetailsCommitObserverFailure final {};

struct DetailsCommitObserver final {
    const ObjectView& view;
    bool& called;
    void operator()(const ObjectSelectionChange&) const {
        called = true;
        require(view.items().front().stable_id == "detail.7" &&
            view.details_sort.read().column.value == "replacement-name" &&
            view.details_sort.read().direction == ObjectSortDirection::descending &&
            view.selected_ids().front() == "detail.6",
            "postcommit listener must see reordered rows and their accepted indicator together");
        throw DetailsCommitObserverFailure{};
    }
};

void test_details_model_with_accepted_sort() {
    const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.accepted.commit"));
    ObjectView& view = *owner;
    view.set_requested_bounds({0.0, 0.0, 230.0, 190.0});
    view.view_mode = ObjectViewMode::details;
    view.set_details_model(details_test_columns(), details_test_items(8U), {{"name"}, ObjectSortDirection::ascending});
    view.set_selected_ids({"detail.2", "detail.6"}, "detail.6");
    Window window(owner, {230.0, 190.0});
    window.flush();
    const bool focused = window.request_focus(owner);
    require(focused, "atomic sort fixture must focus");
    view.set_top_row(2U);
    const std::array<ObjectDetailsSort, 4U> invalid_states{{
        {{"missing"}, ObjectSortDirection::ascending},
        {{"state"}, ObjectSortDirection::ascending},
        {{"name"}, static_cast<ObjectSortDirection>(255)},
        {{}, static_cast<ObjectSortDirection>(255)},
    }};
    for (const ObjectDetailsSort& invalid : invalid_states) {
        bool rejected = false;
        try { view.set_details_model(details_test_columns(), details_test_items(3U), invalid); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && view.items().size() == 8U && view.items().front().stable_id == "detail.0" &&
            view.details_sort.read().column.value == "name" && view.details_sort.read().direction == ObjectSortDirection::ascending &&
            view.selected_id.read() == "detail.6" && view.selected_ids().size() == 2U &&
            view.focused_id() == "detail.6" && view.selection_anchor_id() == "detail.6" && view.top_row() == 2U,
            "invalid accepted state must preserve old model, indicator, selection, focus, anchor and top");
    }
    std::vector<ObjectDetailsColumn> columns = details_test_columns();
    columns[0U].id.value = "replacement-name";
    std::vector<ObjectViewItem> items = details_test_items(8U);
    for (ObjectViewItem& item : items) {
        for (ObjectDetailsCell& cell : item.cells) {
            if (cell.column.value == "name") cell.column.value = "replacement-name";
        }
    }
    std::reverse(items.begin(), items.end());
    bool called = false;
    SubscriptionToken observer = view.selection_changed().subscribe(DetailsCommitObserver{view, called});
    bool threw = false;
    try {
        view.set_details_model(std::move(columns), std::move(items), {{"replacement-name"}, ObjectSortDirection::descending});
    } catch (const DetailsCommitObserverFailure&) { threw = true; }
    require(threw && called && view.items().front().stable_id == "detail.7" &&
        view.details_sort.read().column.value == "replacement-name" &&
        view.details_sort.read().direction == ObjectSortDirection::descending,
        "throwing listener cannot leave the new model with the old accepted indicator");
    observer.disconnect();
    view.set_details_model(details_test_columns(), details_test_items(8U), {{"fact"}, ObjectSortDirection::descending});
    view.set_details_model(details_test_columns(), details_test_items(8U));
    require(view.details_sort.read().column.value == "fact" && view.details_sort.read().direction == ObjectSortDirection::descending,
        "two-argument replacement retains surviving accepted sort");
    view.set_details_model(details_test_columns(), details_test_items(8U), {});
    require(view.details_sort.read().column.value.empty(), "explicit empty accepted sort clears indicator");
}

void test_details_transaction_and_identity() {
    const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.transaction"));
    ObjectView& view = *owner;
    view.set_requested_bounds({0.0, 0.0, 280.0, 190.0});
    view.view_mode = ObjectViewMode::details;
    view.set_details_model(details_test_columns(), details_test_items(20U));
    Window window(owner, {280.0, 190.0});
    view.set_selected_ids({"detail.3", "detail.7"}, "detail.7");
    const bool focused = window.request_focus(owner);
    require(focused, "Details fixture must accept focus");
    const bool moved = window.dispatch_key({KeyAction::down, PhysicalKey::down, Modifier::control});
    require(moved && view.focused_id() == "detail.8", "Details must support independent focus");
    view.set_top_row(5U);
    std::vector<ObjectViewItem> reversed = details_test_items(20U);
    std::reverse(reversed.begin(), reversed.end());
    view.set_items(std::move(reversed));
    window.flush();
    require(view.selected_id.read() == "detail.7" && view.selection_anchor_id() == "detail.7" &&
        view.focused_id() == "detail.8" && view.items()[view.top_row()].stable_id == "detail.5",
        "replacement must preserve independent primary, anchor, focus and top-visible identity");
    require(view.items().front().cells.front().column.value == "name",
        "ID-keyed cells must normalize independently of input order");
    view.set_selected_ids({"detail.3", "detail.7"});
    require(view.selected_id.read() == "detail.7" && view.selection_anchor_id() == "detail.7",
        "a caller republishing selection without explicit primary must preserve surviving primary and anchor");
    std::vector<ObjectViewItem> malformed = details_test_items(2U);
    malformed[1U].cells[0U].column.value = "name";
    bool rejected = false;
    try { view.set_items(std::move(malformed)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && view.items().size() == 20U && view.selected_id.read() == "detail.7" &&
        view.items()[view.top_row()].stable_id == "detail.5", "duplicate cell replacement must leave old state intact");
    std::vector<ObjectDetailsColumn> invalid_columns = details_test_columns();
    invalid_columns[1U].width = std::numeric_limits<double>::quiet_NaN();
    rejected = false;
    try { view.set_details_model(std::move(invalid_columns), details_test_items(1U)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && view.details_columns()[1U].width == 120.0 && view.items().size() == 20U,
        "nonfinite widths must fail before model publication");
    malformed = details_test_items(1U);
    malformed[0U].cells[1U].text = std::string(1U, static_cast<char>(0xFF));
    rejected = false;
    try { view.set_items(std::move(malformed)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && view.items().size() == 20U, "malformed UTF-8 cells must leave model intact");
    malformed = details_test_items(1U);
    malformed[0U].cells.pop_back();
    rejected = false;
    try { view.set_items(std::move(malformed)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && view.items().size() == 20U, "missing cells must fail complete replacement");
    std::vector<ObjectViewItem> excessive = details_test_items(1024U);
    for (ObjectViewItem& item : excessive) item.cells[1U].text.assign(65536U, 'x');
    rejected = false;
    try { view.set_items(std::move(excessive)); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && view.items().size() == 20U && view.selected_id.read() == "detail.7" &&
        view.items()[view.top_row()].stable_id == "detail.5",
        "aggregate text above the development guard must preserve the complete prior model");
    // Each independent invalid input must fail before any retained publication.
    const std::array<DetailsInvalidModelCase, 8U> invalid_cases{
        DetailsInvalidModelCase::duplicate_row, DetailsInvalidModelCase::unknown_cell,
        DetailsInvalidModelCase::invalid_availability, DetailsInvalidModelCase::invalid_alignment,
        DetailsInvalidModelCase::duplicate_column, DetailsInvalidModelCase::invalid_width,
        DetailsInvalidModelCase::invalid_label, DetailsInvalidModelCase::missing_disclosure};
    for (const DetailsInvalidModelCase invalid_case : invalid_cases) {
        std::vector<ObjectDetailsColumn> bad_columns = details_test_columns();
        std::vector<ObjectViewItem> bad_items = details_test_items(2U);
        if (invalid_case == DetailsInvalidModelCase::duplicate_row) bad_items[1U].stable_id = bad_items[0U].stable_id;
        else if (invalid_case == DetailsInvalidModelCase::unknown_cell) bad_items[0U].cells[0U].column.value = "unknown";
        else if (invalid_case == DetailsInvalidModelCase::invalid_availability) bad_items[0U].cells[0U].availability = static_cast<ObjectCellAvailability>(255U);
        else if (invalid_case == DetailsInvalidModelCase::invalid_alignment) bad_columns[0U].alignment = static_cast<ObjectColumnAlignment>(255U);
        else if (invalid_case == DetailsInvalidModelCase::duplicate_column) bad_columns[1U].id = bad_columns[0U].id;
        else if (invalid_case == DetailsInvalidModelCase::invalid_width) bad_columns[0U].width = 0.0;
        else if (invalid_case == DetailsInvalidModelCase::invalid_label) bad_columns[0U].label = std::string(1U, static_cast<char>(0xFF));
        else bad_items[0U].cells[0U].text.clear();
        rejected = false;
        try { view.set_details_model(std::move(bad_columns), std::move(bad_items)); }
        catch (const std::invalid_argument&) { rejected = true; }
        require(rejected && view.items().size() == 20U && view.selected_id.read() == "detail.7" &&
            view.items()[view.top_row()].stable_id == "detail.5", "invalid Details model must preserve previous state");
    }
    view.view_mode = ObjectViewMode::icons;
    view.view_mode = ObjectViewMode::details;
    require(view.selected_id.read() == "detail.7" && view.focused_id() == "detail.8" &&
        view.selection_anchor_id() == "detail.7", "mode changes must preserve independent selection state");
    const bool offscreen_context = view.on_semantic_child_action("detail.19", SemanticAction::show_menu, {});
    require(offscreen_context, "offscreen semantic action must safely reveal its item");
}

void test_details_scroll_fills_available_page() {
    const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.full-page"));
    ObjectView& view = *owner;
    view.set_requested_bounds({0.0, 0.0, 400.0, 300.0});
    view.view_mode = ObjectViewMode::details;
    view.set_details_model(details_test_columns(), details_test_items(3U));
    Window window(owner, {400.0, 300.0});
    window.flush();
    std::vector<ObjectViewItem> reversed = details_test_items(3U);
    std::reverse(reversed.begin(), reversed.end());
    view.set_details_model(details_test_columns(), std::move(reversed));
    require(view.top_row() == 0U,
        "sorting a short folder must not strand the old first identity at the bottom of an empty page");
    view.set_top_row(2U);
    require(view.top_row() == 0U, "scrolling a short folder must retain the full page");
    view.set_details_model(details_test_columns(), details_test_items(30U));
    view.set_top_row(29U);
    const std::size_t before_growth = view.top_row();
    require(before_growth > 0U && before_growth < 29U, "long folders must fill their final page");
    window.resize({400.0, 600.0});
    window.flush();
    require(view.top_row() < before_growth, "growing the viewport must expose earlier rows to fill it");
    view.set_details_model(details_test_columns(), details_test_items(3U));
    require(view.top_row() == 0U, "shrinking the model must retire an obsolete scroll offset");
    view.details_row_height = 28.0;
    window.resize({400.0, 102.0});
    window.flush();
    window.request_focus(owner);
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::home}));
    static_cast<void>(window.dispatch_key({KeyAction::down, PhysicalKey::down}));
    const bool moved = window.dispatch_key({KeyAction::down, PhysicalKey::down});
    std::cout << "Details fractional row: handled=" << moved << " top=" << view.top_row()
              << " selected=" << view.selected_id.read() << " height=" << view.committed_arranged_bounds().height << '\n';
    require(moved && view.top_row() == 1U && view.selected_id.read() == "detail.2",
        "Down must scroll the clipped final row fully into view");
    view.set_top_row(0U);
    const bool ended = window.dispatch_key({KeyAction::down, PhysicalKey::end});
    require(ended && view.top_row() == 1U,
        "End must not treat a partial final row as fully visible");
}

void test_details_keyboard_sort_with_pending_layout() {
    const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.pending-layout"));
    ObjectView& view = *owner;
    view.set_requested_bounds({0.0, 0.0, 230.0, 190.0});
    view.view_mode = ObjectViewMode::details;
    view.set_details_model(details_test_columns(), details_test_items(8U));
    Window window(owner, {230.0, 190.0});
    std::vector<ObjectDetailsSort> requests{};
    SubscriptionToken subscription = view.sort_requested().subscribe(DetailsSortRecorder{requests});
    window.request_focus(owner);
    const bool focused = window.dispatch_key({KeyAction::down, PhysicalKey::f6});
    view.invalidate(Dirty::measure | Dirty::arrange);
    const bool handled = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(focused && handled && requests.size() == 1U && requests.front().column == view.details_columns().front().id,
            "pending layout must resolve before keyboard sort captures its context revision");
}

void test_details_semantic_item_returns_keyboard_to_body() {
    const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.semantic-focus"));
    ObjectView& view = *owner;
    view.set_requested_bounds({0.0, 0.0, 400.0, 190.0});
    view.view_mode = ObjectViewMode::details;
    view.set_details_model(details_test_columns(), details_test_items(3U));
    Window window(owner, {400.0, 190.0});
    window.flush();
    const bool focused = window.request_focus(owner);
    require(focused, "Details fixture must receive keyboard focus");
    const std::array<SemanticAction, 4U> actions{
        SemanticAction::select, SemanticAction::focus,
        SemanticAction::press, SemanticAction::show_menu};
    for (const SemanticAction action : actions) {
        const bool header = window.dispatch_key({KeyAction::down, PhysicalKey::f6});
        require(header, "F6 must enter the Details header before each semantic item action");
        const bool selected = window.perform_semantic_action("detail.1", action);
        require(selected && view.focused_id() == "detail.1",
            "semantic item action must focus the addressed row");
        const std::vector<SemanticNode> nodes = view.semantic_virtual_children();
        bool row_focused = false;
        for (const SemanticNode& node : nodes) {
            if (node.stable_id == "detail.1") {
                row_focused = has_semantic_state(node.states, SemanticState::focused);
                break;
            }
        }
        require(row_focused, "semantic item action must retire internal header focus");
        const bool consumed = window.dispatch_key({KeyAction::down, PhysicalKey::f2});
        require(!consumed, "item-focused Details must leave F2 available to the application accelerator");
    }
}

// A sortable column's header publishes press, which sorts as a click does
// (stage A gap 2); a header that does not sort publishes no action and
// refuses one.
void test_details_header_semantic_press() {
    const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.header-press"));
    ObjectView& view = *owner;
    view.set_requested_bounds({0.0, 0.0, 400.0, 190.0});
    view.view_mode = ObjectViewMode::details;
    view.set_details_model(details_test_columns(), details_test_items(3U));
    Window window(owner, {400.0, 190.0});
    window.flush();
    std::vector<ObjectDetailsSort> requests{};
    SubscriptionToken subscription = view.sort_requested().subscribe(DetailsSortRecorder{requests});
    const std::vector<SemanticNode> nodes = view.semantic_virtual_children();
    const SemanticNode* sortable = nullptr;
    const SemanticNode* fixed = nullptr;
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == "details.header-press.column.fact") sortable = &node;
        if (node.stable_id == "details.header-press.column.state") fixed = &node;
    }
    require(sortable != nullptr && fixed != nullptr && (*sortable).role == SemanticRole::column_header &&
                (*sortable).actions == std::vector<SemanticAction>{SemanticAction::press} &&
                (*fixed).actions.empty(),
            "a sortable header offers press and a fixed one offers nothing");
    const bool pressed = window.perform_semantic_action("details.header-press.column.fact", SemanticAction::press);
    require(pressed && requests.size() == 1U && requests[0U].column.value == "fact" &&
                requests[0U].direction == ObjectSortDirection::ascending && window.focused_control() == owner,
            "pressing a sortable header focuses the view and requests its sort");
    const bool refused = window.perform_semantic_action("details.header-press.column.state", SemanticAction::press);
    const bool selected = window.perform_semantic_action("details.header-press.column.fact", SemanticAction::select);
    require(!refused && !selected && requests.size() == 1U,
            "a fixed header, or an action other than press, is refused");
}

void test_details_header_input_resize_and_cache() {
    const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.input"));
    ObjectView& view = *owner;
    view.set_requested_bounds({0.0, 0.0, 230.0, 190.0});
    view.view_mode = ObjectViewMode::details;
    view.set_details_model(details_test_columns(), details_test_items(8U));
    Window window(owner, {230.0, 190.0});
    std::vector<ObjectDetailsSort> requests{};
    SubscriptionToken subscription = view.sort_requested().subscribe(DetailsSortRecorder{requests});
    view.selected_id = "detail.2";
    pointer_click(window, {40.0, 15.0}, PointerButton::primary);
    require(requests.size() == 1U && requests[0U].column.value == "name" &&
        view.details_sort.read().column.value.empty() && view.selected_id.read() == "detail.2",
        "header sort request must not change accepted order state or body selection");
    view.details_sort = requests.front();
    pointer_click(window, {40.0, 15.0}, PointerButton::primary);
    require(requests.size() == 2U && requests.back().direction == ObjectSortDirection::descending,
        "accepted ascending state must request descending on next activation");
    require(view.item_id_at({40.0, 15.0}).empty(), "header coordinates must not resolve to an item");
    const bool pressed = window.dispatch_pointer({PointerAction::down, PointerButton::primary, {154.0, 15.0}});
    const bool dragged = window.dispatch_pointer({PointerAction::move, PointerButton::none, {194.0, 15.0}});
    const bool released = window.dispatch_pointer({PointerAction::up, PointerButton::primary, {194.0, 15.0}});
    require(pressed && dragged && released && view.details_columns()[0U].width == 190.0 && requests.size() == 2U,
        "header edge drag must resize without sorting or selecting rows");
    const bool last_header = window.dispatch_key({KeyAction::down, PhysicalKey::end});
    require(last_header && view.horizontal_offset() > 0.0, "keyboard End must reveal the last header horizontally");
    const double old_width = view.details_columns()[2U].width;
    const double previous_offset = view.horizontal_offset();
    const bool history_chord = window.dispatch_key({KeyAction::down, PhysicalKey::right, Modifier::alt});
    require(!history_chord && view.horizontal_offset() == previous_offset &&
            view.details_columns()[2U].width == old_width,
            "Alt history chord must remain available to the consumer");
    const bool resized = window.dispatch_key({KeyAction::down, PhysicalKey::right, Modifier::alt | Modifier::shift});
    require(resized && view.details_columns()[2U].width == old_width + 8.0,
        "keyboard Alt+Shift+Right must adjust the focused column");
    const bool unsortable = window.dispatch_key({KeyAction::down, PhysicalKey::enter});
    require(unsortable && requests.size() == 2U, "unsortable headers must not fabricate requests");
    const bool body = window.dispatch_key({KeyAction::down, PhysicalKey::escape});
    require(body, "Escape must leave header focus");
    view.set_horizontal_offset(0.0);
    pointer_click(window, {60.0, 44.0}, PointerButton::primary);
    require(view.selected_id.read() == "detail.0" && requests.size() == 2U,
        "body hit geometry must subtract fixed header height");
    ImageRecordingPainter first{};
    window.paint(first, {0.0, 0.0, 230.0, 190.0});
    require(view.details_paint_work().text_preparations > 0U, "first Details paint must prepare visible labels");
    bool saw_header = false;
    bool saw_ellipsis = false;
    for (const std::string& text : first.texts) {
        require(validate_utf8(text).valid(), "Details elision must preserve UTF-8");
        if (text == "Object name") saw_header = true;
        if (text.ends_with("…")) {
            saw_ellipsis = true;
            if (text.starts_with("Unicode")) {
                const std::string& source = view.items()[0U].cells[0U].text;
                const TextStore store(source);
                const std::size_t prefix_bytes = text.size() - std::string_view("…").size();
                bool boundary = false;
                for (std::size_t cluster = 0U; cluster <= store.grapheme_count().value(); ++cluster) {
                    if (store.utf8_offset(GraphemeIndex(cluster)).value() == prefix_bytes) boundary = true;
                }
                require(boundary, "Details elision must preserve combining and ZWJ grapheme boundaries");
            }
        }
    }
    require(saw_header && saw_ellipsis, "Details must paint actual headers and elide long factual text");
    ImageRecordingPainter second{};
    view.invalidate(Dirty::paint);
    window.paint(second, {0.0, 0.0, 230.0, 190.0});
    require(view.details_paint_work().text_preparations == 0U, "unchanged Details repaint must reuse visible text cache");
    view.set_horizontal_offset(1000000.0);
    require(view.horizontal_offset() <= 196.0, "horizontal offset must clamp to actual content extent");
    view.set_horizontal_offset(0.0);
    const bool cancel_press = window.dispatch_pointer({PointerAction::down, PointerButton::primary, {194.0, 15.0}});
    const bool cancel_drag = window.dispatch_pointer({PointerAction::move, PointerButton::none, {214.0, 15.0}});
    const bool cancelled = window.dispatch_key({KeyAction::down, PhysicalKey::escape});
    require(cancel_press && cancel_drag && cancelled && view.details_columns()[0U].width == 190.0 &&
        !view.has_pointer_capture(), "Escape must restore pre-gesture width and release capture");
    const bool revoked_press = window.dispatch_pointer({PointerAction::down, PointerButton::primary, {194.0, 15.0}});
    window.release_pointer();
    const bool revoked_move = window.dispatch_pointer({PointerAction::move, PointerButton::none, {214.0, 15.0}});
    require(revoked_press && revoked_move && view.details_columns()[0U].width == 190.0,
        "revoked capture must not resume resizing on a later move");
    const bool resize_again = window.dispatch_pointer({PointerAction::down, PointerButton::primary, {194.0, 15.0}});
    require(resize_again && view.has_pointer_capture(), "column resize must acquire capture");
    view.set_details_model(details_test_columns(), {});
    require(!view.has_pointer_capture() && view.items().empty(), "replacement during resize must revoke old capture");
    pointer_click(window, {40.0, 15.0}, PointerButton::primary);
    require(requests.size() == 3U, "empty table must retain usable header requests");
    std::string reentrant_column{};
    SubscriptionToken replacement = view.sort_requested().subscribe(
        DetailsReplacingSortHandler{view, reentrant_column});
    pointer_click(window, {40.0, 15.0}, PointerButton::primary);
    require(reentrant_column == "name" && view.details_columns().empty(),
        "sort event must own its request across synchronous model replacement");
    subscription.disconnect();
    replacement.disconnect();
    view.set_details_model(details_test_columns(), {});
    pointer_click(window, {40.0, 15.0}, PointerButton::primary);
    require(requests.size() == 4U && view.details_columns().size() == 3U,
        "revoked sort listeners must not receive later header activation");
}

void test_details_bounded_work() {
    const std::array<std::size_t, 2U> counts{1000U, 100000U};
    ObjectDetailsPaintWork baseline{};
    for (const std::size_t count : counts) {
        const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.scale"));
        ObjectView& view = *owner;
        view.set_requested_bounds({0.0, 0.0, 230.0, 190.0});
        view.view_mode = ObjectViewMode::details;
        view.set_details_model(details_test_columns(), details_test_items(count));
        Window window(owner, {230.0, 190.0});
        view.select_all();
        view.set_top_row(count / 2U);
        ImageRecordingPainter painter{};
        window.paint(painter, {0.0, 0.0, 230.0, 190.0});
        const ObjectDetailsPaintWork work = view.details_paint_work();
        require(work.rows <= 6U && work.cells <= 12U && work.text_preparations <= 14U &&
            view.children().empty(), "Details paint realization must depend on viewport, not 1k/100k row count");
        if (count == 1000U) baseline = work;
        else require(work.rows == baseline.rows && work.cells == baseline.cells,
            "1k and 100k fixture viewport work must match");
        require(view.selected_ids().size() == count, "bounded paint fixture must include a large selection set");
        view.set_top_row(count / 2U + 1U);
        ImageRecordingPainter scrolled{};
        window.paint(scrolled, {0.0, 0.0, 230.0, 190.0});
        require(view.details_paint_work().rows == work.rows && view.details_paint_work().cells == work.cells,
            "scrolling a large selected model must retain bounded visible work");
        std::cout << "Details fixture rows=" << count << " visited_rows=" << work.rows
                  << " cells=" << work.cells << " prepared=" << work.text_preparations << '\n';
    }
}

void test_details_cold_long_text_measurement_bound() {
    const std::shared_ptr<ObjectView> owner = make_control<ObjectView>(StableId("details.cold"));
    ObjectView& view = *owner;
    view.set_requested_bounds({0.0, 0.0, 140.0, 110.0});
    std::vector<ObjectDetailsColumn> columns{};
    columns.push_back({.id = {"payload"}, .label = "Payload", .width = 80.0});
    ObjectViewItem item{};
    item.stable_id = "long";
    item.name = "Long";
    item.cells.push_back({{"payload"}, std::string(65536U, 'x')});
    std::vector<ObjectViewItem> items{};
    items.push_back(std::move(item));
    view.set_details_model(std::move(columns), std::move(items));
    view.view_mode = ObjectViewMode::details;
    Window window(owner, {140.0, 110.0});
    const std::array<bool, 2U> measurement_profiles{false, true};
    for (const bool nonmonotonic : measurement_profiles) {
        view.view_mode = ObjectViewMode::icons;
        view.view_mode = ObjectViewMode::details;
        window.flush();
        ImageRecordingPainter cold{};
        cold.nonmonotonic_widths = nonmonotonic;
        // Direct renderer-neutral paint measures the control's cold work, not
        // Window's retained display-list replay or a warmed text-layout cache.
        view.on_paint(cold, {0.0, 0.0, 140.0, 110.0});
        const std::size_t queries = cold.measurement_queries;
        const std::size_t bytes = cold.measurement_bytes;
        require(queries <= 24U && bytes <= 200000U,
            "cold 64KiB narrow cell must bound both measurement calls and submitted bytes");
        bool saw_payload = false;
        for (std::size_t index = 0U; index < cold.texts.size(); ++index) {
            const std::string& text = cold.texts[index];
            if (!text.starts_with("x")) continue;
            saw_payload = true;
            const Size measured = cold.measure_text_utf8(text, cold.fonts[index]);
            require(text.ends_with("…") && measured.width <= 40.0,
                "exact returned ellipsis text must fit even with nonmonotonic prefix measurements");
        }
        require(saw_payload, "cold bounded test must actually paint the long cell");
        ImageRecordingPainter warm{};
        warm.nonmonotonic_widths = nonmonotonic;
        view.on_paint(warm, {0.0, 0.0, 140.0, 110.0});
        require(warm.measurement_queries == 0U && view.details_paint_work().text_preparations == 0U,
            "unchanged explicit Details text must reuse measurement and elision");
        std::cout << "Details cold bytes=65536 nonmonotonic=" << nonmonotonic
                  << " queries=" << queries << " measured_bytes=" << bytes << '\n';
    }
}

} // namespace

int main() {
    try {
        test_breadcrumb_identity_overflow_edit_and_input();
        test_breadcrumb_readable_raised_geometry();
        test_tree_visibility_identity_and_navigation();
        test_tree_model_validation();
        test_tree_and_object_view_consume_keyed_image_list();
        test_object_label_wrapping_focus_and_full_name_inspection();
        test_object_virtualization_view_preservation_and_input();
        test_object_multiselection_pointer_keyboard_and_semantics();
        test_details_transaction_and_identity();
        test_details_model_with_accepted_sort();
        test_details_header_entry_reentrancy();
        test_details_mode_release_reentrancy();
        test_details_sort_retires_capture();
        test_details_keyboard_sort_with_pending_layout();
        test_details_semantic_item_returns_keyboard_to_body();
        test_details_header_semantic_press();
        test_details_scroll_fills_available_page();
        test_details_header_input_resize_and_cache();
        test_details_bounded_work();
        test_details_cold_long_text_measurement_bound();
        test_shared_command_binding();
        test_correspondence_virtualization_and_anchor_stability();
        test_correspondence_keyboard_pin_semantics_and_activation();
        test_correspondence_narrow_card_layout();
        test_correspondence_background_pointer_contract();
        std::cout << "gui_forms_collection_controls_tests: all tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << "gui_forms_collection_controls_tests: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
