// PageView (stage DK, phase P3): the strip preset over the view's own set,
// the content area and the panels, event order around a selection, a
// cancelled selection keeping everything, focus remembered per page, late
// content requested exactly once, nested views and key routing (Ctrl+Tab
// policy, Ctrl+PageDown innermost first), F6 regions, take and put between
// two views, and the semantic panels linked from their tabs.

#include "gui_forms/anatomy/types/anatomy_types.hpp"
#include "gui_forms/binding/property_type/property_type.hpp"
#include "gui_forms/control/control/control.hpp"
#include "gui_forms/control/stable_id/stable_id.hpp"
#include "gui_forms/controls/button.hpp"
#include "gui_forms/controls/label.hpp"
#include "gui_forms/controls/page_strip.hpp"
#include "gui_forms/controls/page_strip_values.hpp"
#include "gui_forms/controls/page_view.hpp"
#include "gui_forms/controls/page_view_values.hpp"
#include "gui_forms/controls/panel.hpp"
#include "gui_forms/controls/text_box.hpp"
#include "gui_forms/event/subscription_token/subscription_token.hpp"
#include "gui_forms/events/input_events/input_events.hpp"
#include "gui_forms/pages/page_set/page_set.hpp"
#include "gui_forms/pages/types/page_set_types.hpp"
#include "gui_forms/rules/rule/rule.hpp"
#include "gui_forms/semantics/types/semantic_types.hpp"
#include "gui_forms/text/markdown/markdown.hpp"
#include "gui_forms/types/geometry/geometry.hpp"
#include "gui_forms/window/window.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace gui_forms;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << message << '\n';
    std::exit(1);
}

KeyEvent key(std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{};
    event.action = KeyAction::down;
    event.physical_key = physical;
    event.modifiers = modifiers;
    return event;
}

PageRecord record(std::string_view id, std::string_view title) {
    PageRecord page{};
    page.id = PageId{std::string(id)};
    page.title = std::string(title);
    return page;
}

std::string selected_id(const PageSet& pages) {
    if (!pages.selected_page()) return {};
    return (*pages.selected_page()).value;
}

// Borrows from `nodes`: the pointer is valid while that vector lives. The
// deleted overload refuses a temporary (house style section 1).
const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == id) return &node;
        const SemanticNode* const inner = find_node(node.children, id);
        if (inner != nullptr) return inner;
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view id) = delete;

std::size_t count_role(const std::vector<SemanticNode>& nodes, SemanticRole role) {
    std::size_t count = 0U;
    for (const SemanticNode& node : nodes) {
        if (node.role == role) ++count;
        count += count_role(node.children, role);
    }
    return count;
}

struct Trace final {
    std::vector<std::string> events;
};

struct RecordSelecting final {
    Trace* trace{};
    void operator()(PageSelectingEvent& event) const {
        (*trace).events.push_back("selecting " + event.new_page.value);
    }
};

struct CancelSelecting final {
    bool* active{};
    void operator()(PageSelectingEvent& event) const {
        if (*active) event.cancel = true;
    }
};

// Records the selection and whether the new page's content was already
// shown when the application heard of it.
struct RecordSelected final {
    Trace* trace{};
    PageView* view{};
    void operator()(const PageSelectionChange& change) const {
        if (!change.new_page) return;
        const Control::Ptr content = (*view).page_content(*change.new_page);
        const bool shown = content && (*content).effectively_visible();
        (*trace).events.push_back("selected " + (*change.new_page).value + (shown ? " shown" : " empty"));
    }
};

struct RecordInvoked final {
    Trace* trace{};
    void operator()(const PageInvocation& invocation) const {
        (*trace).events.push_back("invoked " + invocation.page.value);
    }
};

// Supplies a label for the pages named "late.*" and leaves the others empty.
struct SupplyContent final {
    Trace* trace{};
    PageView* view{};
    void operator()(const PageContentRequest& request) const {
        (*trace).events.push_back("content " + request.page.value);
        if (request.page.value.rfind("late", 0U) != 0U) return;
        std::shared_ptr<Label> made = make_control<Label>(StableId("made." + request.page.value), "Made");
        const PageEditStatus status = (*view).set_page_content(request.page, made);
        require(status == PageEditStatus::applied, "late content attaches from the request");
    }
};

std::shared_ptr<TextBox> text_box(std::string_view id) {
    std::shared_ptr<TextBox> box = make_control<TextBox>(StableId(std::string(id)));
    return box;
}

// -- tests ---------------------------------------------------------------------

void anatomy_and_layout() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("view"));
    require((*view).add_page(record("a", "Alpha"), make_control<Label>(StableId("a.label"), "A")) ==
                    PageEditStatus::applied &&
                (*view).add_page(record("b", "Beta"), make_control<Label>(StableId("b.label"), "B")) ==
                    PageEditStatus::applied,
            "pages with content are added");
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    const ControlAnatomy& anatomy = (*view).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "page_view" &&
                anatomy.item_systems.empty(),
            "PageView publishes a valid anatomy of its own, its tabs belonging to its strip");
    const PageView::Parts parts = (*view).parts();
    require(parts.strip.present && parts.content.present && parts.strip.bounds.y == 0.0 &&
                parts.content.bounds.y == parts.strip.bounds.height &&
                parts.strip.bounds == (*(*view).strip()).committed_arranged_bounds(),
            "the strip sits at the top and the content area fills the rest");
    const Control::Ptr alpha = (*view).page_content(PageId{"a"});
    const Control::Ptr beta = (*view).page_content(PageId{"b"});
    require(alpha && beta && (*alpha).effectively_visible() && !(*beta).effectively_visible(),
            "only the selected page's content is visible");
    const Rect content = (*alpha).absolute_bounds();
    require(content.y >= parts.content.bounds.y && content.height > 0.0 &&
                content.y + content.height <= parts.content.bounds.y + parts.content.bounds.height,
            "the selected page's control fills the content area");
    (*(*view).strip()).placement = PageStripPlacement::bottom;
    window.perform_layout();
    const PageView::Parts below = (*view).parts();
    require(below.content.bounds.y == 0.0 && below.strip.bounds.y == below.content.bounds.height,
            "a bottom strip puts the content above it");
    require((*view).add_page(record("a", "Again"), Control::Ptr{}) == PageEditStatus::duplicate_page &&
                (*view).pages().count() == 2U,
            "the set's refusals reach the caller and change nothing");
}

void event_order_and_cancel() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("view"));
    Trace trace{};
    SubscriptionToken supplied = (*view).content_requested().subscribe(SupplyContent{&trace, view.get()});
    require((*view).add_page(record("a", "Alpha"), make_control<Label>(StableId("a.label"), "A")) ==
                PageEditStatus::applied,
            "the first page has content");
    require((*view).add_page(record("late.b", "Beta"), Control::Ptr{}) == PageEditStatus::applied,
            "the second page has none yet");
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    SubscriptionToken selecting = (*view).pages().selecting().subscribe(RecordSelecting{&trace});
    SubscriptionToken selected =
        (*view).pages().selected_page_changed().subscribe(RecordSelected{&trace, view.get()});
    SubscriptionToken invoked = (*(*view).strip()).page_invoked().subscribe(RecordInvoked{&trace});
    trace.events.clear();
    const PageStrip::Item second = (*(*view).strip()).realized_items()[1];
    const Point point{second.parts.tab.bounds.x + 4.0, second.parts.tab.bounds.y + 4.0};
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}), "the tab takes the press");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
    const std::vector<std::string> expected{"selecting late.b", "content late.b", "selected late.b shown",
                                            "invoked late.b"};
    require(trace.events == expected,
            "selecting, then the late content, then the application hears of a page already shown, then invoked");

    // Cancelled: the selection, the visible content and the focus stay.
    bool cancelling = true;
    SubscriptionToken cancel = (*view).pages().selecting().subscribe(CancelSelecting{&cancelling});
    const Control::Ptr focused = window.focused_control();
    trace.events.clear();
    require(window.dispatch_key(key(PhysicalKey::page_up, Modifier::control)), "Ctrl+PageUp is handled");
    const std::vector<std::string> cancelled{"selecting a"};
    const Control::Ptr shown = (*view).page_content(PageId{"late.b"});
    require(trace.events == cancelled && selected_id((*view).pages()) == "late.b" && shown &&
                (*shown).effectively_visible() && !(*(*view).page_content(PageId{"a"})).effectively_visible() &&
                window.focused_control() == focused,
            "a cancelled selection keeps the selection, the shown content and the focus");
    cancelling = false;
}

void late_content_once() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("view"));
    Trace trace{};
    SubscriptionToken supplied = (*view).content_requested().subscribe(SupplyContent{&trace, view.get()});
    require((*view).add_page(record("first", "First"), make_control<Label>(StableId("first.label"), "1")) ==
                PageEditStatus::applied,
            "added");
    require((*view).add_page(record("empty", "Empty"), Control::Ptr{}) == PageEditStatus::applied, "added");
    require((*view).add_page(record("late.c", "Late"), Control::Ptr{}) == PageEditStatus::applied, "added");
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    require(trace.events.empty(), "no content is requested for pages never selected");
    PageSet& pages = (*view).pages();
    for (int round = 0; round < 3; ++round) {
        require(page_edit_succeeded(pages.select(PageId{"empty"}, PageSelectionCause::programmatic)), "selected");
        require(page_edit_succeeded(pages.select(PageId{"late.c"}, PageSelectionCause::programmatic)), "selected");
        require(page_edit_succeeded(pages.select(PageId{"first"}, PageSelectionCause::programmatic)), "selected");
    }
    const std::vector<std::string> expected{"content empty", "content late.c"};
    require(trace.events == expected, "content is requested exactly once per page, on its first selection");
    require(!(*view).page_content(PageId{"empty"}) && (*view).panel_identity(PageId{"empty"}).empty(),
            "a page left without content has no panel");
    require((*view).page_content(PageId{"late.c"}) &&
                (*view).panel_identity(PageId{"late.c"}) == "view.panel.late.c",
            "supplied content gets its panel");
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* const late_tab = find_node(snapshot.roots, "view.strip.tab.late.c");
    const SemanticNode* const empty_tab = find_node(snapshot.roots, "view.strip.tab.empty");
    require(late_tab != nullptr && (*late_tab).controlled_panel == "view.panel.late.c" && empty_tab != nullptr &&
                (*empty_tab).controlled_panel.empty(),
            "a tab links the panel of a page that has content, and only such a tab");
}

void focus_memory() {
    std::shared_ptr<Panel> first = make_control<Panel>(StableId("first"));
    std::shared_ptr<TextBox> first_a = text_box("first.a");
    std::shared_ptr<TextBox> first_b = text_box("first.b");
    (*first_a).set_requested_bounds({10.0, 10.0, 120.0, 24.0});
    (*first_b).set_requested_bounds({10.0, 44.0, 120.0, 24.0});
    (*first).add_child(first_a);
    (*first).add_child(first_b);
    std::shared_ptr<TextBox> second = text_box("second");
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("view"));
    require((*view).add_page(record("one", "One"), first) == PageEditStatus::applied &&
                (*view).add_page(record("two", "Two"), second) == PageEditStatus::applied,
            "added");
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    require(window.request_focus(first_b), "focus starts on the first page's second box");
    require(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control)) &&
                selected_id((*view).pages()) == "two",
            "Ctrl+PageDown from inside the content selects the next page");
    require(window.focused_control() == (*view).strip(),
            "a page with no remembered focus puts the focus on the strip");
    require(window.request_focus(second), "focus moves into the second page");
    require(window.dispatch_key(key(PhysicalKey::page_up, Modifier::control)) &&
                selected_id((*view).pages()) == "one" && window.focused_control() == first_b,
            "returning to a page restores the focus it had there");
    require(window.dispatch_key(key(PhysicalKey::tab, Modifier::control)) && window.focused_control() == second,
            "Ctrl+Tab selects the next page and restores its focus too");
    require(window.dispatch_key(key(PhysicalKey::tab, Modifier::control | Modifier::shift)) &&
                selected_id((*view).pages()) == "one" && window.focused_control() == first_b,
            "Ctrl+Shift+Tab selects the previous page");
    // A click on a tab focuses the strip, whatever the page remembered.
    const PageStrip::Item two = (*(*view).strip()).realized_items()[1];
    const Point point{two.parts.tab.bounds.x + 4.0, two.parts.tab.bounds.y + 4.0};
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
    require(selected_id((*view).pages()) == "two" && window.focused_control() == (*view).strip(),
            "a click selects and focuses the strip");
}

void nested_views_and_key_routing() {
    std::shared_ptr<PageView> inner = make_control<PageView>(StableId("inner"));
    std::shared_ptr<TextBox> inner_box = text_box("inner.box");
    require((*inner).add_page(record("x", "X"), inner_box) == PageEditStatus::applied &&
                (*inner).add_page(record("y", "Y"), make_control<Label>(StableId("y.label"), "Y")) ==
                    PageEditStatus::applied,
            "the inner view has two pages");
    std::shared_ptr<PageView> outer = make_control<PageView>(StableId("outer"));
    require((*outer).add_page(record("a", "A"), inner) == PageEditStatus::applied &&
                (*outer).add_page(record("b", "B"), make_control<Label>(StableId("b.label"), "B")) ==
                    PageEditStatus::applied,
            "the outer view holds the inner one on its first page");
    Window window(outer, {500.0, 300.0});
    window.perform_layout();
    require(window.request_focus(inner_box), "focus is inside the inner view");
    require(window.dispatch_key(key(PhysicalKey::page_down, Modifier::control)) &&
                selected_id((*inner).pages()) == "y" && selected_id((*outer).pages()) == "a",
            "Ctrl+PageDown goes to the innermost view around the focus");
    require(window.request_focus((*inner).strip()), "focus on the inner strip");
    require(window.dispatch_key(key(PhysicalKey::tab, Modifier::control)) &&
                selected_id((*inner).pages()) == "x" && selected_id((*outer).pages()) == "a",
            "Ctrl+Tab goes to the innermost view that handles it");
    (*inner).ctrl_tab = PageViewCtrlTab::pass;
    require(window.dispatch_key(key(PhysicalKey::tab, Modifier::control)) &&
                selected_id((*inner).pages()) == "x" && selected_id((*outer).pages()) == "b",
            "a view that passes Ctrl+Tab leaves it to the view around it");
    require(window.dispatch_key(key(PhysicalKey::tab, Modifier::control)) && selected_id((*outer).pages()) == "a",
            "the outer view's strip, now focused, still takes Ctrl+Tab");
    (*outer).ctrl_tab = PageViewCtrlTab::pass;
    require(window.request_focus((*inner).strip()), "focus on the inner strip again");
    // Unhandled by every view, the key falls to the window's Tab traversal.
    static_cast<void>(window.dispatch_key(key(PhysicalKey::tab, Modifier::control | Modifier::shift)));
    require(selected_id((*outer).pages()) == "a" && selected_id((*inner).pages()) == "x",
            "with every view passing, the selections stay");
}

void focus_regions() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<Button> before = make_control<Button>(StableId("before"), "Before");
    (*before).set_requested_bounds({0.0, 0.0, 80.0, 24.0});
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("view"));
    (*view).set_requested_bounds({0.0, 40.0, 400.0, 200.0});
    (*view).focus_region = true;
    std::shared_ptr<TextBox> box = text_box("view.box");
    require((*view).add_page(record("a", "A"), box) == PageEditStatus::applied, "added");
    (*root).add_child(before);
    (*root).add_child(view);
    Window window(root, {400.0, 260.0});
    window.perform_layout();
    const std::vector<Control::Ptr> regions = window.focus_regions();
    require(regions.size() == 1U && regions[0] == view, "a view set as a region registers itself on attachment");
    require(window.request_focus(before), "focus starts outside the view");
    require(window.dispatch_key(key(PhysicalKey::f6)) && window.focused_control() &&
                (window.focused_control() == (*view).strip() || window.focused_control() == box),
            "F6 moves the focus into the view");
    (*view).focus_region = false;
    require(window.focus_regions().empty(), "turning the region off unregisters the view");
}

void take_and_put_between_views() {
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("root"));
    std::shared_ptr<PageView> left = make_control<PageView>(StableId("left"));
    std::shared_ptr<PageView> right = make_control<PageView>(StableId("right"));
    (*left).set_requested_bounds({0.0, 0.0, 300.0, 200.0});
    (*right).set_requested_bounds({300.0, 0.0, 300.0, 200.0});
    (*root).add_child(left);
    (*root).add_child(right);
    std::shared_ptr<TextBox> moving = text_box("moving");
    require((*left).add_page(record("doc", "Document"), moving) == PageEditStatus::applied &&
                (*left).add_page(record("other", "Other"), make_control<Label>(StableId("o"), "O")) ==
                    PageEditStatus::applied &&
                (*right).add_page(record("r", "R"), make_control<Label>(StableId("r.label"), "R")) ==
                    PageEditStatus::applied,
            "added");
    Window window(root, {600.0, 200.0});
    window.perform_layout();
    (*moving).text = "unsaved words";
    PageTakeResult taken = (*left).pages().take(PageId{"doc"});
    require(taken.status == PageEditStatus::applied && !(*moving).parent(),
            "a taken page's control leaves the view and is kept by the taken page");
    require((*right).pages().put(taken.page, PagePlacement::end_of_group()) == PageEditStatus::applied, "put");
    require(page_edit_succeeded((*right).pages().select(PageId{"doc"}, PageSelectionCause::programmatic)),
            "selected in the new view");
    window.perform_layout();
    require((*right).page_content(PageId{"doc"}) == moving && (*moving).text.read() == "unsaved words" &&
                (*moving).effectively_visible() && (*right).panel_identity(PageId{"doc"}) == "right.panel.doc" &&
                (*left).panel_identity(PageId{"doc"}).empty(),
            "the control moves to the other view with its state and shows there");
    const std::weak_ptr<TextBox> observed = moving;
    moving.reset();
    require(page_edit_succeeded((*right).pages().remove(PageId{"doc"})) && observed.expired(),
            "removing the page releases its control");
}

void semantics() {
    std::shared_ptr<PageView> view = make_control<PageView>(StableId("view"));
    require((*view).add_page(record("a", "&Alpha"), make_control<Label>(StableId("a.label"), "A")) ==
                    PageEditStatus::applied &&
                (*view).add_page(record("b", "Beta"), make_control<Label>(StableId("b.label"), "B")) ==
                    PageEditStatus::applied,
            "added");
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    const SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* const list = find_node(snapshot.roots, "view.strip");
    const SemanticNode* const tab = find_node(snapshot.roots, "view.strip.tab.a");
    const SemanticNode* const panel = find_node(snapshot.roots, "view.panel.a");
    require(list != nullptr && (*list).role == SemanticRole::tab_group, "the strip is the tab list");
    require(tab != nullptr && (*tab).controlled_panel == "view.panel.a", "the tab links its panel");
    require(panel != nullptr && (*panel).role == SemanticRole::tab_panel && (*panel).name == "Alpha" &&
                find_node((*panel).children, "a.label") != nullptr,
            "the panel is a tab panel named by its tab, holding the page's control");
    require(find_node(snapshot.roots, "view.panel.b") == nullptr && count_role(snapshot.roots, SemanticRole::tab_panel) == 1U,
            "only the selected page's panel is published");
    require(find_node(snapshot.roots, "view") == nullptr, "an unnamed view adds no node of its own");
    require(window.perform_semantic_action("view.strip.tab.b", SemanticAction::select) &&
                selected_id((*view).pages()) == "b",
            "selecting a tab through accessibility shows its page");
    const SemanticSnapshot after = window.semantic_snapshot();
    require(find_node(after.roots, "view.panel.b") != nullptr && find_node(after.roots, "view.panel.a") == nullptr,
            "the published panel follows the selection");
}

} // namespace

int main() {
    anatomy_and_layout();
    event_order_and_cancel();
    late_content_once();
    focus_memory();
    nested_views_and_key_routing();
    focus_regions();
    take_and_put_between_views();
    semantics();
    std::cout << "PageView: layout, event order, cancel, late content, focus memory, nested key routing, "
                 "F6 regions, take and put, and panel semantics passed\n";
    return 0;
}
