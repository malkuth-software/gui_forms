#include "file_manager_demoboard/demoboard.hpp"
#include "file_manager_demoboard/fixture_model.hpp"
#include "gui_forms/gui_forms.hpp"
#include "../src/smoke_test.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>

namespace {

class NullPainter final : public gui_forms::Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(gui_forms::Point) override {}
    void clip_rect(gui_forms::Rect) override {}
    void fill_rect(gui_forms::Rect, gui_forms::Color) override {}
    void stroke_rect(gui_forms::Rect, gui_forms::Color, double) override {}
    void draw_line(gui_forms::Point, gui_forms::Point, gui_forms::Color,
                   double) override {}
    void draw_text_utf8(gui_forms::Point, std::string_view,
                        gui_forms::FontSpec, gui_forms::Color) override {}
    void draw_image(gui_forms::ImageId, gui_forms::Rect, double) override {}
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

bool nearly_equal(double left, double right) noexcept {
    return std::abs(left - right) < 0.01;
}

class SemanticStableIdMatch final {
public:
    explicit SemanticStableIdMatch(std::string_view stable_id) noexcept
        : stable_id_(stable_id) {}

    bool operator()(const gui_forms::SemanticNode& node) const noexcept {
        return node.stable_id == stable_id_;
    }

private:
    std::string_view stable_id_;
};

bool contained_by(gui_forms::Rect inner, gui_forms::Rect outer) noexcept {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width + .01 &&
           inner.y + inner.height <= outer.y + outer.height + .01;
}

class ContainedByEither final {
public:
    ContainedByEither(gui_forms::Rect first, gui_forms::Rect second) noexcept
        : first_(first), second_(second) {}

    bool operator()(gui_forms::Rect rect) const noexcept {
        return contained_by(rect, first_) || contained_by(rect, second_);
    }

private:
    gui_forms::Rect first_;
    gui_forms::Rect second_;
};

bool is_renamed_facade_study(
    const gui_forms::ObjectViewItem& item) noexcept {
    return item.stable_id == "fm.object.obj-facade-study" &&
           item.name == "Facade Final.png";
}

void resize_and_layout_twice(gui_forms::Window& window,
                             gui_forms::Size size) {
    window.resize(size);
    window.perform_layout();
    window.perform_layout();
}

void require_bounds(gui_forms::Window& window, std::string_view id,
                    gui_forms::Rect expected) {
    const gui_forms::Control::Ptr control = window.find(id);
    require(control != nullptr, std::string("missing control: ") + std::string(id));
    const gui_forms::Rect actual = (*control).committed_arranged_bounds();
    if (!(nearly_equal(actual.x, expected.x) &&
          nearly_equal(actual.y, expected.y) &&
          nearly_equal(actual.width, expected.width) &&
          nearly_equal(actual.height, expected.height))) {
        std::cerr << id << " bounds were " << actual.x << ',' << actual.y << ','
                  << actual.width << ',' << actual.height << " expected "
                  << expected.x << ',' << expected.y << ',' << expected.width
                  << ',' << expected.height << '\n';
        std::exit(1);
    }
}

void print_paint_dirty(const gui_forms::Control::Ptr& control) {
    if (gui_forms::has_dirty((*control).dirty(), gui_forms::Dirty::paint) ||
        gui_forms::has_dirty((*control).subtree_dirty(), gui_forms::Dirty::paint)) {
        std::cerr << "paint-dirty " << (*control).stable_id().value() << '\n';
    }
    for (const std::shared_ptr<gui_forms::Control>& child : (*control).children()) print_paint_dirty(child);
}

bool semantic_contains(const std::vector<gui_forms::SemanticNode>& nodes,
                       std::string_view stable_id) {
    for (const gui_forms::SemanticNode& node : nodes) {
        if (node.stable_id == stable_id || semantic_contains(node.children, stable_id)) {
            return true;
        }
    }
    return false;
}

const gui_forms::SemanticNode* find_semantic(
    const std::vector<gui_forms::SemanticNode>& nodes,
    std::string_view stable_id) {
    for (const gui_forms::SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        if (const gui_forms::SemanticNode* found = find_semantic(node.children, stable_id)) return found;
    }
    return nullptr;
}
const gui_forms::SemanticNode* find_semantic(std::vector<gui_forms::SemanticNode>&& nodes, std::string_view stable_id) = delete;

gui_forms::Point virtual_item_center(const gui_forms::ObjectView& view,
                                     std::string_view stable_id) {
    const std::vector<gui_forms::SemanticNode> nodes = view.semantic_virtual_children();
    const std::vector<gui_forms::SemanticNode>::const_iterator found =
        std::find_if(nodes.begin(), nodes.end(),
                     SemanticStableIdMatch(stable_id));
    require(found != nodes.end(), "virtual item required by interaction is unrealized");
    return {(*found).bounds.x + (*found).bounds.width * .5,
            (*found).bounds.y + (*found).bounds.height * .5};
}

void secondary_click(gui_forms::Window& window, gui_forms::Point point) {
    require(window.dispatch_pointer({gui_forms::PointerAction::down,
                                     gui_forms::PointerButton::secondary, point}) &&
                window.dispatch_pointer({gui_forms::PointerAction::up,
                                         gui_forms::PointerButton::secondary, point}),
            "secondary click must be handled by the object collection");
}

struct CountSmokeClose final {
    std::shared_ptr<int> count;

    void operator()() const { ++(*count); }
};

std::shared_ptr<file_manager_demoboard::SmokeTest> arm_smoke(
    gui_forms::Window& window, gui_forms::FrameTime started, const std::shared_ptr<int>& closes) {
    std::shared_ptr<file_manager_demoboard::SmokeTest> state =
        std::make_shared<file_manager_demoboard::SmokeTest>();
    (*state).window = &window;
    (*state).started = started;
    (*state).request_close = CountSmokeClose{closes};
    return state;
}

// --smoke-test passes only when a frame had been presented before the
// scheduled close; the deadline close of a never-presented window fails.
void require_smoke_verdict() {
    using namespace std::chrono_literals;
    const gui_forms::FrameTime started = gui_forms::FrameTime{} + 1s;

    std::unique_ptr<gui_forms::Window> hidden = file_manager_demoboard::make_product_window();
    (*hidden).set_occluded(true, gui_forms::FrameTime{});
    const std::shared_ptr<int> hidden_closes = std::make_shared<int>(0);
    const std::shared_ptr<file_manager_demoboard::SmokeTest> unseen =
        arm_smoke(*hidden, started, hidden_closes);
    file_manager_demoboard::SmokeTestTick{unseen}(started + 9900ms);
    require(!(*unseen).completed, "the smoke closed before its deadline");
    file_manager_demoboard::SmokeTestTick{unseen}(started + 10s);
    require((*unseen).completed && *hidden_closes == 1 && !(*unseen).presented &&
                (*unseen).occluded,
            "the deadline close must record no presented frame and the occlusion");
    require(file_manager_demoboard::smoke_test_result(unseen, 0) == 1,
            "a smoke that presented no frame must fail although the host returned 0");

    std::unique_ptr<gui_forms::Window> shown = file_manager_demoboard::make_product_window();
    NullPainter painter;
    const std::optional<gui_forms::PaintReceipt> receipt =
        (*shown).paint(painter, (*shown).take_damage().bounds());
    require(receipt.has_value() && (*shown).notify_presented(*receipt),
            "the product window did not present its first frame");
    const std::shared_ptr<int> shown_closes = std::make_shared<int>(0);
    const std::shared_ptr<file_manager_demoboard::SmokeTest> seen =
        arm_smoke(*shown, started, shown_closes);
    file_manager_demoboard::SmokeTestTick{seen}(started + 1s);
    require((*seen).completed && *shown_closes == 1 && (*seen).presented,
            "a presented smoke must close after one second and record the frame");
    require(file_manager_demoboard::smoke_test_result(seen, 0) == 0, "a presented smoke failed");
    require(file_manager_demoboard::smoke_test_result(seen, 3) == 3,
            "the host's failure code was replaced");
}

} // namespace

int main() {
    using file_manager_demoboard::FixtureCatalogue;
    const FixtureCatalogue& catalogue = FixtureCatalogue::instance();
    require(catalogue.id() == "file-manager-demoboard-001", "catalogue ID drifted");
    require(catalogue.generation() == 86, "catalogue generation drifted");
    require(catalogue.project_objects().size() == 14, "project object fixture drifted");

    std::unique_ptr<gui_forms::Window> product = file_manager_demoboard::make_product_window();
    require_bounds(*product, "fm.title.identity", {0,0,1450,40});
    require_bounds(*product, "fm.ribbon.tabs", {0,40,1450,23});
    require_bounds(*product, "fm.ribbon.shelf", {0,63,1450,66});
    require_bounds(*product, "fm.navigation", {0,129,1450,40});
    require_bounds(*product, "fm.workspace", {0,169,1450,657});
    require_bounds(*product, "fm.status", {0,826,1450,24});
    require((*product).find("fm.tree.view") != nullptr, "tree projection is absent");
    require((*product).find("fm.selection.pane") != nullptr,
            "selection projection is absent");
    const gui_forms::SemanticSnapshot semantic = (*product).semantic_snapshot();
    require(semantic.node_count != 0 && !semantic.roots.empty(),
            "semantic snapshot is empty");
    require(semantic_contains(semantic.roots, "fm.object.obj-facade-study"),
            "default selected virtual fixture object is absent from semantics");
    require((*(*product).find("fm.title.material")).absolute_bounds() ==
                gui_forms::Rect{0,0,1450,40},
            "title material is not arranged in the title band");

    const std::shared_ptr<gui_forms::Button> terminal = std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.path.terminal"));
    require(terminal && (*product).request_focus(terminal),
            "path terminal must be a focusable retained overlay owner");
    const gui_forms::Rect terminal_bounds = (*terminal).absolute_bounds();
    const gui_forms::Point terminal_center{
        terminal_bounds.x + terminal_bounds.width * .5,
        terminal_bounds.y + terminal_bounds.height * .5};
    require((*product).dispatch_pointer({gui_forms::PointerAction::down,
                                       gui_forms::PointerButton::primary,
                                       terminal_center}) &&
                (*product).dispatch_pointer({gui_forms::PointerAction::up,
                                           gui_forms::PointerButton::primary,
                                           terminal_center}),
            "path terminal pointer activation must route through retained input");
    (*product).perform_layout();
    const gui_forms::Control::Ptr matrix = (*product).find("fm.path.matrix");
    const std::shared_ptr<gui_forms::AnchoredOverlayLayer> matrix_layer = std::dynamic_pointer_cast<gui_forms::AnchoredOverlayLayer>(
        (*product).find("fm.path.matrix.layer"));
    require(matrix && matrix_layer && (*terminal).expanded_state.read() == true &&
                (*matrix).absolute_bounds() == gui_forms::Rect{111,169,860,424} &&
                (*product).focused_control() ==
                    (*product).find("fm.path.matrix.current.tail"),
            "./ must open one edge-bounded matrix and contain focus at its open tail");
    const char* recent_ids[] = {
        "orchard-study", "legal-2026", "archive-material-library",
        "pictures-scans", "reference-materials"};
    for (const char* id : recent_ids) {
        require((*product).find(std::string("fm.path.matrix.recent.") + id) != nullptr,
                "path matrix must expose all five exact recent full stacks");
    }
    require((*product).find("fm.path.matrix.recent.sixth") == nullptr &&
                (*product).find("fm.path.matrix.current.segment.macintosh-hd") &&
                (*product).find("fm.path.matrix.current.segment.projects"),
            "path matrix must retain one full current drive stack and exactly five recents");

    (*std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.path.matrix.current.tail"))).perform_click();
    static_cast<void>((*product).drain_posted_work());
    (*product).perform_layout();
    std::shared_ptr<gui_forms::TextBox> path_editor = std::dynamic_pointer_cast<gui_forms::TextBox>(
        (*product).find("fm.path.matrix.editor"));
    std::shared_ptr<gui_forms::ListBox> completions = std::dynamic_pointer_cast<gui_forms::ListBox>(
        (*product).find("fm.path.matrix.completions"));
    require(path_editor && completions && (*path_editor).visible.read() &&
                (*path_editor).text.read() == "/Users/quentin/Work/Projects/" &&
                (*path_editor).selection().empty() &&
                (*path_editor).selection().caret.value() == (*path_editor).text.read().size() &&
                (*product).focused_control() == path_editor,
            "open tail must become the real public TextBox in place with caret at end");
    (*path_editor).text = "$PROJECTS/N";
    const gui_forms::DispatcherSnapshot completion_dispatch =
        (*product).dispatcher_snapshot();
    require(completion_dispatch.pending == 1U &&
                completion_dispatch.cancelled >= 1U,
            "a newer path query must cancel the prior deferred completion generation");
    static_cast<void>((*product).drain_posted_work());
    require((*completions).items.read().size() == 1U &&
                (*completions).items.read().front() ==
                    "/Users/quentin/Work/Projects/North Shore/" &&
                (*completions).item_stable_id(0U) ==
                    "fm.path.matrix.completion.north-shore" &&
                semantic_contains((*product).semantic_snapshot().roots,
                                  "fm.path.matrix.completion.north-shore"),
            "fixture completion must expand variables and preserve exact option identity");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::down}) &&
                (*product).focused_control() == path_editor &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::tab}) &&
                (*path_editor).text.read() ==
                    "/Users/quentin/Work/Projects/North Shore/" &&
                (*product).focused_control() == path_editor,
            "completion navigation and Tab acceptance must leave text focus in the editor");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::enter}) &&
                !(*product).find("fm.path.matrix") &&
                (*terminal).expanded_state.read() == false &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.title.text"))).text.read().find("North Shore") !=
                    std::string::npos,
            "Enter on a valid completion must close once and navigate atomically");
    (*std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.nav.back"))).perform_click();

    require((*product).request_focus(terminal),
            "terminal must accept focus after valid path navigation");
    (*terminal).perform_click();
    (*std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.path.matrix.current.tail"))).perform_click();
    static_cast<void>((*product).drain_posted_work());
    path_editor = std::dynamic_pointer_cast<gui_forms::TextBox>(
        (*product).find("fm.path.matrix.editor"));
    (*path_editor).text = "$UNKNOWN/Projects";
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::enter}) &&
                (*product).find("fm.path.matrix") &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.path.matrix.resolution"))
                        ).accessible_description.read().starts_with("Error:") &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.status.authority"))).text.read().find(
                        "Unknown variable") != std::string::npos,
            "invalid path must remain editing with visible and semantic explanation");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::escape}) &&
                (*product).find("fm.path.matrix") &&
                !(*path_editor).visible.read() &&
                (*product).focused_control() ==
                    (*product).find("fm.path.matrix.current.tail") &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::escape}) &&
                !(*product).find("fm.path.matrix") &&
                (*product).focused_control() == terminal,
            "Escape must first return to browse, then close and restore terminal focus");

    (*terminal).perform_click();
    (*product).perform_layout();
    require((*product).dispatch_pointer({gui_forms::PointerAction::down,
                                       gui_forms::PointerButton::primary,
                                       {20.0, 220.0}}) &&
                !(*product).find("fm.path.matrix") &&
                (*product).focused_control() == terminal,
            "click-away must close the matrix and restore its owner focus");

    std::unique_ptr<gui_forms::Window> capture_product = file_manager_demoboard::make_product_window();
    require(file_manager_demoboard::apply_capture_state(
                *capture_product, "path-matrix-browse") &&
                (*capture_product).find("fm.path.matrix") &&
                !file_manager_demoboard::apply_capture_state(
                    *capture_product, "not-a-capture-state"),
            "capture-state entrypoint must route through public semantic actions");

    std::unique_ptr<gui_forms::Window> search_capture_product = file_manager_demoboard::make_product_window();
    require(file_manager_demoboard::apply_capture_state(
                *search_capture_product, "search-pinned") &&
                (*(*search_capture_product).find("fm.search.surface")).visible.read() &&
                !(*(*search_capture_product).find(
                    "fm.workspace.content-selection")).visible.read(),
            "Search capture state must use the same retained surface transition");

    std::unique_ptr<gui_forms::Window> search_product = file_manager_demoboard::make_product_window();
    require(file_manager_demoboard::set_product_surface(*search_product, "search"),
            "public surface transition must admit Search");
    (*search_product).perform_layout();
    const std::shared_ptr<gui_forms::CorrespondenceView> search_results =
        std::dynamic_pointer_cast<gui_forms::CorrespondenceView>(
            (*search_product).find("fm.search.results"));
    const gui_forms::Control::Ptr search_surface =
        (*search_product).find("fm.search.surface");
    const gui_forms::Control::Ptr folder_surface = (*search_product).find(
        "fm.workspace.content-selection");
    require(search_results && search_surface && folder_surface &&
                (*search_surface).visible.read() && !(*folder_surface).visible.read() &&
                (*search_results).items().size() == 7U &&
                (*search_results).children().empty() &&
                (*search_results).compact_height.read() == 45.0 &&
                (*search_results).expanded_height.read() == 126.0 &&
                (*search_results).selected_id.read() ==
                    "fm.result.result-invoice-text" &&
                (*search_results).pinned_id.read() ==
                    "fm.result.result-invoice-text" &&
                (*search_results).expanded("fm.result.result-invoice-text"),
            "Search must expose seven virtual correspondences with one exact default pin");
    const gui_forms::SemanticSnapshot search_semantics =
        (*search_product).semantic_snapshot();
    require(semantic_contains(search_semantics.roots, "fm.tree.view") &&
                semantic_contains(search_semantics.roots,
                                  "fm.result.result-invoice-text") &&
                semantic_contains(search_semantics.roots,
                                  "fm.result.result-invoice-text.activate") &&
                semantic_contains(search_semantics.roots,
                                  "fm.result.result-invoice-text.percentage") &&
                semantic_contains(search_semantics.roots,
                                  "fm.result.result-invoice-text.metadata") &&
                semantic_contains(search_semantics.roots,
                                  "fm.result.result-invoice-text.excerpt") &&
                semantic_contains(search_semantics.roots,
                                  "fm.result.result-invoice-text.plugins") &&
                !semantic_contains(search_semantics.roots, "fm.selection.pane") &&
                !semantic_contains(search_semantics.roots, "fm.folder.objects"),
            "Search must retain the tree, expose evidence lanes, and remove Folder selection semantics");
    require((*search_results).realized_count() <= 7U,
            "Search correspondence realization must remain bounded by its viewport");
    const std::optional<gui_forms::Rect> first_bounds = (*search_results).item_bounds(
        "fm.result.result-invoice-text");
    const std::optional<gui_forms::Rect> second_bounds = (*search_results).item_bounds(
        "fm.result.result-invoice-pdf");
    require(first_bounds && second_bounds && (*first_bounds).height == 126.0 &&
                (*second_bounds).height == 45.0 &&
                std::abs((*second_bounds).y - ((*first_bounds).y + (*first_bounds).height)) <
                    0.01,
            "compact and expanded correspondence geometry must share one stable row stack");
    NullPainter search_painter;
    const gui_forms::DamageRegion search_startup_damage =
        (*search_product).take_damage();
    (*search_product).paint(search_painter, search_startup_damage.bounds());
    (*search_product).reset_activity_metrics();
    require((*search_product).take_damage().empty() &&
                !(*search_product).needs_frame(),
            "settled Search must retain neither idle damage nor an active frame");
    require((*search_product).perform_semantic_action(
                "fm.result.result-pages-offline",
                gui_forms::SemanticAction::expand),
            "unavailable Search evidence must remain inspectable");
    (*search_product).perform_layout();
    const gui_forms::DamageRegion offline_damage = (*search_product).take_damage();
    const gui_forms::Rect result_damage_bounds = offline_damage.bounds();
    const gui_forms::Rect result_control_bounds = (*search_results).absolute_bounds();
    const gui_forms::Rect result_status_bounds =
        (*(*search_product).find("fm.status.authority")).absolute_bounds();
    const bool damage_contained = !offline_damage.empty() &&
        std::all_of(offline_damage.rectangles().begin(),
                    offline_damage.rectangles().end(),
                    ContainedByEither(result_control_bounds,
                                      result_status_bounds));
    require(damage_contained,
            "correspondence expansion damage must stay inside Search and its factual status cell");
    (*search_product).paint(search_painter, result_damage_bounds);
    const gui_forms::SemanticSnapshot offline_semantics =
        (*search_product).semantic_snapshot();
    const gui_forms::SemanticNode* offline = find_semantic(
        offline_semantics.roots, "fm.result.result-pages-offline");
    require(offline && gui_forms::has_semantic_state(
                           (*offline).states, gui_forms::SemanticState::expanded) &&
                (*offline).description.find("source unavailable") != std::string::npos &&
                semantic_contains((*offline).children,
                                  "fm.result.result-pages-offline.excerpt"),
            "offline correspondence must expand with explicit availability evidence");
    require((*search_product).perform_semantic_action(
                "fm.result.result-pages-offline",
                gui_forms::SemanticAction::press) &&
                (*search_surface).visible.read() &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*search_product).find("fm.status.authority"))).text.read().find(
                        "source volume unavailable") != std::string::npos,
            "unavailable result activation must fail in place with an honest reason");
    require((*search_product).request_focus(search_results) &&
                (*search_product).dispatch_key({gui_forms::KeyAction::down,
                                              gui_forms::PhysicalKey::home}) &&
                (*search_product).dispatch_key({gui_forms::KeyAction::down,
                                              gui_forms::PhysicalKey::down}) &&
                (*search_results).focused_id() ==
                    "fm.result.result-invoice-pdf" &&
                (*search_product).dispatch_key({gui_forms::KeyAction::down,
                                              gui_forms::PhysicalKey::space}) &&
                (*search_results).pinned_id.read() ==
                    "fm.result.result-invoice-pdf",
            "Search keyboard focus and Space pinning must be independent and deterministic");
    require((*search_product).dispatch_key({gui_forms::KeyAction::down,
                                          gui_forms::PhysicalKey::enter}) &&
                (*folder_surface).visible.read() && !(*search_surface).visible.read() &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*search_product).find("fm.title.text"))).text.read().ends_with(
                        "Orchard Study") &&
                (*std::dynamic_pointer_cast<gui_forms::ObjectView>(
                    (*search_product).find("fm.folder.objects"))).selected_id.read() ==
                    "fm.object.search-invoice-pdf" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*search_product).find("fm.selection.object-name"))).text.read() ==
                    "Invoice 0428.pdf",
            "Enter must activate a Search result through normal Folder navigation");

    std::unique_ptr<gui_forms::Window> debounced_search = file_manager_demoboard::make_product_window();
    const std::shared_ptr<gui_forms::TextBox> search_editor = std::dynamic_pointer_cast<gui_forms::TextBox>(
        (*debounced_search).find("fm.search.editor"));
    require(search_editor != nullptr, "Search requires the public navigation TextBox");
    (*search_editor).text = "invoice";
    (*search_editor).text = "invoice quartz";
    require(!(*(*debounced_search).find("fm.search.surface")).visible.read() &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*debounced_search).find("fm.status.authority"))).text.read().find(
                        "Search pending") != std::string::npos,
            "Search edits must remain deferred until the latest debounce generation");
    static_cast<void>((*debounced_search).poll_frame_schedule(
        gui_forms::FrameClock::now() + std::chrono::seconds(1)));
    (*debounced_search).perform_layout();
    require((*(*debounced_search).find("fm.search.surface")).visible.read() &&
                !(*(*debounced_search).find("fm.workspace.content-selection")).visible.read() &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*debounced_search).find("fm.status.summary"))).text.read() ==
                    "5 of 7 results shown · 1 unavailable source" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*debounced_search).find("fm.status.authority"))).text.read() ==
                    "Local navigation ready · result generation 86",
            "only the latest debounced query may project the Search surface");
    (*debounced_search).resize({1800, 1050});
    (*debounced_search).perform_layout();
    (*debounced_search).perform_layout();
    const double wide_search_width =
        (*(*debounced_search).find("fm.search.results")).absolute_bounds().width;
    require(wide_search_width > 1500.0,
            "Search correspondence must fill a product window wider than startup");
    (*debounced_search).resize({480, 360});
    (*debounced_search).perform_layout();
    (*debounced_search).perform_layout();
    require((*(*debounced_search).find("fm.search.results")).absolute_bounds().width >
                430.0 &&
                !semantic_contains((*debounced_search).semantic_snapshot().roots,
                                   "fm.selection.pane"),
            "compact Search must preserve one useful evidence field without reintroducing Selection");

    std::unique_ptr<gui_forms::Window> criteria_product = file_manager_demoboard::make_product_window();
    require(file_manager_demoboard::set_product_surface(
                *criteria_product, "criteria"),
            "public surface transition must admit Criteria");
    (*criteria_product).perform_layout();
    const std::shared_ptr<gui_forms::InstrumentRack> criteria_rack =
        std::dynamic_pointer_cast<gui_forms::InstrumentRack>(
            (*criteria_product).find("fm.criteria.console"));
    const std::shared_ptr<gui_forms::ObjectView> criteria_objects =
        std::dynamic_pointer_cast<gui_forms::ObjectView>(
            (*criteria_product).find("fm.criteria.objects"));
    const std::shared_ptr<gui_forms::Button> criteria_apply =
        std::dynamic_pointer_cast<gui_forms::Button>(
            (*criteria_product).find("fm.criteria.apply"));
    const std::shared_ptr<gui_forms::ProgressBar> criteria_progress =
        std::dynamic_pointer_cast<gui_forms::ProgressBar>(
            (*criteria_product).find("fm.criteria.progress"));
    require(criteria_rack && criteria_objects && criteria_apply &&
                criteria_progress && (*criteria_rack).modules().size() == 3U &&
                (*criteria_objects).items().size() == 31U &&
                (*criteria_objects).selected_id.read() ==
                    "fm.object.obj-facade-study" &&
                (*(*criteria_product).find("fm.workspace.content-selection")).visible.read() &&
                !(*(*criteria_product).find("fm.folder.objects")).visible.read() &&
                (*(*criteria_product).find("fm.selection.pane")).visible.read(),
            "Criteria must retain three exact instruments, its virtual objects, and Selection");
    const gui_forms::SemanticSnapshot criteria_semantics =
        (*criteria_product).semantic_snapshot();
    require(semantic_contains(criteria_semantics.roots,
                              "fm.criteria.module.criteria-kind-images.enable") &&
                semantic_contains(criteria_semantics.roots,
                                  "fm.criteria.module.criteria-modified-2026.operator") &&
                semantic_contains(criteria_semantics.roots,
                                  "fm.criteria.module.criteria-content-facade.value") &&
                semantic_contains(criteria_semantics.roots,
                                  "fm.criteria.module.criteria-content-facade.remove") &&
                semantic_contains(criteria_semantics.roots,
                                  "fm.criteria.apply") &&
                !semantic_contains(criteria_semantics.roots,
                                   "fm.folder.objects"),
            "Criteria controls and staged meaning must be exposed without hidden Folder semantics");
    require((*criteria_product).perform_semantic_action(
                "fm.criteria.apply", gui_forms::SemanticAction::press) &&
                (*criteria_progress).animation_enabled() &&
                !(*criteria_apply).enabled.read(),
            "Apply must enter one explicit pending state and reject duplicate activation");
    const gui_forms::FrameTime apply_clock = gui_forms::FrameClock::now() +
                                              std::chrono::seconds(1);
    for (int step = 0; step < 4; ++step) {
        static_cast<void>((*criteria_product).poll_frame_schedule(
            apply_clock + std::chrono::milliseconds(100 * step)));
    }
    require(!(*criteria_progress).animation_enabled() &&
                (*criteria_progress).value() == 100.0 &&
                (*criteria_objects).items().size() == 5U &&
                (*criteria_objects).selected_id.read() ==
                    "fm.object.obj-facade-study" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*criteria_product).find("fm.status.summary"))).text.read() ==
                    "5 objects · 3 criteria applied" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*criteria_product).find("fm.criteria.console.title"))).text.read().find(
                        "0 STAGED") != std::string::npos &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*criteria_product).find("fm.criteria.results.summary"))).text.read().starts_with(
                        "5 OBJECTS") &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*criteria_product).find("fm.status.authority"))).text.read() ==
                    "Local navigation ready · index 87 current",
            "Apply must complete deterministically while preserving surviving selection");

    std::unique_ptr<gui_forms::Window> criteria_edit = file_manager_demoboard::make_product_window();
    static_cast<void>(file_manager_demoboard::set_product_surface(
        *criteria_edit, "criteria"));
    const std::shared_ptr<gui_forms::InstrumentRack> edit_rack = std::dynamic_pointer_cast<gui_forms::InstrumentRack>(
        (*criteria_edit).find("fm.criteria.console"));
    const std::shared_ptr<gui_forms::ComboBox> kind_value = std::dynamic_pointer_cast<gui_forms::ComboBox>(
        (*criteria_edit).find("fm.criteria.module.criteria-kind-images.value"));
    require(edit_rack && kind_value, "Criteria live editor projection is absent");
    (*kind_value).selected_index = 1U;
    require((*edit_rack).modules()[0].state ==
                gui_forms::InstrumentModuleState::live &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*criteria_edit).find("fm.status.authority"))).text.read().find(
                        "Live criterion committed") != std::string::npos,
            "cheap choice edits must update the live projection immediately");
    require((*criteria_edit).perform_semantic_action(
                "fm.criteria.add", gui_forms::SemanticAction::press),
            "bounded Add menu must open through the public semantic action");
    (*criteria_edit).perform_layout();
    const gui_forms::Control::Ptr add_button =
        (*criteria_edit).find("fm.criteria.add");
    const gui_forms::Control::Ptr add_overlay = (*criteria_edit).find(
        "fm.criteria.add.menu.overlay.panel.0");
    require(add_button && add_overlay &&
                (*add_overlay).absolute_bounds().x >=
                    (*add_button).absolute_bounds().x - 1.0 &&
                (*add_overlay).absolute_bounds().y >=
                    (*add_button).absolute_bounds().y +
                    (*add_button).absolute_bounds().height - 1.0 &&
                (*criteria_edit).dispatch_key({gui_forms::KeyAction::down,
                                             gui_forms::PhysicalKey::enter}) &&
                (*edit_rack).modules().size() == 4U,
            "bounded Add menu must keyboard-activate a genuine module template");
    const std::string added_id = (*edit_rack).modules().back().stable_id;
    require((*criteria_edit).request_focus(
                (*criteria_edit).find(added_id + ".remove")) &&
                (*criteria_edit).perform_semantic_action(
                    added_id + ".remove", gui_forms::SemanticAction::press) &&
                (*edit_rack).modules().size() == 3U &&
                (*criteria_edit).focused_control() != nullptr,
            "module removal must mutate the model and transfer focus predictably");

    std::unique_ptr<gui_forms::Window> compact_criteria = file_manager_demoboard::make_product_window();
    (*compact_criteria).resize({540.0, 500.0});
    (*compact_criteria).set_text_scale(2.25);
    static_cast<void>(file_manager_demoboard::set_product_surface(
        *compact_criteria, "criteria"));
    (*compact_criteria).perform_layout();
    (*compact_criteria).perform_layout();
    const std::shared_ptr<gui_forms::InstrumentRack> compact_rack =
        std::dynamic_pointer_cast<gui_forms::InstrumentRack>(
            (*compact_criteria).find("fm.criteria.console"));
    const std::optional<gui_forms::Rect> compact_first = (*compact_rack).module_bounds(
        "fm.criteria.module.criteria-kind-images");
    const std::optional<gui_forms::Rect> compact_third = (*compact_rack).module_bounds(
        "fm.criteria.module.criteria-content-facade");
    require(compact_first && compact_third &&
                (*compact_third).y > (*compact_first).y &&
                (*compact_rack).content_height() >
                    (*compact_rack).committed_arranged_bounds().height,
            "Criteria must wrap and remain scroll-reachable under narrow large text");

    std::unique_ptr<gui_forms::Window> palette_product = file_manager_demoboard::make_product_window();
    require(file_manager_demoboard::apply_capture_state(
                *palette_product, "palettes"),
            "palette capture state must enter through the public surface selector");
    (*palette_product).perform_layout();
    const gui_forms::Control::Ptr palette_surface =
        (*palette_product).find("fm.review.palettes");
    require(palette_surface && (*palette_surface).visible.read() &&
                !(*(*palette_product).find("fm.workspace.content-selection")).visible.read() &&
                !(*(*palette_product).find("fm.search.surface")).visible.read() &&
                !(*(*palette_product).find("fm.criteria.surface")).visible.read(),
            "palette review must replace the daily content surface without hidden overlap");
    constexpr const char* palette_ids[] = {
        "cobalt", "amethyst", "miami", "orchid", "aqua", "apricot",
        "mulberry", "viridian", "sapphire", "rose", "iris", "phosphor"};
    for (const char* palette_id : palette_ids) {
        const std::string card_id = std::string("fm.palette.") + palette_id;
        const std::shared_ptr<gui_forms::Card> card = std::dynamic_pointer_cast<gui_forms::Card>(
            (*palette_product).find(card_id));
        require(card && (*card).interactive.read() &&
                    (*palette_product).find(card_id + ".title") &&
                    (*palette_product).find(card_id + ".swatch") &&
                    (*palette_product).find(card_id + ".state"),
                "every atmosphere must be a public interactive Card with retained sections");
    }
    const std::shared_ptr<gui_forms::Card> sapphire_card = std::dynamic_pointer_cast<gui_forms::Card>(
        (*palette_product).find("fm.palette.sapphire"));
    const std::shared_ptr<gui_forms::Card> cobalt_card = std::dynamic_pointer_cast<gui_forms::Card>(
        (*palette_product).find("fm.palette.cobalt"));
    const gui_forms::SemanticSnapshot palette_semantics =
        (*palette_product).semantic_snapshot();
    const gui_forms::SemanticNode* sapphire_semantic = find_semantic(
        palette_semantics.roots, "fm.palette.sapphire");
    require(sapphire_card && cobalt_card && (*sapphire_card).selected.read() &&
                !(*cobalt_card).selected.read() && sapphire_semantic &&
                gui_forms::has_semantic_state(
                    (*sapphire_semantic).states, gui_forms::SemanticState::selected),
            "Sapphire must begin as the visibly and semantically selected recipe");
    require((*palette_product).perform_semantic_action(
                "fm.palette.cobalt", gui_forms::SemanticAction::press) &&
                (*cobalt_card).selected.read() && !(*sapphire_card).selected.read() &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*palette_product).find("fm.status.summary"))).text.read().find(
                        "Cobalt atmosphere") != std::string::npos,
            "palette activation must atomically move one selection and publish its status");
    (*palette_product).resize({1800.0, 1050.0});
    (*palette_product).perform_layout();
    (*palette_product).perform_layout();
    require((*palette_surface).absolute_bounds().width > 1500.0 &&
                (*(*palette_product).find("fm.palette.phosphor")
                        ).absolute_bounds().x +
                        (*(*palette_product).find("fm.palette.phosphor")
                            ).absolute_bounds().width <=
                    (*palette_surface).absolute_bounds().x +
                        (*palette_surface).absolute_bounds().width,
            "palette cards must reflow inside the grown product surface");
    require(file_manager_demoboard::set_product_surface(
                *palette_product, "folder") &&
                !(*palette_surface).visible.read() &&
                (*(*palette_product).find("fm.workspace.content-selection")).visible.read(),
            "leaving a review laboratory must restore the ordinary Folder projection");

    std::unique_ptr<gui_forms::Window> dna_product = file_manager_demoboard::make_product_window();
    require(file_manager_demoboard::apply_capture_state(*dna_product, "dna"),
            "DNA capture state must enter through the public surface selector");
    (*dna_product).perform_layout();
    const std::shared_ptr<gui_forms::MasterDetailView> dna_view = std::dynamic_pointer_cast<gui_forms::MasterDetailView>(
        (*dna_product).find("fm.review.dna"));
    require(dna_view && (*dna_view).visible.read() && (*dna_view).master() &&
                (*dna_view).detail() &&
                (*dna_view).effective_display_mode() ==
                    gui_forms::MasterDetailDisplayMode::side_by_side,
            "DNA must dogfood the public wide MasterDetailView composition");
    constexpr const char* decision_ids[]{
        "surface", "material", "provider", "similarity"};
    for (const char* decision_id : decision_ids) {
        const std::shared_ptr<gui_forms::Card> card = std::dynamic_pointer_cast<gui_forms::Card>(
            (*dna_product).find(std::string("fm.dna.") + decision_id));
        require(card && (*card).interactive.read() && (*card).header() && (*card).body() &&
                    (*card).footer(),
                "every DNA decision must be a retained interactive public Card");
    }
    require((*dna_product).perform_semantic_action(
                "fm.dna.material", gui_forms::SemanticAction::press) &&
                (*std::dynamic_pointer_cast<gui_forms::Card>(
                    (*dna_product).find("fm.dna.material"))).selected.read() &&
                !(*std::dynamic_pointer_cast<gui_forms::Card>(
                    (*dna_product).find("fm.dna.surface"))).selected.read() &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*dna_product).find("fm.review.dna.heading"))).text.read() ==
                    "House material authority",
            "DNA activation must move selection and project the chosen record");
    (*dna_product).resize({680.0, 650.0});
    (*dna_product).perform_layout();
    (*dna_product).perform_layout();
    require((*dna_view).effective_display_mode() ==
                gui_forms::MasterDetailDisplayMode::master_only,
            "DNA must collapse to its master navigation under compact width");
    require((*dna_product).perform_semantic_action(
                "fm.dna.provider", gui_forms::SemanticAction::press),
            "compact DNA decision must accept semantic activation");
    (*dna_product).perform_layout();
    require((*dna_view).effective_display_mode() ==
                gui_forms::MasterDetailDisplayMode::detail_only,
            "compact DNA activation must navigate to the detail presentation");
    (*dna_view).show_master();
    (*dna_product).perform_layout();
    require((*dna_view).effective_display_mode() ==
                gui_forms::MasterDetailDisplayMode::master_only,
            "compact DNA detail must retain an explicit route back to master");

    NullPainter painter;
    const gui_forms::DamageRegion startup_damage = (*product).take_damage();
    require(!startup_damage.empty(), "startup did not publish retained damage");
    (*product).paint(painter, startup_damage.bounds());
    (*product).reset_activity_metrics();
    const gui_forms::DamageRegion idle_damage = (*product).take_damage();
    const gui_forms::MetricsSnapshot idle = (*product).metrics_snapshot();
    if (!idle_damage.empty() || (*product).needs_frame() || idle.measure_passes != 0 ||
        idle.arrange_passes != 0 || idle.paint_passes != 0) {
        std::cerr << "idle damage=" << idle_damage.area()
                  << " frame=" << (*product).needs_frame()
                  << " measure=" << idle.measure_passes
                  << " arrange=" << idle.arrange_passes
                  << " paint=" << idle.paint_passes << '\n';
        print_paint_dirty((*product).root());
        const gui_forms::Rect caption_bounds =
            (*(*product).find("fm.tree.caption")).absolute_bounds();
        std::cerr << "caption bounds=" << caption_bounds.x << ',' << caption_bounds.y
                  << ',' << caption_bounds.width << ',' << caption_bounds.height << '\n';
        require(false,
                "quiescent demoboard retained an idle layout, paint, or frame loop");
    }

    const std::shared_ptr<gui_forms::ObjectView> objects = std::dynamic_pointer_cast<gui_forms::ObjectView>(
        (*product).find("fm.folder.objects"));
    const std::shared_ptr<gui_forms::TreeView> tree = std::dynamic_pointer_cast<gui_forms::TreeView>(
        (*product).find("fm.tree.view"));
    const std::shared_ptr<gui_forms::Button> view_command = std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.ribbon.view-mode"));
    const std::shared_ptr<gui_forms::Button> sort_command = std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.ribbon.sort"));
    const std::shared_ptr<gui_forms::PropertyList> property_list = std::dynamic_pointer_cast<gui_forms::PropertyList>(
        (*product).find("fm.selection.properties"));
    const std::shared_ptr<gui_forms::TextBox> property_name = std::dynamic_pointer_cast<gui_forms::TextBox>(
        (*product).find("fm.property.name.editor"));
    const std::shared_ptr<gui_forms::ComboBox> property_handler = std::dynamic_pointer_cast<gui_forms::ComboBox>(
        (*product).find("fm.property.handler.editor"));
    const std::shared_ptr<gui_forms::Button> preview_disclosure = std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.selection.preview-disclosure"));
    const std::shared_ptr<gui_forms::Button> selection_collapse = std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.selection.collapse"));
    const std::shared_ptr<gui_forms::SplitContainer> selection_split = std::dynamic_pointer_cast<gui_forms::SplitContainer>(
        (*product).find("fm.workspace.content-selection"));
    const gui_forms::Control::Ptr preview =
        (*product).find("fm.selection.preview");
    require(objects && tree && view_command && sort_command && property_list &&
                property_name && property_handler && preview_disclosure && preview &&
                selection_collapse && selection_split,
            "D2 public controls are not present in the product tree");

    require((*property_list).header_content() ==
                (*product).find("fm.selection.summary") &&
                (*property_list).header_height.read() == 212.0 &&
                (*property_list).content_height() >
                    (*property_list).header_height.read() &&
                (*property_name).parent() == property_list &&
                (*(*property_list).header_content()).parent() == property_list,
            "Selection preview and grouped properties must share one owned scroll plane");
    require((*product).request_focus(property_name),
            "session Name property must accept focus");
    (*property_name).select_all();
    require((*product).dispatch_text({"File Manager"}),
            "duplicate session rename text input must be handled");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::enter}),
            "duplicate session rename Enter commit must be handled");
    const gui_forms::EventStatistics rename_commit_statistics =
        (*property_list).value_committed().statistics();
    require((*property_name).text.read() == "Facade Study.png",
            std::string("duplicate session rename must restore the previous editor value; actual: ") +
                std::string((*property_name).text.read()) + "; model: " +
                (*property_list).value("fm.property.name").value_or("<missing>") +
                "; accessible: " + (*property_name).accessible_description.read() +
                "; status: " + (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.status.authority"))).text.read() +
                "; connected/emitted/disconnected: " +
                std::to_string(rename_commit_statistics.subscriptions_connected) + "/" +
                std::to_string(rename_commit_statistics.callbacks_emitted) + "/" +
                std::to_string(rename_commit_statistics.subscriptions_disconnected) +
                "; selection count/id: " +
                std::to_string((*objects).selected_ids().size()) + "/" +
                std::string((*objects).selected_id.read()));
    require((*property_name).accessible_description.read().find("already exists") !=
                std::string::npos,
            "duplicate session rename must publish inline accessible validation; actual: " +
                (*property_name).accessible_description.read());
    require((*std::dynamic_pointer_cast<gui_forms::Label>(
                (*product).find("fm.status.authority"))).text.read().find(
                "rename rejected") != std::string::npos,
            "duplicate session rename must publish status authority rejection");
    (*property_name).select_all();
    require((*product).dispatch_text({"Facade Final.png"}) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::enter}) &&
                (*property_list).value("fm.property.name") == "Facade Final.png" &&
                (*property_name).accessible_description.read().find("Error:") ==
                    std::string::npos &&
                std::find_if((*objects).items().begin(), (*objects).items().end(),
                             is_renamed_facade_study) !=
                    (*objects).items().end() &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.selection.object-name"))).text.read() ==
                    "Facade Final.png",
            "valid session rename must update one coherent object/property projection");
    (*property_handler).selected_index = 1U;
    require((*property_list).value("fm.property.handler") == "Image Laboratory" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.status.authority"))).text.read().find(
                    "handler committed") != std::string::npos,
            "choice property must commit through the shared PropertyList model");
    (*preview_disclosure).perform_click();
    require(!(*preview).visible.read() && (*property_list).header_height.read() == 34.0 &&
                (*preview_disclosure).text.read() == "▶",
            "preview disclosure must collapse retained header content without replacing it");
    (*preview_disclosure).perform_click();
    require((*preview).visible.read() && (*property_list).header_height.read() == 212.0 &&
                (*preview_disclosure).text.read() == "▼" &&
                (*property_list).header_content() ==
                    (*product).find("fm.selection.summary"),
            "preview disclosure must restore the same retained header subtree");
    (*selection_split).splitter_distance = 0.0;
    (*product).perform_layout();
    require((*(*selection_split).second_panel()).arranged_bounds().width <= 420.01 &&
                std::abs((*property_list).committed_arranged_bounds().width -
                         (*(*selection_split).second_panel()).arranged_bounds().width) <
                    0.01,
            "widening Selection must stop at its useful content extent and fill it");
    (*selection_split).splitter_distance = 935.0;
    (*product).perform_layout();
    const double inspector_extent =
        (*(*selection_split).second_panel()).arranged_bounds().width;
    (*selection_collapse).perform_click();
    (*product).perform_layout();
    require((*selection_split).second_collapsed() &&
                !(*(*selection_split).second_panel()).visible.read(),
            "Selection caption affordance must collapse the reusable split pane");
    (*std::dynamic_pointer_cast<gui_forms::Button>(
        (*product).find("fm.ribbon.properties"))).perform_click();
    (*product).perform_layout();
    require(!(*selection_split).second_collapsed() &&
                (*(*selection_split).second_panel()).visible.read() &&
                std::abs((*(*selection_split).second_panel()).arranged_bounds().width -
                         inspector_extent) < 0.01,
            "Selection-pane command must restore the remembered inspector extent");
    require((*product).request_focus(objects) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::f2}) &&
                (*product).focused_control() == property_name &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.status.authority"))).text.read().find(
                    "Rename active") != std::string::npos,
            "F2 must route from the object field into the retained Name editor");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::escape}),
            "Escape must cancel the active property rename editor");
    const gui_forms::SemanticSnapshot inspection_semantics =
        (*product).semantic_snapshot();
    const gui_forms::SemanticNode* inspection_grid = find_semantic(
        inspection_semantics.roots, "fm.selection.properties");
    const gui_forms::SemanticNode* inspection_group = find_semantic(
        inspection_semantics.roots, "fm.property.group.editable");
    require(inspection_grid &&
                (*inspection_grid).role == gui_forms::SemanticRole::property_grid &&
                inspection_group &&
                (*inspection_group).role == gui_forms::SemanticRole::property_group,
            "Selection properties must expose reusable property grid/group semantics");

    (*view_command).perform_click();
    require((*objects).view_mode.read() == gui_forms::ObjectViewMode::details &&
                (*std::dynamic_pointer_cast<gui_forms::Button>(
                    (*product).find("fm.status.view"))).text.read() == "Details  ▼" &&
                (*objects).selected_id.read() == "fm.object.obj-facade-study",
            "shared view command did not preserve selection across presentations");
    require((*objects).on_semantic_child_action(
                "fm.object.obj-file-manager", gui_forms::SemanticAction::select, {}) &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.selection.object-name"))).text.read() == "File Manager" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.status.summary"))).text.read().find("1.3G") !=
                    std::string::npos,
            "object selection did not update the shared Selection/status projection");
    (*objects).set_selected_ids({"fm.object.obj-file-manager",
                               "fm.object.obj-facade-study"},
                              "fm.object.obj-file-manager");
    require((*objects).selected_ids().size() == 2U &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.selection.object-name"))).text.read() ==
                    "2 objects selected" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.status.summary"))).text.read().starts_with(
                    "2 selected"),
            "multi-selection did not project one coherent Selection/status state");
    (*sort_command).perform_click();
    require((*objects).items().front().stable_id ==
                "fm.object.obj-delivery-archive" &&
                (*objects).selected_id.read() == "fm.object.obj-file-manager" &&
                (*objects).selected_ids().size() == 2U &&
                (*sort_command).text.read() == "Sort Z→A  ▼",
            "shared sort command did not preserve the stable selection set");
    require((*product).request_focus(objects),
            "object field must accept focus before context-menu exercise");
    secondary_click(*product,
                    virtual_item_center(*objects, "fm.object.obj-file-manager"));
    require((*product).find("fm.context.object.overlay.panel.0") != nullptr &&
                (*product).focus_scope_depth() == 1U &&
                (*objects).selected_ids().size() == 2U,
            "object context menu did not preserve the existing multiselection");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::end}),
            "object context menu must handle End");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::enter}),
            "object context menu must handle Enter");
    const gui_forms::Control::Ptr context_overlay_after_enter =
        (*product).find("fm.context.object.overlay.panel.0");
    const gui_forms::Control::Ptr context_focus_after_enter = (*product).focused_control();
    const std::shared_ptr<gui_forms::Label> context_authority_after_enter =
        std::dynamic_pointer_cast<gui_forms::Label>(
            (*product).find("fm.status.authority"));
    if (context_overlay_after_enter || (*product).focus_scope_depth() != 0U ||
        context_focus_after_enter != property_list || !context_authority_after_enter ||
        (*context_authority_after_enter).text.read().find("properties active") ==
            std::string::npos) {
        throw std::runtime_error(
            "object context post-Enter mismatch: overlay=" +
            std::to_string(static_cast<bool>(context_overlay_after_enter)) +
            " scope-depth=" +
            std::to_string((*product).focus_scope_depth()) + " focus=" +
            (context_focus_after_enter
                 ? std::string((*context_focus_after_enter).stable_id().value())
                 : std::string("<none>")) +
            " authority=" +
            (context_authority_after_enter
                 ? (*context_authority_after_enter).text.read()
                 : std::string("<missing>")));
    }

    const gui_forms::Rect object_bounds = (*objects).absolute_bounds();
    secondary_click(*product, {object_bounds.x + object_bounds.width - 6.0,
                               object_bounds.y + object_bounds.height - 6.0});
    require((*product).find("fm.context.background.overlay.panel.0") != nullptr &&
                (*objects).selected_ids().empty(),
            "background context must clear object selection and expose its own menu");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::enter}) &&
                (*objects).items().size() == 15U &&
                (*objects).selected_id.read() == "fm.object.session-new-folder-1" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.selection.object-name"))).text.read() == "New Folder",
            "background New folder must create and project a session-only fixture");

    secondary_click(*product,
                    virtual_item_center(*objects, "fm.object.session-new-folder-1"));
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::home}) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::down}) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::down}) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::down}) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::down}) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::down}) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::down}) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::enter}) &&
                (*objects).items().size() == 14U && (*objects).selected_ids().empty(),
            "destructive fixture command must hide only the selected session item");
    require((*tree).on_semantic_child_action(
                "fm.tree.node.reference", gui_forms::SemanticAction::select, {}) &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.status.authority"))).text.read().find("reference") !=
                    std::string::npos,
            "tree selection did not publish deterministic fixture navigation state");
    require((*objects).items().size() == 3U &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.title.text"))).text.read() ==
                    "File Manager  ·  Reference" &&
                (*std::dynamic_pointer_cast<gui_forms::Button>(
                    (*product).find("fm.nav.back"))).enabled.read() &&
                !(*std::dynamic_pointer_cast<gui_forms::Button>(
                    (*product).find("fm.nav.forward"))).enabled.read(),
            "tree navigation did not atomically project location objects and history");
    require((*product).request_focus(objects) &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::left,
                                       gui_forms::Modifier::alt}) &&
                (*objects).items().size() == 14U &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.title.text"))).text.read().ends_with("Projects") &&
                (*std::dynamic_pointer_cast<gui_forms::Button>(
                    (*product).find("fm.nav.forward"))).enabled.read(),
            "Alt+Left did not restore the exact retained Projects history entry");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::right,
                                   gui_forms::Modifier::alt}) &&
                (*objects).items().size() == 3U &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.title.text"))).text.read().ends_with("Reference"),
            "Alt+Right did not restore the forward location entry");
    require((*product).dispatch_key({gui_forms::KeyAction::down,
                                   gui_forms::PhysicalKey::up,
                                   gui_forms::Modifier::alt}) &&
                (*objects).items().size() == 3U &&
                (*objects).items().front().name == "Projects" &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.title.text"))).text.read().ends_with("Work") &&
                !(*std::dynamic_pointer_cast<gui_forms::Button>(
                    (*product).find("fm.nav.forward"))).enabled.read(),
            "Alt+Up did not navigate to the parent and clear the forward branch");

    const std::shared_ptr<gui_forms::MenuStrip> menu = std::dynamic_pointer_cast<gui_forms::MenuStrip>(
        (*product).find("fm.ribbon.tabs"));
    require(menu && (*product).perform_semantic_action(
                "fm.menu.help", gui_forms::SemanticAction::expand) &&
                (*product).find("fm.ribbon.tabs.menu.overlay.panel.0") != nullptr &&
                (*product).dispatch_key({gui_forms::KeyAction::down,
                                       gui_forms::PhysicalKey::enter}) &&
                (*std::dynamic_pointer_cast<gui_forms::Label>(
                    (*product).find("fm.status.authority"))).text.read().find(
                    "Native GUI.Forms dogfood") != std::string::npos,
            "application menu semantic path did not execute its shared command");

    std::unique_ptr<gui_forms::Window> responsive = file_manager_demoboard::make_product_window();
    const std::shared_ptr<gui_forms::SplitContainer> responsive_outer =
        std::dynamic_pointer_cast<gui_forms::SplitContainer>(
            (*responsive).find("fm.workspace"));
    const std::shared_ptr<gui_forms::SplitContainer> responsive_inner =
        std::dynamic_pointer_cast<gui_forms::SplitContainer>(
            (*responsive).find("fm.workspace.content-selection"));
    const gui_forms::Control::Ptr responsive_objects =
        (*responsive).find("fm.folder.objects");
    const gui_forms::Control::Ptr title_band =
        (*responsive).find("fm.title.identity");
    const gui_forms::Control::Ptr ribbon_band =
        (*responsive).find("fm.ribbon.shelf");
    const gui_forms::Control::Ptr status_band = (*responsive).find("fm.status");
    require(responsive_outer && responsive_inner && responsive_objects &&
                title_band && ribbon_band && status_band,
            "responsive fixture requires both split policies and shell bands");
    resize_and_layout_twice(*responsive, {1200, 760});
    require(!(*responsive_outer).first_collapsed() &&
                !(*responsive_inner).second_collapsed() && (*ribbon_band).visible.read(),
            "common size must retain both panes and the full command shelf");
    resize_and_layout_twice(*responsive, {960, 680});
    require(!(*responsive_outer).first_collapsed() &&
                (*responsive_inner).second_collapsed() &&
                (*responsive_inner).second_collapse_origin() ==
                    gui_forms::SplitCollapseOrigin::automatic_accommodation,
            "narrow size must automatically collapse Selection before the tree");
    resize_and_layout_twice(*responsive, {720, 520});
    require(!(*responsive_outer).first_collapsed() &&
                (*responsive_inner).second_collapsed() && !(*ribbon_band).visible.read(),
            "compact size must preserve the tree and move ribbon vocabulary to menus");
    resize_and_layout_twice(*responsive, {480, 360});
    require((*responsive_outer).first_collapsed() &&
                (*responsive_outer).first_collapse_origin() ==
                    gui_forms::SplitCollapseOrigin::automatic_accommodation &&
                (*responsive_inner).second_collapsed(),
            "severe size must collapse both secondary panes around one content field");
    resize_and_layout_twice(*responsive, {300, 240});
    require((*responsive_objects).committed_arranged_bounds().width > 250.0 &&
                (*responsive_objects).committed_arranged_bounds().height > 90.0,
            "extreme size must preserve a useful current-location content region");
    resize_and_layout_twice(*responsive, {150, 150});
    require(!(*title_band).visible.read() && !(*ribbon_band).visible.read() &&
                !(*status_band).visible.read() &&
                (*responsive_objects).committed_arranged_bounds().width > 120.0 &&
                (*responsive_objects).committed_arranged_bounds().height > 60.0 &&
                (*(*responsive).find("fm.ribbon.tabs")).visible.read() &&
                (*(*responsive).find("fm.navigation")).visible.read(),
            "enforced minimum must retain menu, location controls, and one usable region");
    resize_and_layout_twice(*responsive, {1450, 850});
    require(!(*responsive_outer).first_collapsed() &&
                !(*responsive_inner).second_collapsed() && (*title_band).visible.read() &&
                (*ribbon_band).visible.read() && (*status_band).visible.read(),
            "restoring reference size must restore only automatically hidden surfaces");
    const gui_forms::Rect reference_object_bounds =
        (*responsive_objects).committed_arranged_bounds();
    resize_and_layout_twice(*responsive, {1800, 1050});
    const gui_forms::Rect expanded_selection =
        (*(*responsive).find("fm.selection.pane")).absolute_bounds();
    require((*(*responsive).find("fm.title.identity")).committed_arranged_bounds().width ==
                1800.0 &&
                (*(*responsive).find("fm.workspace")).committed_arranged_bounds().width ==
                1800.0 &&
                (*responsive_objects).committed_arranged_bounds().width >
                    reference_object_bounds.width &&
                (*responsive_objects).committed_arranged_bounds().height >
                    reference_object_bounds.height &&
                std::abs(expanded_selection.x + expanded_selection.width -
                         1800.0) < 0.01,
            "growing beyond startup size must fill every shell band and expand the primary content field to the new window edges");
    resize_and_layout_twice(*responsive, {1450, 850});

    const std::shared_ptr<gui_forms::PropertyList> responsive_properties =
        std::dynamic_pointer_cast<gui_forms::PropertyList>(
            (*responsive).find("fm.selection.properties"));
    require(responsive_properties != nullptr,
            "large-text matrix requires the public Selection PropertyList");
    const double normal_property_content = (*responsive_properties).content_height();
    (*responsive).set_text_scale(1.25);
    (*responsive).perform_layout();
    (*responsive).perform_layout();
    require((*(*responsive).find("fm.ribbon.tabs")).committed_arranged_bounds().height ==
                23.0 * 1.25 &&
                (*(*responsive).find("fm.navigation")).committed_arranged_bounds().height ==
                40.0 * 1.25 &&
                (*responsive_properties).content_height() > normal_property_content &&
                !(*responsive_outer).first_collapsed() &&
                !(*responsive_inner).second_collapsed(),
            "125% text must grow primary bands and inspector rows without premature collapse");
    (*responsive).set_text_scale(1.5);
    (*responsive).perform_layout();
    (*responsive).perform_layout();
    require(!(*responsive_outer).first_collapsed() &&
                (*responsive_inner).second_collapsed() && (*ribbon_band).visible.read(),
            "150% text must stack inspector content and collapse Selection before tree");
    (*responsive).set_text_scale(2.0);
    (*responsive).perform_layout();
    (*responsive).perform_layout();
    require(!(*responsive_outer).first_collapsed() &&
                (*responsive_inner).second_collapsed() && !(*ribbon_band).visible.read() &&
                (*(*responsive).find("fm.ribbon.tabs")).visible.read() &&
                (*(*responsive).find("fm.navigation")).visible.read(),
            "200% text must withdraw the ribbon while preserving menu, location, and tree");
    (*responsive).set_text_scale(2.25);
    (*responsive).perform_layout();
    (*responsive).perform_layout();
    require((*responsive_outer).first_collapsed() &&
                (*responsive_inner).second_collapsed() &&
                (*responsive_outer).first_collapse_origin() ==
                    gui_forms::SplitCollapseOrigin::automatic_accommodation &&
                (*responsive_objects).committed_arranged_bounds().width > 1300.0,
            "225% text must collapse secondary panes and preserve the primary object field");
    (*responsive).set_text_scale(1.0);
    (*responsive).perform_layout();
    (*responsive).perform_layout();
    require(!(*responsive_outer).first_collapsed() &&
                !(*responsive_inner).second_collapsed() && (*ribbon_band).visible.read(),
            "returning to 100% must restore only automatically accommodated surfaces");

    std::unique_ptr<gui_forms::Window> controller = file_manager_demoboard::make_controller_window(responsive.get());
    const std::shared_ptr<gui_forms::ComboBox> surface_choice = std::dynamic_pointer_cast<gui_forms::ComboBox>(
        (*controller).find("demo.surface.choice"));
    require(surface_choice != nullptr,
            "controller surface selector is absent");
    require((*controller).find("demo.diagnostics") != nullptr,
            "controller capability report is absent");
    const std::shared_ptr<gui_forms::ComboBox> text_scale = std::dynamic_pointer_cast<gui_forms::ComboBox>(
        (*controller).find("demo.accommodation.text-scale"));
    require(text_scale && (*text_scale).enabled.read(),
            "controller text-scale accommodation selector is absent");
    const std::shared_ptr<gui_forms::CheckBox> sound = std::dynamic_pointer_cast<gui_forms::CheckBox>(
        (*controller).find("demo.accommodation.sound"));
    const std::shared_ptr<gui_forms::CheckBox> reduced_motion = std::dynamic_pointer_cast<gui_forms::CheckBox>(
        (*controller).find("demo.accommodation.reduced-motion"));
    require(sound && (*sound).checked.read(),
            "controller semantic-sound policy toggle is absent");
    require(reduced_motion && !(*reduced_motion).checked.read(),
            "controller reduced-motion policy toggle is absent");
    (*surface_choice).selected_index = 1U;
    require((*(*responsive).find("fm.search.surface")).visible.read() &&
                !(*(*responsive).find("fm.workspace.content-selection")).visible.read(),
            "controller Search choice must reach the public product-surface transition");
    (*surface_choice).selected_index = 2U;
    require((*(*responsive).find("fm.criteria.surface")).visible.read() &&
                (*(*responsive).find("fm.workspace.content-selection")).visible.read() &&
                !(*(*responsive).find("fm.folder.objects")).visible.read(),
            "controller Criteria choice must reach the public retained surface transition");
    (*surface_choice).selected_index = 3U;
    require((*(*responsive).find("fm.review.palettes")).visible.read() &&
                !(*(*responsive).find("fm.workspace.content-selection")).visible.read() &&
                !(*(*responsive).find("fm.search.surface")).visible.read() &&
                !(*(*responsive).find("fm.criteria.surface")).visible.read(),
            "controller Palettes choice must reach the public Card laboratory");
    (*surface_choice).selected_index = 6U;
    require((*(*responsive).find("fm.review.dna")).visible.read() &&
                !(*(*responsive).find("fm.review.palettes")).visible.read() &&
                !(*(*responsive).find("fm.workspace.content-selection")).visible.read(),
            "controller DNA choice must reach the public MasterDetail laboratory");
    (*surface_choice).selected_index = 0U;
    require(!(*(*responsive).find("fm.search.surface")).visible.read() &&
                !(*(*responsive).find("fm.review.palettes")).visible.read() &&
                !(*(*responsive).find("fm.review.dna")).visible.read() &&
                (*(*responsive).find("fm.workspace.content-selection")).visible.read(),
            "controller Folder choice must restore the retained Folder projection");
    (*text_scale).selected_index = 4U;
    require((*responsive).presentation_settings().text_scale == 2.25,
            "controller text-scale selector did not reach the product presentation contract");
    (*text_scale).selected_index = 0U;
    require((*responsive).presentation_settings().text_scale == 1.0,
            "controller did not restore the product to 100% text");
    const gui_forms::SemanticSnapshot semantic_before_sound_toggle =
        (*responsive).semantic_snapshot();
    (*sound).checked = false;
    require(!(*responsive).presentation_settings().sound_enabled &&
                (*responsive).semantic_snapshot().node_count ==
                    semantic_before_sound_toggle.node_count,
            "sounds-off must preserve the product semantic state");
    (*sound).checked = true;
    (*reduced_motion).checked = true;
    require((*responsive).presentation_settings().reduced_motion,
            "controller reduced-motion toggle did not reach the product presentation contract");
    (*reduced_motion).checked = false;

    require_smoke_verdict();

    std::cout << "demoboard fixture, reference geometry, semantics, controller, and smoke verdict pass\n";
}
