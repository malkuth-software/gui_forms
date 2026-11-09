// RibbonGallery and GalleryCommand (stage W, ribbon phase 3): the value
// command and its preview protocol, in-ribbon layout and the column range,
// the virtualization bound, the event order of hover and keyboard preview,
// commit and cancel, the drop-down form with categories, the more button's
// overlay and focus, and semantics.

#include "gui_forms/gui_forms.hpp"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
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

// One ordered log of everything the command and gallery raise.
struct Log final {
    std::vector<std::string> entries;
    void on_value(const std::optional<std::string>& value) { entries.push_back("value:" + value.value_or("none")); }
    void on_preview(const GalleryPreview& preview) { entries.push_back("preview:" + preview.item_id); }
    void on_cancel(const GalleryPreview& preview) { entries.push_back("cancel:" + preview.item_id); }
    void on_invoked(const CommandInvocation&) { entries.push_back("invoked"); }
    void on_overlay(bool open) { entries.push_back(open ? "overlay:open" : "overlay:closed"); }
    [[nodiscard]] std::string joined() const {
        std::string text;
        for (const std::string& entry : entries) text += entry + " ";
        return text;
    }
};

struct Fixture final {
    std::shared_ptr<Command> command = std::make_shared<Command>("styles", "Styles");
    std::shared_ptr<GalleryCommand> gallery_command = std::make_shared<GalleryCommand>(command);
    Log log;
    std::vector<SubscriptionToken> tokens;

    Fixture() {
        tokens.push_back((*gallery_command).value_changed().subscribe(
            Delegate<const std::optional<std::string>&>::bind<Log, &Log::on_value>(log)));
        tokens.push_back((*gallery_command).preview_requested().subscribe(
            Delegate<const GalleryPreview&>::bind<Log, &Log::on_preview>(log)));
        tokens.push_back((*gallery_command).preview_cancelled().subscribe(
            Delegate<const GalleryPreview&>::bind<Log, &Log::on_cancel>(log)));
        tokens.push_back((*command).invoked().subscribe(
            Delegate<const CommandInvocation&>::bind<Log, &Log::on_invoked>(log)));
    }
};

std::vector<RibbonGalleryItem> numbered_items(std::size_t count, std::string_view category = {}) {
    std::vector<RibbonGalleryItem> items;
    items.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        RibbonGalleryItem item;
        item.stable_id = std::string(category.empty() ? "item" : category) + "." + std::to_string(index);
        item.label = "Item " + std::to_string(index);
        item.category = std::string(category);
        items.push_back(std::move(item));
    }
    return items;
}

Point centre(const Control& control, Rect local) {
    const Rect absolute = control.absolute_bounds();
    const Point point{absolute.x + local.x + local.width * 0.5, absolute.y + local.y + local.height * 0.5};
    return point;
}

// A borrow into `items`, which must outlive it: temporaries are refused
// (house style §1).
const RibbonGallery::Item* find_item(const std::vector<RibbonGallery::Item>& items, std::string_view id) {
    for (const RibbonGallery::Item& item : items) {
        if (item.stable_id == id) return &item;
    }
    return nullptr;
}
const RibbonGallery::Item* find_item(std::vector<RibbonGallery::Item>&& items, std::string_view id) = delete;

// The item `id` names, which must be there: a missing one ends the test
// rather than dereferencing null. A borrow into `items`, as find_item is.
const RibbonGallery::Item& item_named(const std::vector<RibbonGallery::Item>& items, std::string_view id) {
    const RibbonGallery::Item* const found = find_item(items, id);
    require(found != nullptr, "the item the test names is realized");
    return *found;
}
const RibbonGallery::Item& item_named(std::vector<RibbonGallery::Item>&& items, std::string_view id) = delete;

void move_to(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::none, point}));
}

void click_at(Window& window, Point point) {
    static_cast<void>(window.dispatch_pointer({PointerAction::down, PointerButton::primary, point}));
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary, point}));
}

// Lets every pending dwell end.
void elapse_dwell(Window& window) {
    const FrameTime later = FrameClock::now() + RibbonGallery::preview_dwell + std::chrono::milliseconds(50);
    static_cast<void>(window.poll_frame_schedule(later));
}

bool key(Window& window, std::uint32_t physical) {
    const bool handled = window.dispatch_key({KeyAction::down, physical});
    return handled;
}

std::shared_ptr<RibbonGallery> in_ribbon_gallery(const char* id, std::size_t count) {
    std::shared_ptr<RibbonGallery> gallery = make_control<RibbonGallery>(StableId(std::string(id)));
    (*gallery).item_size = Size{40.0, 40.0};
    (*gallery).visible_rows = 2U;
    (*gallery).set_column_range(3U, 6U);
    (*gallery).columns = 6U;
    (*gallery).set_items({}, numbered_items(count));
    return gallery;
}

void value_command() {
    Fixture fixture;
    GalleryCommand& command = *fixture.gallery_command;
    require(command.stable_id() == "styles" && command.state().text == "Styles",
            "a value command shares its Command's identity and state");
    command.set_value(std::string("a"));
    command.set_value(std::string("a"));
    require(fixture.log.joined() == "value:a ", "set_value raises value_changed once and does not invoke");
    require(command.preview("b", "test") && !command.preview("b", "test") && command.preview("c", "test"),
            "a preview of the outstanding item is refused; another replaces it");
    require(command.commit("c", "test") && !command.previewing(), "a commit ends the preview");
    require(fixture.log.joined() == "value:a preview:b preview:c value:c invoked ",
            "previews replace each other without cancellation, and a commit sets the value before invoking");
    command.cancel_preview("test");
    require(command.preview("d", "test"), "a new preview starts");
    command.cancel_preview("test");
    require(fixture.log.entries.back() == "cancel:d", "cancel_preview withdraws the outstanding preview");
    (*fixture.command).set_enabled(false);
    require(!command.commit("e", "test") && !command.preview("e", "test") && command.value() == std::string("c"),
            "a disabled command refuses commits and previews");
    bool refused = false;
    try {
        command.set_value(std::string());
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused, "an empty value is refused");
}

void layout_and_columns() {
    std::shared_ptr<RibbonGallery> gallery = in_ribbon_gallery("gallery.layout", 30U);
    const ControlAnatomy& anatomy = (*gallery).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none &&
                anatomy.control_id == "ribbon_gallery" && anatomy.item_systems.size() == 2U,
            "RibbonGallery publishes a valid anatomy with gallery_item and gallery_category");
    const Size wanted = (*gallery).measure({1000.0, 1000.0});
    require(wanted.width == 6.0 * 40.0 + 16.0 + 4.0 && wanted.height == 2.0 * 40.0 + 4.0,
            "an in-ribbon gallery asks for its columns and rows plus the scroll column");
    Window window(gallery, wanted);
    window.perform_layout();
    require((*gallery).realized_items().size() == 12U && (*gallery).line_count() == 5U,
            "two rows of six are realized out of five rows");
    const RibbonGallery::Parts parts = (*gallery).parts();
    require(parts.scroll_up.present && parts.scroll_down.present && parts.more_button.present &&
                parts.more_glyph.present && parts.scroll_up.states.contains(AnatomyState::disabled) &&
                !parts.scroll_down.states.contains(AnatomyState::disabled),
            "scroll up is disabled at the top; scroll down and more are present");
    (*gallery).columns = 5U;
    window.perform_layout();
    require((*gallery).realized_items().size() == 10U && (*gallery).line_count() == 6U,
            "giving up one column reflows the rows");
    bool refused = false;
    try {
        (*gallery).columns = 2U;
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused, "a column count below the authored minimum is refused");
    (*gallery).set_column_range(4U, 4U);
    require((*gallery).columns.read() == 4U, "a narrower range clamps the column count");
    (*gallery).scroll_to_line(1U);
    window.perform_layout();
    const std::vector<RibbonGallery::Item> items = (*gallery).realized_items();
    require(!items.empty() && items.front().stable_id == "item.4" && (*gallery).can_scroll_up(),
            "scrolling by a row shows the next row first");
    const RibbonGallery::Item* first = find_item(items, "item.4");
    require((*first).parts.item_face.present && (*first).parts.item_icon.present && !(*first).parts.item_label.present,
            "an item without labels shows a face and an image area");
    (*gallery).show_labels = true;
    window.perform_layout();
    const std::vector<RibbonGallery::Item> labelled = (*gallery).realized_items();
    require(item_named(labelled, labelled.front().stable_id).parts.item_label.present,
            "labels show under the images when asked");
}

void virtualization() {
    std::shared_ptr<RibbonGallery> gallery = in_ribbon_gallery("gallery.large", 10000U);
    Window window(gallery, (*gallery).measure({1000.0, 1000.0}));
    window.perform_layout();
    require((*gallery).realized_item_count() == 12U && (*gallery).line_count() == 1667U,
            "a 10,000-item gallery realizes only the rows in view");
    (*gallery).scroll_to_line(5000U);
    window.perform_layout();
    require((*gallery).first_visible_line() == 1665U && (*gallery).realized_items().back().stable_id == "item.9999",
            "scrolling to the end keeps the last page full");

    std::shared_ptr<RibbonGallery> full = make_control<RibbonGallery>(StableId("gallery.full"), RibbonGalleryForm::drop_down);
    std::vector<RibbonGalleryItem> items = numbered_items(7U, "warm");
    std::vector<RibbonGalleryItem> cool = numbered_items(500U, "cool");
    items.insert(items.end(), cool.begin(), cool.end());
    (*full).item_size = Size{40.0, 40.0};
    (*full).set_drop_down_size(4U, 3U);
    (*full).set_items({{"warm", "Warm"}, {"cool", "Cool"}}, std::move(items));
    Window full_window(full, (*full).measure({1000.0, 1000.0}));
    full_window.perform_layout();
    const std::vector<RibbonGallery::Category> captions = (*full).realized_categories();
    require(!captions.empty() && captions.front().stable_id == "warm" && captions.front().parts.category_caption.present &&
                captions.front().parts.category_divider.present,
            "the drop-down form shows a caption before each category");
    require((*full).realized_items().size() <= 12U && (*full).line_count() == 1U + 2U + 1U + 125U,
            "the drop-down form realizes only rows in view; categories start new rows");
}

void hover_preview_order() {
    Fixture fixture;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("hover.root"));
    std::shared_ptr<RibbonGallery> gallery = in_ribbon_gallery("hover.gallery", 30U);
    (*gallery).command = fixture.gallery_command;
    (*root).add_child(gallery);
    (*gallery).set_requested_bounds({10.0, 10.0, 260.0, 84.0});
    Window window(root, {400.0, 200.0});
    window.perform_layout();
    std::vector<RibbonGallery::Item> items = (*gallery).realized_items();
    const Point a = centre(*gallery, item_named(items, "item.0").parts.item_face.bounds);
    const Point b = centre(*gallery, item_named(items, "item.1").parts.item_face.bounds);
    const Point c = centre(*gallery, item_named(items, "item.2").parts.item_face.bounds);
    move_to(window, a);
    require(fixture.log.entries.empty(), "no preview before the dwell");
    const std::vector<RibbonGallery::Item> hovered = (*gallery).realized_items();
    require(item_named(hovered, "item.0").states.hot, "the item under the pointer is hot");
    elapse_dwell(window);
    move_to(window, b);
    elapse_dwell(window);
    move_to(window, {380.0, 180.0});
    require(fixture.log.joined() == "preview:item.0 preview:item.1 cancel:item.1 ",
            "hover previews after the dwell, the next item replaces it, leaving cancels");
    fixture.log.entries.clear();
    move_to(window, c);
    elapse_dwell(window);
    click_at(window, c);
    require(fixture.log.joined() == "preview:item.2 value:item.2 invoked ",
            "a click commits without a cancellation, value before invocation");
    const std::vector<RibbonGallery::Item> chosen = (*gallery).realized_items();
    require((*gallery).selected_id.read() == std::string("item.2") && item_named(chosen, "item.2").states.selected,
            "the selection follows the command's value");
    fixture.log.entries.clear();
    move_to(window, a);
    move_to(window, b);
    move_to(window, {380.0, 180.0});
    elapse_dwell(window);
    require(fixture.log.entries.empty(), "a pointer that does not rest previews nothing");
}

void keyboard() {
    Fixture fixture;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("keys.root"));
    std::shared_ptr<RibbonGallery> gallery = in_ribbon_gallery("keys.gallery", 30U);
    (*gallery).command = fixture.gallery_command;
    (*fixture.gallery_command).set_value(std::string("item.7"));
    fixture.log.entries.clear();
    std::shared_ptr<Button> other = make_control<Button>(StableId("keys.other"), "Other");
    (*root).add_child(gallery);
    (*root).add_child(other);
    (*gallery).set_requested_bounds({0.0, 0.0, 260.0, 84.0});
    (*other).set_requested_bounds({0.0, 100.0, 80.0, 30.0});
    Window window(root, {400.0, 200.0});
    window.perform_layout();
    require(window.request_focus(gallery) && (*gallery).focused_id() == std::string("item.7"),
            "focus starts on the selected item");
    require(key(window, PhysicalKey::right) && (*gallery).focused_id() == std::string("item.8"), "Right moves one item");
    elapse_dwell(window);
    require(fixture.log.joined() == "preview:item.8 ", "keyboard focus resting on an item previews it");
    require(key(window, PhysicalKey::down) && (*gallery).focused_id() == std::string("item.14"),
            "Down moves one row in the same column");
    elapse_dwell(window);
    require(key(window, PhysicalKey::escape) && fixture.log.joined() == "preview:item.8 preview:item.14 cancel:item.14 ",
            "Escape withdraws the preview");
    require(key(window, PhysicalKey::page_down) && (*gallery).focused_id() == std::string("item.26"),
            "Page Down moves a page of rows");
    require((*gallery).first_visible_line() == 3U, "the focused row is scrolled into view");
    require(key(window, PhysicalKey::home) && (*gallery).focused_id() == std::string("item.0"), "Home goes first");
    require(key(window, PhysicalKey::end) && (*gallery).focused_id() == std::string("item.29"), "End goes last");
    fixture.log.entries.clear();
    require(key(window, PhysicalKey::enter) && fixture.log.joined() == "value:item.29 invoked ",
            "Enter commits the focused item");
    fixture.log.entries.clear();
    require(key(window, PhysicalKey::left), "Left moves back");
    elapse_dwell(window);
    require(window.request_focus(other), "focus moves to another control");
    require(fixture.log.joined() == "preview:item.28 cancel:item.28 ", "losing focus withdraws the preview");
}

void overlays() {
    Fixture fixture;
    Log overlay_log;
    std::shared_ptr<Panel> root = make_control<Panel>(StableId("overlay.root"));
    std::shared_ptr<RibbonGallery> gallery = in_ribbon_gallery("overlay.gallery", 30U);
    (*gallery).command = fixture.gallery_command;
    (*gallery).set_drop_down_size(6U, 4U);
    (*root).add_child(gallery);
    (*gallery).set_requested_bounds({10.0, 10.0, 260.0, 84.0});
    Window window(root, {600.0, 500.0});
    window.perform_layout();
    const Point more = centre(*gallery, (*gallery).parts().more_button.bounds);
    click_at(window, more);
    std::shared_ptr<RibbonGallery> full = (*gallery).more_gallery();
    require(full && (*full).overlay_open() && (*full).form() == RibbonGalleryForm::drop_down,
            "the more button opens the full gallery as an overlay");
    SubscriptionToken overlay_token = (*full).overlay_open_changed().subscribe(
        Delegate<bool>::bind<Log, &Log::on_overlay>(fixture.log));
    window.perform_layout();
    require((*gallery).parts().more_button.states.contains(AnatomyState::open), "the more button publishes open");
    require(window.focused_control() == std::static_pointer_cast<Control>(full) && (*full).focused_id(),
            "keyboard focus moves into the full gallery");
    require((*full).absolute_bounds().y >= (*gallery).absolute_bounds().y + (*gallery).absolute_bounds().height,
            "the full gallery opens over the ribbon, below the in-ribbon gallery");
    const std::vector<RibbonGallery::Item> items = (*full).realized_items();
    require(items.size() == 24U, "the full gallery shows its rows");
    const Point target = centre(*full, item_named(items, "item.9").parts.item_face.bounds);
    move_to(window, target);
    elapse_dwell(window);
    require(key(window, PhysicalKey::escape), "Escape is taken");
    require(!(*full).overlay_open() && fixture.log.joined() == "preview:item.9 cancel:item.9 overlay:closed ",
            "Escape cancels the preview, then closes the overlay");
    fixture.log.entries.clear();
    click_at(window, more);
    window.perform_layout();
    const std::vector<RibbonGallery::Item> reopened = (*full).realized_items();
    const Point choice = centre(*full, item_named(reopened, "item.11").parts.item_face.bounds);
    click_at(window, choice);
    require(fixture.log.joined() == "overlay:open value:item.11 invoked overlay:closed ",
            "a click in the overlay commits, then closes it");
    require((*gallery).selected_id.read() == std::string("item.11"), "the in-ribbon gallery follows the committed value");

    // A drop-down gallery as a button's overlay.
    std::shared_ptr<Button> anchor = make_control<Button>(StableId("overlay.anchor"), "Styles");
    (*root).add_child(anchor);
    (*anchor).set_requested_bounds({300.0, 10.0, 100.0, 30.0});
    std::shared_ptr<RibbonGallery> drop_down = make_control<RibbonGallery>(StableId("overlay.drop-down"),
                                                                          RibbonGalleryForm::drop_down);
    (*drop_down).set_items({}, numbered_items(8U));
    window.perform_layout();
    require(!(*gallery).open_overlay(anchor), "an in-ribbon gallery is not itself an overlay");
    require((*drop_down).open_overlay(anchor) && (*drop_down).overlay_open(), "a drop-down gallery opens under a button");
    (*drop_down).close_overlay();
    require(!(*drop_down).overlay_open(), "and closes");

    require((*gallery).show_more() && (*full).overlay_open(), "the full gallery opens again");
    static_cast<void>((*root).remove_child((*gallery).runtime_id()));
    (*gallery).dispose();
    require(!(*full).is_alive() && !(*full).overlay_open(), "disposing a gallery disposes and closes its full gallery");
}

void semantics() {
    std::shared_ptr<RibbonGallery> gallery = in_ribbon_gallery("semantics.gallery", 9U);
    (*gallery).accessible_name = "Styles";
    Window window(gallery, (*gallery).measure({1000.0, 1000.0}));
    window.perform_layout();
    const SemanticDescriptor descriptor = (*gallery).semantic_descriptor();
    require(descriptor.role == SemanticRole::list && descriptor.name == "Styles", "a gallery is a named list");
    const std::vector<SemanticNode> nodes = (*gallery).semantic_virtual_children();
    require(nodes.size() == 12U && nodes[0].role == SemanticRole::list_item && nodes[0].position_in_set == 1U &&
                nodes[0].set_size == 9U && nodes[8].position_in_set == 9U,
            "every item is a list item with its position in the set");
    const SemanticNode& more = nodes.back();
    require(more.role == SemanticRole::menu_button && more.has_popup, "the more button is a menu button with an overlay");
    require((*gallery).on_semantic_child_action("item.4", SemanticAction::press, {}) &&
                (*gallery).selected_id.read() == std::string("item.4"),
            "pressing an item node selects it");

    std::shared_ptr<RibbonGallery> full = make_control<RibbonGallery>(StableId("semantics.full"), RibbonGalleryForm::drop_down);
    std::vector<RibbonGalleryItem> items = numbered_items(3U, "warm");
    std::vector<RibbonGalleryItem> cool = numbered_items(2U, "cool");
    items.insert(items.end(), cool.begin(), cool.end());
    (*full).set_items({{"warm", "Warm"}, {"cool", "Cool"}}, std::move(items));
    const std::vector<SemanticNode> grouped = (*full).semantic_virtual_children();
    require(grouped.size() >= 2U && grouped[0].role == SemanticRole::group && grouped[0].name == "Warm" &&
                grouped[0].children.size() == 3U && grouped[0].children[2].position_in_set == 3U &&
                grouped[0].children[2].set_size == 3U,
            "the drop-down form groups items by category, positioned within it");
}

} // namespace

int main() {
    value_command();
    layout_and_columns();
    virtualization();
    hover_preview_order();
    keyboard();
    overlays();
    semantics();
    std::cout << "RibbonGallery: value command, layout, columns, virtualization, preview order, keyboard, "
                 "overlays and semantics passed\n";
    return 0;
}
