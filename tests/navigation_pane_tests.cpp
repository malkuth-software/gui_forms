// NavigationPane (stage W, docking phase P7): the large buttons and the grip's
// steps with their semantic value, hidden pages that appear only in the
// configure menu, collapsing and expanding, the collapsed pane's overlay (an
// outside press closes it and passes through; Escape closes it), selection
// and the content shown, the saved state's round trip, the semantic
// snapshot, and the published parts and states.

#include "gui_forms/gui_forms.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
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

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

// The returned pointer borrows from `nodes`, so the vector must outlive it:
// semantic_snapshot() returns a new snapshot, and a pointer into the roots of
// that temporary dangles at the end of the statement. The deleted overload
// refuses a temporary.
const SemanticNode* find_node(const std::vector<SemanticNode>& nodes, std::string_view stable_id) {
    for (const SemanticNode& node : nodes) {
        if (node.stable_id == stable_id) return &node;
        if (const SemanticNode* nested = find_node(node.children, stable_id)) return nested;
    }
    return nullptr;
}
const SemanticNode* find_node(std::vector<SemanticNode>&& nodes, std::string_view stable_id) = delete;

const SemanticNode* find_named(const std::vector<SemanticNode>& nodes, SemanticRole role, std::string_view name) {
    for (const SemanticNode& node : nodes) {
        if (node.role == role && node.name == name) return &node;
        if (const SemanticNode* nested = find_named(node.children, role, name)) return nested;
    }
    return nullptr;
}
// Borrows from `nodes` as find_node does; temporaries are refused.
const SemanticNode* find_named(std::vector<SemanticNode>&& nodes, SemanticRole role, std::string_view name) = delete;

const NavigationPane::Item* find_item(const std::vector<NavigationPane::Item>& items, std::string_view page) {
    for (const NavigationPane::Item& item : items) {
        if (item.page.value == page) return &item;
    }
    return nullptr;
}
// Borrows from `items` as find_node does; temporaries are refused.
const NavigationPane::Item* find_item(std::vector<NavigationPane::Item>&& items, std::string_view page) = delete;

void click(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

void key(Window& window, std::uint32_t physical_key, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, physical_key};
    event.modifiers = modifiers;
    static_cast<void>(window.dispatch_key(event));
}

// Records every state the pane reports.
struct RecordState final {
    std::vector<NavigationPaneState>* seen{};
    void operator()(const NavigationPaneState& state) const { (*seen).push_back(state); }
};

// Records the order of the overlay's closing and a press reaching a control.
struct RecordOverlay final {
    std::vector<std::string>* order{};
    void operator()(bool open) const { (*order).push_back(open ? "opened" : "closed"); }
};

struct RecordPress final {
    std::vector<std::string>* order{};
    void operator()(const PointerEvent& event) const {
        if (event.action == PointerAction::down) (*order).push_back("outside pressed");
    }
};

struct CountOptions final {
    int* count{};
    void operator()(const NavigationPaneOptionsRequest& request) const {
        if (request.pane_id == "nav") ++*count;
    }
};

constexpr std::array<std::string_view, 6> page_ids{"mail", "calendar", "contacts", "tasks", "notes", "folders"};
constexpr std::array<std::string_view, 6> page_titles{"Mail", "Calendar", "Contacts", "Tasks", "Notes", "Folders"};

// A window with the pane docked left, a button beside it, and six pages,
// each with a label as its content.
struct Fixture final {
    std::shared_ptr<Panel> root;
    std::shared_ptr<NavigationPane> pane;
    std::shared_ptr<Button> outside;
    std::vector<std::shared_ptr<Label>> contents;
    std::unique_ptr<Window> window;
};

std::unique_ptr<Fixture> make_fixture(Size client = {640.0, 420.0}) {
    std::unique_ptr<Fixture> fixture = std::make_unique<Fixture>();
    (*fixture).root = make_control<Panel>(StableId("root"));
    (*fixture).pane = make_control<NavigationPane>(StableId("nav"));
    (*(*fixture).pane).dock = DockStyle::left;
    (*(*fixture).root).add_child((*fixture).pane);
    (*fixture).outside = make_control<Button>(StableId("outside"), "Outside");
    (*(*fixture).outside).set_requested_bounds({400.0, 20.0, 120.0, 30.0});
    (*(*fixture).root).add_child((*fixture).outside);
    PageSet& pages = (*(*fixture).pane).pages();
    for (std::size_t index = 0; index < page_ids.size(); ++index) {
        const PageRecord record{.id = PageId{std::string(page_ids[index])}, .title = std::string(page_titles[index])};
        require(pages.insert(record, PagePlacement::end_of_group()) == PageEditStatus::applied, "a page enters the set");
        std::shared_ptr<Label> label =
            make_control<Label>(StableId("content." + std::string(page_ids[index])), std::string(page_titles[index]));
        (*fixture).contents.push_back(label);
        std::unique_ptr<PageContent> content = std::make_unique<PageControlContent>(label);
        require(pages.set_content(record.id, content) == PageEditStatus::applied && !content,
                "a page takes a control as its content");
    }
    (*fixture).window = std::make_unique<Window>((*fixture).root, client);
    (*(*fixture).window).perform_layout();
    return fixture;
}

void layout_and_content() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    NavigationPane& pane = *(*fixture).pane;
    Window& window = *(*fixture).window;
    const ControlAnatomy& anatomy = pane.anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "navigation_pane" &&
                anatomy.item_systems.size() == 2U && anatomy.item_systems[0].name == "nav_page" &&
                anatomy.item_systems[1].name == "nav_overflow",
            "NavigationPane publishes a valid anatomy with its two item systems");
    require(pane.committed_arranged_bounds().width == 220.0 && pane.committed_arranged_bounds().height == 420.0,
            "a pane docked left takes its expanded width and the full height");
    require(pane.button_count.read() == 4U && pane.visible_button_count() == 4U, "four large buttons by default");
    const std::vector<NavigationPane::Item> items = pane.realized_items();
    require(items.size() == 6U, "every page is a large button or an icon");
    for (std::size_t index = 0; index < 4U; ++index) {
        require(items[index].kind == NavigationPaneItemKind::large_button && items[index].page.value == page_ids[index] &&
                    items[index].button.nav_page_face.present && items[index].button.nav_page_label.present &&
                    !items[index].icon.nav_overflow_face.present,
                "the first pages are large buttons in page order, with a face and a label");
    }
    require(items[4].kind == NavigationPaneItemKind::icon && items[5].kind == NavigationPaneItemKind::icon &&
                items[4].icon.nav_overflow_face.present && items[4].icon.nav_overflow_label.present &&
                !items[4].icon.nav_overflow_icon.present,
            "the rest are overflow icons; without an image an icon shows its initial");
    require(items[4].icon.nav_overflow_face.bounds.x < items[5].icon.nav_overflow_face.bounds.x,
            "overflow icons run in page order");
    require(items[0].stable_id == "nav.tab.mail" && items[0].states.selected && !items[1].states.selected,
            "the first page entered the empty set and is selected");

    const NavigationPane::Parts parts = pane.parts();
    require(parts.header.present && parts.header_label.present && parts.collapse_button.present &&
                parts.collapse_glyph.present && parts.content.present && parts.grip_bar.present && parts.grip.present &&
                parts.button_area.present && parts.overflow_area.present && parts.configure_button.present &&
                parts.configure_glyph.present && parts.focus.present,
            "an expanded pane shows its header, content, grip, buttons and overflow strip");
    require(!parts.collapsed_bar.present && !parts.collapsed_label.present && !parts.overlay_face.present &&
                !parts.header_icon.present,
            "the collapsed bar and the overlay are absent, and a page without an image has no header icon");
    require(parts.button_area.bounds.height == 4.0 * 31.0 &&
                parts.grip_bar.bounds.y + parts.grip_bar.bounds.height == parts.button_area.bounds.y &&
                parts.button_area.bounds.y + parts.button_area.bounds.height == parts.overflow_area.bounds.y &&
                parts.content.bounds.y == parts.header.bounds.height &&
                parts.content.bounds.y + parts.content.bounds.height == parts.grip_bar.bounds.y,
            "header, content, grip, buttons and overflow strip stack top to bottom");
    require(parts.grip.bounds.width < parts.grip_bar.bounds.width &&
                parts.grip_bar.bounds.contains(parts.grip.bounds),
            "the grip's dots sit centred in the grip bar");

    const Control::Ptr host = window.find("nav.content");
    require(host && (*host).parent() == (*fixture).pane, "the content panel is the pane's child");
    require((*(*fixture).contents[0]).parent() == host &&
                (*host).committed_arranged_bounds() == parts.content.bounds &&
                (*(*fixture).contents[0]).committed_arranged_bounds().width == parts.content.bounds.width,
            "the selected page's control fills the content panel");
    require(!(*(*fixture).contents[1]).parent(), "a page that is not selected is not in the tree");

    // Selection: a click on a large button selects its page and swaps the
    // content; an icon selects as well.
    click(window, centre(items[2].button.nav_page_face.bounds));
    window.perform_layout();
    require(pane.pages().selected_page() && (*pane.pages().selected_page()).value == "contacts",
            "clicking a large button selects its page");
    require((*(*fixture).contents[2]).parent() == host && !(*(*fixture).contents[0]).parent(),
            "the content panel holds only the selected page's control");
    require(pane.parts().header_label.present, "the header shows the selected page's title");
    click(window, centre(items[5].icon.nav_overflow_face.bounds));
    require((*pane.pages().selected_page()).value == "folders", "clicking an overflow icon selects its page");

    // Removing the selected page falls back to its neighbour and removes its control.
    require(pane.pages().remove(PageId{"folders"}) == PageEditStatus::applied, "the selected page leaves the set");
    window.perform_layout();
    require((*pane.pages().selected_page()).value == "notes" && (*(*fixture).contents[4]).parent() == host &&
                !(*(*fixture).contents[5]).parent(),
            "the fallback page's control replaces the removed page's");
}

void grip_steps() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    NavigationPane& pane = *(*fixture).pane;
    Window& window = *(*fixture).window;
    std::vector<NavigationPaneState> states;
    SubscriptionToken token = pane.state_changed().subscribe(RecordState{&states});

    require(pane.show_more_buttons() && pane.visible_button_count() == 5U && pane.button_count.read() == 5U,
            "show more buttons adds one large button");
    require(states.size() == 1U && states.back().button_count == 5U, "the step reports the new state");
    SemanticSnapshot grip_snapshot = window.semantic_snapshot();
    const SemanticNode* grip = find_node(grip_snapshot.roots, "nav.grip");
    require(grip != nullptr && (*grip).role == SemanticRole::split_pane && (*grip).numeric_value == 5.0 &&
                (*grip).minimum_value == 0.0 && (*grip).maximum_value == 6.0 && (*grip).value == "5",
            "the grip publishes the large-button count as its value, from none to every page");
    require(pane.show_more_buttons() && !pane.show_more_buttons() && pane.visible_button_count() == 6U,
            "the count stops at the number of pages");
    require(pane.show_fewer_buttons() && pane.visible_button_count() == 5U, "show fewer removes one");

    // The keyboard: the grip is the stop before the tab list.
    require(window.request_focus((*fixture).pane), "the pane takes focus");
    key(window, PhysicalKey::tab, Modifier::shift);
    key(window, PhysicalKey::down);
    require(pane.visible_button_count() == 4U, "Down on the grip shows one fewer large button");
    key(window, PhysicalKey::up);
    key(window, PhysicalKey::up);
    require(pane.visible_button_count() == 6U, "Up on the grip shows one more");
    grip_snapshot = window.semantic_snapshot();
    grip = find_node(grip_snapshot.roots, "nav.grip");
    require(grip != nullptr && has_semantic_state((*grip).states, SemanticState::focused),
            "the grip is the focused stop");

    // Semantic actions.
    require(window.perform_semantic_action("nav.grip", SemanticAction::decrement) && pane.visible_button_count() == 5U,
            "decrement steps the grip");
    require(window.perform_semantic_action("nav.grip", SemanticAction::set_value, "2") &&
                pane.visible_button_count() == 2U,
            "set_value sets the count");
    require(!window.perform_semantic_action("nav.grip", SemanticAction::set_value, "9") &&
                !window.perform_semantic_action("nav.grip", SemanticAction::set_value, "two") &&
                pane.visible_button_count() == 2U,
            "a value past the pages or not a number is refused");

    // Dragging: each button height up shows one more.
    window.perform_layout();
    const Rect bar = pane.parts().grip_bar.bounds;
    const Point start = centre(bar);
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, start}), "the grip takes the press");
    require(pane.parts().grip.states.contains(AnatomyState::captured), "the grip is captured while dragged");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, {start.x, start.y - 31.0}}));
    window.perform_layout();
    require(pane.visible_button_count() == 3U, "dragging up one button height shows one more");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, {start.x, start.y + 70.0}}));
    window.perform_layout();
    require(pane.visible_button_count() == 0U, "dragging down past the buttons shows none");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, {start.x, start.y + 70.0}}));
    require(!pane.parts().grip.states.contains(AnatomyState::captured), "the release ends the drag");
    const std::vector<NavigationPane::Item> items = pane.realized_items();
    require(items.size() == 6U && items[0].kind == NavigationPaneItemKind::icon,
            "with no large buttons every page is an icon");

    // Height limits the count; the choice is kept.
    pane.button_count = 6U;
    window.resize({640.0, 230.0});
    window.perform_layout();
    require(pane.button_count.read() == 6U && pane.visible_button_count() == 4U,
            "a short pane shows the large buttons that fit and keeps the chosen count");
    bool refused = false;
    try {
        pane.button_count = PageSetLimits::maximum_capacity + 1U;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && pane.button_count.read() == 6U, "a count past the set's capacity is refused");
}

void hidden_pages_and_menu() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    NavigationPane& pane = *(*fixture).pane;
    Window& window = *(*fixture).window;
    int options = 0;
    SubscriptionToken token = pane.options_requested().subscribe(CountOptions{&options});

    require(pane.set_page_hidden(PageId{"calendar"}, true) && pane.page_hidden(PageId{"calendar"}),
            "a page can be hidden");
    require(!pane.set_page_hidden(PageId{"calendar"}, true) && !pane.set_page_hidden(PageId{"absent"}, true),
            "hiding a hidden or unknown page changes nothing");
    window.perform_layout();
    const std::vector<NavigationPane::Item> items = pane.realized_items();
    require(items.size() == 5U && find_item(items, "calendar") == nullptr && items[1].page.value == "contacts",
            "a hidden page has neither a large button nor an icon");
    SemanticSnapshot snapshot = window.semantic_snapshot();
    require(find_node(snapshot.roots, "nav.tab.calendar") == nullptr && find_node(snapshot.roots, "nav.tab.mail"),
            "a hidden page is not a tab");

    require(pane.show_configure_menu() && pane.configure_menu_open(), "the configure menu opens");
    require(pane.parts().configure_button.states.contains(AnatomyState::open), "the configure button shows open");
    snapshot = window.semantic_snapshot();
    require(find_named(snapshot.roots, SemanticRole::menu_item, "Show more buttons") &&
                find_named(snapshot.roots, SemanticRole::menu_item, "Show fewer buttons") &&
                find_named(snapshot.roots, SemanticRole::menu_item, "Hidden pages") &&
                find_named(snapshot.roots, SemanticRole::menu_item, "Navigation pane options\xE2\x80\xA6"),
            "the menu offers more and fewer buttons, the hidden pages and the options");
    const SemanticNode* row = find_named(snapshot.roots, SemanticRole::menu_item, "Calendar");
    require(row != nullptr, "the hidden page is listed in the menu");
    require(find_named(snapshot.roots, SemanticRole::menu_item, "Mail") == nullptr,
            "a page with a button is not listed");
    require(window.perform_semantic_action((*row).stable_id, SemanticAction::press), "the hidden page's row is chosen");
    window.perform_layout();
    const std::vector<NavigationPane::Item> shown_items = pane.realized_items();
    require(!pane.configure_menu_open() && !pane.page_hidden(PageId{"calendar"}) &&
                (*pane.pages().selected_page()).value == "calendar" && find_item(shown_items, "calendar"),
            "choosing a hidden page shows it again and selects it");

    require(pane.show_configure_menu(), "the configure menu opens again");
    snapshot = window.semantic_snapshot();
    require(find_named(snapshot.roots, SemanticRole::menu_item, "Hidden pages") == nullptr,
            "with no hidden pages the section is gone");
    const SemanticNode* more = find_named(snapshot.roots, SemanticRole::menu_item, "Show more buttons");
    require(more != nullptr && window.perform_semantic_action((*more).stable_id, SemanticAction::press) &&
                pane.visible_button_count() == 5U,
            "show more buttons from the menu adds a large button");
    require(pane.show_configure_menu(), "and again");
    snapshot = window.semantic_snapshot();
    const SemanticNode* option = find_named(snapshot.roots, SemanticRole::menu_item, "Navigation pane options\xE2\x80\xA6");
    require(option != nullptr && window.perform_semantic_action((*option).stable_id, SemanticAction::press) &&
                options == 1,
            "the options row raises options_requested and shows no dialog");

    // Pages that fit nowhere are listed too.
    window.resize({640.0, 420.0});
    pane.expanded_width = 80.0;
    pane.button_count = 0U;
    window.perform_layout();
    const std::size_t realized = pane.realized_items().size();
    require(realized < 6U, "a narrow strip holds fewer icons than pages");
    require(pane.show_configure_menu(), "the menu opens on a narrow pane");
    snapshot = window.semantic_snapshot();
    require(find_named(snapshot.roots, SemanticRole::menu_item, "Folders") != nullptr,
            "a page with no room for an icon is listed in the menu");
    pane.close_configure_menu();
}

void collapse_and_overlay() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    NavigationPane& pane = *(*fixture).pane;
    Window& window = *(*fixture).window;
    std::vector<NavigationPaneState> states;
    SubscriptionToken state_token = pane.state_changed().subscribe(RecordState{&states});

    require(!pane.open_overlay(), "an expanded pane has no overlay");
    click(window, centre(pane.parts().collapse_button.bounds));
    window.perform_layout();
    require(pane.collapsed.read() && states.size() == 1U && states.back().collapsed, "the collapse button collapses the pane");
    require(pane.committed_arranged_bounds().width == 32.0, "a collapsed pane is a narrow bar");
    NavigationPane::Parts parts = pane.parts();
    require(parts.collapsed_bar.present && parts.collapsed_label.present && parts.collapse_button.present &&
                !parts.header.present && !parts.content.present && !parts.grip.present &&
                !parts.configure_button.present,
            "the bar shows the expand button and the turned title, and no header, content, grip or strip");
    std::vector<NavigationPane::Item> items = pane.realized_items();
    require(items.size() == 4U && items[0].kind == NavigationPaneItemKind::icon && items[0].page.value == "mail",
            "the large buttons' pages are icons on the bar");
    require(!pane.show_configure_menu(), "a collapsed pane has no configure menu");
    const Control::Ptr host = window.find("nav.content");
    require(host && !(*host).effectively_visible(), "the content is hidden while collapsed");
    SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* expand = find_node(snapshot.roots, "nav.collapse");
    const SemanticNode* caption = find_node(snapshot.roots, "nav.caption");
    require(expand != nullptr && (*expand).name == "Expand navigation pane" && caption != nullptr &&
                (*caption).name == "Mail" && (*caption).has_popup,
            "the bar's button expands and its title opens an overlay");

    // The overlay opens from the title over the window.
    std::vector<std::string> order;
    SubscriptionToken overlay_token = pane.overlay_open_changed().subscribe(RecordOverlay{&order});
    SubscriptionToken press_token = (*(*fixture).outside).pointer_observed().subscribe(RecordPress{&order});
    click(window, centre(parts.collapsed_label.bounds));
    window.perform_layout();
    require(pane.overlay_open() && order.size() == 1U && order[0] == "opened", "clicking the title opens the overlay");
    const Control::Ptr overlay = window.find("nav.overlay");
    parts = pane.parts();
    require(overlay && (*host).parent() == overlay && (*host).effectively_visible() &&
                (*(*fixture).contents[0]).parent() == host,
            "the content panel moves into the overlay with the selected page's control");
    require(parts.overlay_face.present && parts.overlay_shadow.present && parts.overlay_label.present &&
                parts.overlay_content.present && parts.overlay_face.bounds.x == 32.0 &&
                parts.overlay_face.bounds.width == 220.0 && parts.collapsed_label.states.contains(AnatomyState::open),
            "the overlay lies beside the bar at the expanded width, and the title shows open");
    require((*overlay).committed_arranged_bounds() == Rect{32.0, 0.0, 220.0, 420.0},
            "the overlay's root covers the overlay's face in window coordinates");

    // A press outside closes it first, then reaches the control under it.
    click(window, centre((*(*fixture).outside).committed_arranged_bounds()));
    window.perform_layout();
    require(!pane.overlay_open() && order.size() == 3U && order[1] == "closed" && order[2] == "outside pressed",
            "an outside press closes the overlay and passes through to the control under it");
    require((*host).parent() == (*fixture).pane && !window.find("nav.overlay") && !pane.parts().overlay_face.present,
            "the content panel returns to the pane and the overlay leaves the window");

    // Escape closes it, and focus comes back to the pane.
    require(window.request_focus((*fixture).pane), "the pane takes focus");
    require(window.perform_semantic_action("nav.caption", SemanticAction::expand) && pane.overlay_open(),
            "the title's expand action opens the overlay");
    key(window, PhysicalKey::escape);
    require(!pane.overlay_open() && window.focused_control() == (*fixture).pane, "Escape closes the overlay");

    // A focusable control in the content takes focus inside the overlay, and
    // Escape it leaves unhandled closes the overlay and restores focus.
    std::shared_ptr<Button> editor = make_control<Button>(StableId("content.editor"), "Compose");
    std::unique_ptr<PageContent> content = std::make_unique<PageControlContent>(editor);
    const PageRecord record{.id = PageId{"journal"}, .title = "Journal"};
    require(pane.pages().insert(record, PagePlacement::end_of_group()) == PageEditStatus::applied &&
                pane.pages().set_content(record.id, content) == PageEditStatus::applied,
            "a page with a button joins while collapsed");
    require(pane.pages().select(record.id, PageSelectionCause::programmatic) == PageEditStatus::applied,
            "a page is selected while collapsed");
    require(pane.open_overlay() && window.focused_control() == editor, "the overlay's first Tab stop takes focus");
    key(window, PhysicalKey::escape);
    require(!pane.overlay_open() && window.focused_control() == (*fixture).pane,
            "Escape inside the overlay closes it and focus returns");

    // Expanding closes the overlay.
    require(pane.open_overlay(), "the overlay opens again");
    require(window.perform_semantic_action("nav.collapse", SemanticAction::press) && !pane.collapsed.read() &&
                !pane.overlay_open(),
            "expanding closes the overlay");
    window.perform_layout();
    require((*host).effectively_visible() && (*editor).parent() == host, "expanded, the content is back in the pane");
}

void keyboard_and_semantics() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    NavigationPane& pane = *(*fixture).pane;
    Window& window = *(*fixture).window;
    require(window.request_focus((*fixture).pane), "the pane takes focus");
    SemanticSnapshot snapshot = window.semantic_snapshot();
    const SemanticNode* list = find_node(snapshot.roots, "nav.tabs");
    require(list != nullptr && (*list).role == SemanticRole::tab_group &&
                (*list).orientation == SemanticOrientation::vertical && (*list).children.size() == 6U,
            "the buttons and icons are one vertical tab list");
    const SemanticNode& first = (*list).children[0];
    require(first.role == SemanticRole::tab && first.name == "Mail" && first.position_in_set == 1U &&
                first.set_size == 6U && has_semantic_state(first.states, SemanticState::selected) &&
                has_semantic_state(first.states, SemanticState::focused) && first.controlled_panel == "nav.content",
            "the selected tab is focused first and controls the content panel");
    bool presses = false;
    for (const SemanticAction action : first.actions) {
        if (action == SemanticAction::press) presses = true;
    }
    require(!presses, "tabs publish select, not press");
    require((*list).children[1].controlled_panel.empty(), "a tab that is not selected controls nothing");
    const SemanticNode* panel = find_node(snapshot.roots, "nav.content");
    require(panel != nullptr && (*panel).role == SemanticRole::tab_panel && (*panel).name == "Mail" &&
                find_node((*panel).children, "content.mail") != nullptr,
            "the content panel is a tab panel named by its page, holding the page's control");
    const SemanticNode* pane_node = find_node(snapshot.roots, "nav");
    require(pane_node != nullptr && (*pane_node).role == SemanticRole::group && (*pane_node).name == "Navigation pane",
            "the pane is a named group");
    const SemanticNode* configure = find_node(snapshot.roots, "nav.configure");
    require(configure != nullptr && (*configure).role == SemanticRole::menu_button && (*configure).has_popup,
            "the configure button is a menu button");

    // Arrows move across the buttons and icons without selecting; Enter
    // selects.
    key(window, PhysicalKey::down);
    key(window, PhysicalKey::down);
    require((*pane.pages().selected_page()).value == "mail", "arrows move focus without selecting");
    key(window, PhysicalKey::enter);
    require((*pane.pages().selected_page()).value == "contacts", "Enter selects the focused tab");
    key(window, PhysicalKey::end);
    key(window, PhysicalKey::space);
    require((*pane.pages().selected_page()).value == "folders", "End reaches the last icon and Space selects it");
    key(window, PhysicalKey::down);
    key(window, PhysicalKey::enter);
    require((*pane.pages().selected_page()).value == "mail", "the arrows wrap");
    const std::vector<NavigationPane::Item> items = pane.realized_items();
    require(items[0].states.selected, "the selected tab publishes selected");

    // Tab moves through the stops: the configure button follows the tabs.
    key(window, PhysicalKey::tab);
    snapshot = window.semantic_snapshot();
    configure = find_node(snapshot.roots, "nav.configure");
    require(configure != nullptr && has_semantic_state((*configure).states, SemanticState::focused),
            "Tab moves from the tab list to the configure button");
    key(window, PhysicalKey::enter);
    require(pane.configure_menu_open(), "Enter on the configure button opens its menu");
    pane.close_configure_menu();

    // Semantic selection and the collapse button.
    require(window.perform_semantic_action("nav.tab.tasks", SemanticAction::select) &&
                (*pane.pages().selected_page()).value == "tasks",
            "a tab's select action selects its page");
    require(!window.perform_semantic_action("nav.tab.absent", SemanticAction::select), "an unknown tab is refused");
    require(window.perform_semantic_action("nav.collapse", SemanticAction::collapse) && pane.collapsed.read(),
            "the collapse action collapses the pane");

    // F6 registration.
    const std::vector<Control::Ptr> regions = window.focus_regions();
    require(regions.size() == 1U && regions[0] == (*fixture).pane, "the pane registers itself as an F6 region");
}

void state_round_trip() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    NavigationPane& pane = *(*fixture).pane;
    Window& window = *(*fixture).window;
    require(pane.pages().move(PageId{"notes"}, PagePosition{0U}) == PageEditStatus::applied, "a page moves to the front");
    require(pane.set_page_hidden(PageId{"tasks"}, true), "a page is hidden");
    pane.button_count = 2U;
    pane.collapsed = true;
    const NavigationPaneState saved = pane.state();
    require(saved.collapsed && saved.button_count == 2U && saved.hidden_pages.size() == 1U &&
                saved.hidden_pages[0].value == "tasks" && saved.page_order.size() == 6U &&
                saved.page_order[0].value == "notes",
            "the state holds the collapse, the count, the hidden pages and the order");

    // A second pane with the same pages in the default order takes the state.
    std::unique_ptr<Fixture> other = make_fixture();
    NavigationPane& restored = *(*other).pane;
    std::vector<NavigationPaneState> states;
    SubscriptionToken token = restored.state_changed().subscribe(RecordState{&states});
    NavigationPaneState with_unknown = saved;
    with_unknown.page_order.insert(with_unknown.page_order.begin() + 1, PageId{"plugin"});
    with_unknown.hidden_pages.push_back(PageId{"removed"});
    const NavigationPaneRestore result = restored.apply_state(with_unknown);
    require(result.unknown_pages == 2U, "identities the set does not hold are counted and skipped");
    require(restored.state() == saved, "the state round-trips");
    require(states.size() == 1U && states.back() == saved, "restoring raises one state_changed");
    (*(*other).window).perform_layout();
    require(restored.committed_arranged_bounds().width == 32.0, "the restored pane is collapsed");

    NavigationPaneState repeated = saved;
    repeated.page_order.push_back(PageId{"notes"});
    bool refused = false;
    try {
        static_cast<void>(restored.apply_state(repeated));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && restored.state() == saved, "a repeated identity is refused and changes nothing");

    // The framework never saves: the state is only what the program reads.
    pane.collapsed = false;
    window.perform_layout();
    require(pane.state() != saved && !pane.state().collapsed, "the state follows the pane");
}

void published_states() {
    std::unique_ptr<Fixture> fixture = make_fixture();
    NavigationPane& pane = *(*fixture).pane;
    Window& window = *(*fixture).window;
    const NavigationPane::Parts parts = pane.parts();
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, centre(parts.collapse_button.bounds)}));
    require(pane.parts().collapse_button.states.contains(AnatomyState::hot) &&
                pane.parts().collapse_glyph.states.contains(AnatomyState::hot),
            "the collapse button and its glyph publish hot");
    std::vector<NavigationPane::Item> items = pane.realized_items();
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, centre(items[1].button.nav_page_face.bounds)}));
    items = pane.realized_items();
    require(items[1].states.hot && !items[0].states.hot && !pane.parts().collapse_button.states.contains(AnatomyState::hot),
            "a large button publishes hot");
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, centre(items[1].button.nav_page_face.bounds)}));
    items = pane.realized_items();
    require(items[1].states.pressed, "a pressed large button publishes pressed");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, centre(items[1].button.nav_page_face.bounds)}));
    items = pane.realized_items();
    require(!items[1].states.pressed && items[1].states.selected, "the release selects and the press ends");
    require(pane.realized_item_count() == 6U && pane.realized_item_system(0U) == ItemSystemIndex{0U} &&
                pane.realized_item_system(5U) == ItemSystemIndex{1U},
            "large buttons are nav_page items and icons nav_overflow items");

    // Keyboard focus shows on the focused stop.
    require(window.perform_semantic_action("nav.tab.tasks", SemanticAction::focus), "a tab takes semantic focus");
    std::vector<NavigationPane::Item> focused = pane.realized_items();
    const NavigationPane::Item* tasks = find_item(focused, "tasks");
    require(window.focused_control() == (*fixture).pane && tasks != nullptr &&
                (*tasks).states.focus_visible == window.focus_cue_visible() && !focused[0].states.focus_visible,
            "the focused tab publishes focus_visible with the cue");
    require(pane.parts().focus.bounds == (*tasks).button.nav_page_face.bounds,
            "the focus part follows the focused stop");

    // The anatomy declares states only on the parts that take them.
    const ControlAnatomy& anatomy = pane.anatomy();
    const std::optional<PartIndex> grip = find_part(anatomy, "grip");
    require(grip && declares_part_state(anatomy.parts[(*grip).value], AnatomyState::captured),
            "the grip declares captured");
    const std::optional<PartIndex> configure = find_part(anatomy, "configure_button");
    require(configure && declares_part_state(anatomy.parts[(*configure).value], AnatomyState::open) &&
                anatomy.parts[(*configure).value].composed_from == "button",
            "the configure button is composed from a button and declares open");
}

void refusals() {
    std::shared_ptr<NavigationPane> pane = make_control<NavigationPane>(StableId("nav"));
    require(!(*pane).open_overlay() && !(*pane).show_configure_menu(), "a detached pane opens nothing");
    bool threw = false;
    try {
        (*pane).expanded_width = 10.0;
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw && (*pane).expanded_width.read() == 220.0, "an expanded width below 80 is refused");
    threw = false;
    try {
        static_cast<void>((*pane).show_configure_menu(static_cast<MenuOpenMode>(9)));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw, "an unknown menu mode is refused");
    require((*pane).visible_button_count() == 0U, "an unlaid pane shows no large buttons");
}

} // namespace

int main() {
    layout_and_content();
    grip_steps();
    hidden_pages_and_menu();
    collapse_and_overlay();
    keyboard_and_semantics();
    state_round_trip();
    published_states();
    refusals();
    std::cout << "NavigationPane: layout, content, grip steps, hidden pages and menu, collapse and overlay order, "
                 "keyboard, semantics, state round trip and published parts passed\n";
    return 0;
}
