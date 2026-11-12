// ListView (stage W): the details, list and icon views and their published
// parts, selection by pointer and keyboard, check boxes, column clicks,
// sorting and resizing, virtual mode, type-ahead, scrolling, semantics and
// refusals.

#include "gui_forms/gui_forms.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
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

class RecordingPainter final : public Painter {
public:
    void save() override {}
    void restore() override {}
    void translate(Point) override {}
    void clip_rect(Rect) override {}
    void fill_rect(Rect, Color) override { ++fills; }
    void fill_rounded_rect(Rect, double, Color) override { ++fills; }
    void fill_linear_gradient(Rect, Point, Point, std::span<const GradientStop>) override { ++fills; }
    void stroke_rect(Rect, Color, double) override {}
    void draw_line(Point, Point, Color, double) override {}
    void draw_text_utf8(Point, std::string_view text, FontSpec, Color) override { texts.emplace_back(text); }
    void draw_image(ImageId, Rect, double) override { ++images; }

    std::vector<std::string> texts;
    std::uint64_t images{};
    std::uint64_t fills{};
};

bool painted(const RecordingPainter& painter, std::string_view text) {
    for (const std::string& seen : painter.texts) {
        if (seen == text) return true;
    }
    return false;
}

struct CountSelection final {
    int* count{};
    void operator()(const ListViewSelectionChange&) const { ++*count; }
};

struct RecordActivation final {
    std::vector<std::size_t>* seen{};
    void operator()(const ListViewItemActivation& activation) const { (*seen).push_back(activation.index); }
};

struct RecordColumnClick final {
    std::vector<std::size_t>* seen{};
    void operator()(const ListViewColumnClick& click) const { (*seen).push_back(click.column); }
};

struct RecordWidth final {
    std::vector<double>* seen{};
    void operator()(const ListViewColumnWidthChange& change) const { (*seen).push_back(change.width); }
};

struct CancelChecking final {
    bool* cancel{};
    int* seen{};
    void operator()(ListViewItemCheck& check) const {
        ++*seen;
        check.cancel = *cancel;
    }
};

struct CountChecked final {
    int* count{};
    void operator()(const ListViewItemCheck&) const { ++*count; }
};

// A million rows made on demand; counts how often the view asks.
class CountingSource final : public ListViewItemSource {
public:
    explicit CountingSource(std::size_t count) : count_(count) {}
    [[nodiscard]] std::size_t item_count() const override { return count_; }
    void retrieve_item(std::size_t index, ListViewItem& item) const override {
        ++retrievals;
        item.stable_id = "row-" + std::to_string(index);
        item.text = "Row " + std::to_string(index);
        item.sub_items.assign(1U, std::to_string(index * 2U));
        item.image_key.clear();
        item.checked = index < checked.size() && checked[index];
    }
    void set_count(std::size_t count) { count_ = count; }

    mutable std::size_t retrievals{};
    std::vector<bool> checked{};

private:
    std::size_t count_{};
};

struct StoreVirtualCheck final {
    CountingSource* source{};
    void operator()(ListViewItemCheck& check) const {
        if ((*source).checked.size() <= check.index) (*source).checked.resize(check.index + 1U, false);
        (*source).checked[check.index] = check.new_value;
    }
};

// Records label edits; cancels the next before or after event on request.
struct RecordLabelEdits final {
    std::vector<ListViewLabelEdit>* seen{};
    bool* cancel{};
    void operator()(ListViewLabelEdit& edit) const {
        (*seen).push_back(edit);
        edit.cancel = *cancel;
    }
};

std::shared_ptr<Control> as_control(const std::shared_ptr<TextBox>& editor) {
    const std::shared_ptr<Control> control = editor;
    return control;
}

Point centre(Rect bounds) {
    const Point point{bounds.x + bounds.width * 0.5, bounds.y + bounds.height * 0.5};
    return point;
}

Point to_window(const ListView& view, Point local) {
    const Rect absolute = view.absolute_bounds();
    const Point point{absolute.x + local.x, absolute.y + local.y};
    return point;
}

void click(Window& window, Point point, Modifier modifiers = Modifier::none, std::uint32_t clicks = 1U) {
    PointerEvent down{PointerAction::down, PointerButton::primary, point};
    down.modifiers = modifiers;
    down.click_count = clicks;
    static_cast<void>(window.dispatch_pointer(down));
    PointerEvent up{PointerAction::up, PointerButton::primary, point};
    up.modifiers = modifiers;
    static_cast<void>(window.dispatch_pointer(up));
}

bool key(Window& window, std::uint32_t physical, Modifier modifiers = Modifier::none) {
    KeyEvent event{KeyAction::down, physical};
    event.modifiers = modifiers;
    const bool handled = window.dispatch_key(event);
    return handled;
}

std::vector<ListViewColumn> sample_columns() {
    std::vector<ListViewColumn> columns;
    columns.push_back({.stable_id = "name", .text = "Name", .width = 140.0});
    columns.push_back({.stable_id = "size", .text = "Size", .width = 80.0, .alignment = ListViewAlignment::right});
    columns.push_back({.stable_id = "kind", .text = "Kind", .width = 100.0});
    return columns;
}

std::vector<ListViewItem> sample_items(std::size_t count) {
    static const char* const names[] = {"Delta", "alpha", "Charlie", "bravo", "Echo", "foxtrot"};
    std::vector<ListViewItem> items;
    for (std::size_t index = 0; index < count; ++index) {
        ListViewItem item;
        item.stable_id = "item-" + std::to_string(index);
        item.text = std::string(names[index % 6U]) + " " + std::to_string(index);
        item.sub_items = {std::to_string(index + 1U) + " KB", index % 2U == 0U ? "Image" : "Text"};
        items.push_back(std::move(item));
    }
    return items;
}

// Replaces the items from inside after_label_edit, as an application that
// reloads its list on a rename.
struct ReloadOnAfterEdit final {
    ListView* view{};
    void operator()(ListViewLabelEdit&) const { (*view).set_items(sample_items(3U)); }
};

std::shared_ptr<ListView> details_view(std::size_t count) {
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("list"));
    (*view).set_columns(sample_columns());
    (*view).set_items(sample_items(count));
    return view;
}

void details_parts_and_paint() {
    std::shared_ptr<ListView> view = details_view(6U);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    const ControlAnatomy& anatomy = (*view).anatomy();
    require(validate_control_anatomy(anatomy).defect == AnatomyDefect::none && anatomy.control_id == "list_view" &&
                anatomy.item_systems.size() == 4U && find_item_system(anatomy, "column").has_value() &&
                find_item_part(anatomy.item_systems[2], "sort_glyph").has_value() &&
                find_item_part(anatomy.item_systems[3], "group_caption").has_value(),
            "ListView publishes a valid anatomy with item, sub-item, column and group systems");
    const ListView::Parts parts = (*view).parts();
    require(parts.well.present && parts.header.present && !parts.vertical_scroll_bar.present,
            "a short details view shows its well and header and no scroll bar");
    const std::vector<ListView::Item> items = (*view).realized_items();
    require(items.size() == 6U, "every row of a short list is realized");
    require(items[0].parts.text.present && items[0].parts.item_face.present && !items[0].parts.check_well.present &&
                !items[0].parts.icon.present,
            "a row without check boxes or images shows its face and text only");
    require(items[0].parts.cell.bounds.y >= parts.header.bounds.y + parts.header.bounds.height - 0.001 &&
                items[1].parts.cell.bounds.y > items[0].parts.cell.bounds.y,
            "rows sit under the header, one under another");
    const std::vector<ListView::SubItem> sub_items = (*view).realized_sub_items();
    require(sub_items.size() == 12U && sub_items[0].column == 1U && sub_items[1].column == 2U &&
                sub_items[0].stable_id == "item-0.sub.size",
            "each row realizes one sub-item per visible column after the first");
    std::vector<ListView::Column> columns = (*view).realized_columns();
    require(columns.size() == 3U && columns[1].stable_id == "list.column.size" &&
                columns[1].parts.column_label.present && columns[1].parts.column_divider.present &&
                !columns[1].parts.sort_glyph.present,
            "each visible column publishes its header parts; no sort glyph unsorted");
    require((*view).realized_item_count() == 6U + 12U + 3U && (*view).realized_item_system(0U).value == 0U &&
                (*view).realized_item_system(6U).value == 1U && (*view).realized_item_system(20U).value == 2U,
            "the generic path lists rows, then sub-items, then columns");
    (*view).set_sort_indicator(std::size_t{1U}, ListViewSortOrder::ascending);
    columns = (*view).realized_columns();
    require(columns[1].parts.sort_glyph.present && columns[1].states.sorted_ascending &&
                !columns[0].states.sorted_ascending,
            "the sort indicator shows a glyph and a custom state on its column only");
    require((*view).items()[0].text == "Delta 0", "a sort indicator never reorders the items");

    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 400.0, 240.0}));
    require(painted(painter, "Name") && painted(painter, "Delta 0") && painted(painter, "1 KB") &&
                painted(painter, "Image"),
            "the view paints headers, item text and sub-items");
}

void selection_by_pointer_and_keyboard() {
    std::shared_ptr<ListView> view = details_view(20U);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    int changes = 0;
    SubscriptionToken counted = (*view).selection_changed().subscribe(CountSelection{&changes});
    std::vector<std::size_t> activations;
    SubscriptionToken activated = (*view).item_activated().subscribe(RecordActivation{&activations});
    std::vector<ListView::Item> items = (*view).realized_items();
    click(window, to_window(*view, centre(items[1].parts.text.bounds)));
    require((*view).selected_count() == 1U && (*view).is_selected(1U) && (*view).focused_index() == 1U &&
                changes == 1,
            "a click selects the item, focuses it and reports the change");
    click(window, to_window(*view, centre(items[3].parts.text.bounds)), Modifier::control);
    require((*view).selected_indices() == std::vector<std::size_t>({1U, 3U}), "Control-click adds an item");
    click(window, to_window(*view, centre(items[5].parts.text.bounds)), Modifier::shift);
    require((*view).selected_indices() == std::vector<std::size_t>({3U, 4U, 5U}),
            "Shift-click selects the range from the anchor");
    click(window, to_window(*view, centre(items[3].parts.text.bounds)), Modifier::control);
    require(!(*view).is_selected(3U) && (*view).selected_count() == 2U, "Control-click removes a selected item");
    items = (*view).realized_items();
    require(items[5].states.selected && items[3].states.current && !items[3].states.selected,
            "rows publish selected and current");

    require(window.request_focus(view), "the view takes focus");
    require(key(window, PhysicalKey::down) && (*view).focused_index() == 4U && (*view).selected_count() == 1U &&
                (*view).is_selected(4U),
            "Down selects the next item alone");
    require(key(window, PhysicalKey::down, Modifier::shift) && key(window, PhysicalKey::down, Modifier::shift) &&
                (*view).selected_indices() == std::vector<std::size_t>({4U, 5U, 6U}),
            "Shift with Down extends the selection from the anchor");
    require(key(window, PhysicalKey::down, Modifier::control) && (*view).focused_index() == 7U &&
                (*view).selected_count() == 3U,
            "Control with Down moves the focus without selecting");
    require(key(window, PhysicalKey::space, Modifier::control) && (*view).is_selected(7U) &&
                (*view).selected_count() == 4U,
            "Control with Space toggles the focused item");
    require(key(window, PhysicalKey::end) && (*view).focused_index() == 19U && (*view).is_selected(19U),
            "End goes to the last item");
    require((*view).top_index() > 0U, "moving to an item out of view scrolls it into view");
    require(key(window, PhysicalKey::home) && (*view).focused_index() == 0U && (*view).top_index() == 0U,
            "Home goes to the first item and scrolls back");
    require(key(window, PhysicalKey::a, Modifier::control) && (*view).selected_count() == 20U,
            "Control with A selects everything");
    require(key(window, PhysicalKey::enter) && activations.size() == 1U && activations[0] == 0U,
            "Enter activates the focused item");
    items = (*view).realized_items();
    click(window, to_window(*view, centre(items[2].parts.text.bounds)));
    click(window, to_window(*view, centre(items[2].parts.text.bounds)), Modifier::none, 2U);
    require(activations.size() == 2U && activations[1] == 2U, "a double click activates the item");

    (*view).multi_select = false;
    require((*view).selected_count() == 1U, "single selection keeps one item");
    click(window, to_window(*view, centre(items[4].parts.text.bounds)), Modifier::shift);
    require((*view).selected_count() == 1U && (*view).is_selected(4U), "Shift-click selects one item in single mode");
}

void type_ahead() {
    std::shared_ptr<ListView> view = details_view(6U);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    require(window.request_focus(view), "the view takes focus");
    TextInputEvent typed{};
    typed.text_utf8 = "c";
    require(window.dispatch_text(typed) && (*view).focused_index() == 2U && (*view).is_selected(2U),
            "typing a letter selects the next item starting with it, ignoring case");
}

void check_boxes() {
    std::shared_ptr<ListView> view = details_view(4U);
    (*view).check_boxes = true;
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    bool cancel = true;
    int checking = 0;
    int checked = 0;
    SubscriptionToken vetoed = (*view).item_checking().subscribe(CancelChecking{&cancel, &checking});
    SubscriptionToken counted = (*view).item_checked().subscribe(CountChecked{&checked});
    std::vector<ListView::Item> items = (*view).realized_items();
    require(items[0].parts.check_well.present && items[0].parts.check.present &&
                items[0].parts.text.bounds.x > items[0].parts.check_well.bounds.x,
            "check boxes put a check well before the text");
    click(window, to_window(*view, centre(items[1].parts.check_well.bounds)));
    require(checking == 1 && checked == 0 && !(*view).item_checked(1U), "a cancelled check changes nothing");
    cancel = false;
    click(window, to_window(*view, centre(items[1].parts.check_well.bounds)));
    require(checking == 2 && checked == 1 && (*view).item_checked(1U) && (*view).selected_count() == 0U,
            "a click on the check box toggles it without selecting");
    require((*view).realized_items()[1].states.checked, "a checked item publishes checked");
    require(window.request_focus(view) && key(window, PhysicalKey::space) && (*view).item_checked(1U) == false,
            "Space toggles the focused item's check box");
    const std::vector<SemanticNode> children = (*view).semantic_virtual_children();
    require(children.size() == 3U + 4U && children[3].role == SemanticRole::row &&
                has_semantic_state(children[3].states, SemanticState::checkable) &&
                !has_semantic_state(children[3].states, SemanticState::checked) && children[3].value == "unchecked",
            "details rows with check boxes are checkable rows");
    require((*view).on_semantic_child_action("item-2", SemanticAction::press, {}) && (*view).item_checked(2U),
            "the semantic press toggles a check-list item");
    (*view).set_item_checked(3U, true);
    require((*view).item_checked(3U) && checked == 4, "the program sets a check and item_checked reports it");
}

void columns_click_sort_and_resize() {
    std::shared_ptr<ListView> view = details_view(6U);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    std::vector<std::size_t> clicks;
    SubscriptionToken clicked = (*view).column_click().subscribe(RecordColumnClick{&clicks});
    std::vector<double> widths;
    SubscriptionToken resized = (*view).column_width_changed().subscribe(RecordWidth{&widths});
    std::vector<ListView::Column> columns = (*view).realized_columns();
    click(window, to_window(*view, centre(columns[0].parts.column_label.bounds)));
    require(clicks.size() == 1U && clicks[0] == 0U && (*view).items()[0].text == "Delta 0",
            "a header click raises column_click and does not reorder");

    click(window, to_window(*view, centre((*view).realized_items()[0].parts.text.bounds)));
    require((*view).is_selected(0U), "the first item is selected");
    (*view).sort_items(0U, ListViewSortOrder::ascending);
    require((*view).items()[0].text == "alpha 1" && (*view).items()[1].text == "bravo 3" &&
                (*view).sort_column() == 0U && (*view).sort_order() == ListViewSortOrder::ascending,
            "sort_items orders by the column's text, folding ASCII case, and shows the glyph");
    const std::optional<std::size_t> delta = (*view).index_of("item-0");
    require(delta.has_value() && (*view).is_selected(*delta) && (*view).focused_index() == delta,
            "the selection and focus follow their item through a sort");
    (*view).sort_items(1U, ListViewSortOrder::descending);
    require((*view).items()[0].sub_items[0] == "6 KB", "sort_items sorts sub-item columns descending");

    columns = (*view).realized_columns();
    const Rect grip = columns[0].parts.column_grip.bounds;
    const Point start = to_window(*view, centre(grip));
    require(window.dispatch_pointer({PointerAction::down, PointerButton::primary, start}),
            "the divider takes the press");
    require((*view).realized_columns()[0].states.captured, "a column being resized is captured");
    static_cast<void>(window.dispatch_pointer({PointerAction::move, PointerButton::primary,
                                               {start.x + 30.0, start.y}}));
    require((*view).columns()[0].width == 170.0 && widths.empty(), "dragging the divider resizes live");
    static_cast<void>(window.dispatch_pointer({PointerAction::up, PointerButton::primary,
                                               {start.x + 30.0, start.y}}));
    require(widths.size() == 1U && widths[0] == 170.0 && clicks.size() == 1U,
            "releasing the divider reports the width once and is no header click");
    (*view).set_column_width(1U, 60.0);
    require(widths.size() == 2U && (*view).columns()[1].width == 60.0, "set_column_width reports the change");
}

void views_lay_out_items() {
    std::shared_ptr<ListView> view = details_view(30U);
    Window window(view, {360.0, 200.0});
    (*view).view = ListViewView::list;
    window.perform_layout();
    std::vector<ListView::Item> items = (*view).realized_items();
    require(items.size() > 2U && items[1].parts.cell.bounds.y > items[0].parts.cell.bounds.y &&
                items[1].parts.cell.bounds.x == items[0].parts.cell.bounds.x,
            "the list view fills a column top to bottom");
    require(!(*view).parts().header.present && (*view).realized_columns().empty() &&
                (*view).realized_sub_items().empty(),
            "only the details view has headers and sub-items");
    require((*view).parts().horizontal_scroll_bar.present && !(*view).parts().vertical_scroll_bar.present,
            "the list view scrolls sideways");
    require(window.request_focus(view) && key(window, PhysicalKey::right) && (*view).focused_index().has_value() &&
                *(*view).focused_index() > 1U,
            "Right moves to the next column of the list view");

    (*view).view = ListViewView::small_icon;
    window.perform_layout();
    items = (*view).realized_items();
    require(items[1].parts.cell.bounds.x > items[0].parts.cell.bounds.x &&
                items[1].parts.cell.bounds.y == items[0].parts.cell.bounds.y,
            "the small-icon view fills a row left to right");

    (*view).view = ListViewView::large_icon;
    window.perform_layout();
    items = (*view).realized_items();
    require(items[0].parts.text.present && items[0].parts.cell.bounds.height > 40.0 &&
                (*view).parts().vertical_scroll_bar.present,
            "the large-icon view shows tall cells and scrolls down");
    require(items[0].parts.text.bounds.y > items[0].parts.cell.bounds.y + 30.0,
            "the large-icon label sits under the icon's place");
}

// The tile view (Windows Forms' View.Tile with TileSize): tiles filled left
// to right, each with the item's text over one line per non-empty sub-item,
// as far as the tile's height allows.
void tile_view() {
    std::shared_ptr<ListView> view = details_view(12U);
    std::vector<ListViewItem> items_with_gap = sample_items(12U);
    items_with_gap[1].sub_items[0].clear();
    (*view).set_items(std::move(items_with_gap));
    Window window(view, {600.0, 260.0});
    require((*view).tile_size.read() == Size{}, "tiles are sized automatically by default");
    (*view).view = ListViewView::tile;
    window.perform_layout();
    std::vector<ListView::Item> items = (*view).realized_items();
    const Rect tile = items[0].parts.cell.bounds;
    require(items.size() > 2U && items[1].parts.cell.bounds.x > tile.x && items[1].parts.cell.bounds.y == tile.y,
            "the tile view fills a row left to right");
    require(tile.width > 200.0 && items[0].parts.text.present && items[0].parts.text.bounds.y > tile.y &&
                !items[0].parts.icon.present,
            "a default tile is wide, with its text centred and no icon without a large image list");
    require(!(*view).parts().header.present && (*view).realized_sub_items().empty(),
            "the tile view has no headers or sub-item cells");
    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 600.0, 260.0}));
    require(painted(painter, "Delta 0") && painted(painter, "1 KB") && painted(painter, "Image") &&
                painted(painter, "Text"),
            "a tile shows its text over its sub-items");
    require(!painted(painter, "2 KB") && items[1].parts.text.bounds.y > items[0].parts.text.bounds.y,
            "an empty sub-item takes no line, and a tile with fewer lines centres them lower");

    std::size_t per_row = 0U;
    for (const ListView::Item& item : items) {
        if (item.parts.cell.bounds.y == tile.y) ++per_row;
    }
    require(window.request_focus(view), "the view takes focus");
    (*view).set_focused_index(std::size_t{0U});
    require(key(window, PhysicalKey::down) && (*view).focused_index() == std::optional<std::size_t>(per_row),
            "Down moves to the tile below");

    (*view).tile_size = Size{300.0, 24.0};
    window.perform_layout();
    items = (*view).realized_items();
    require(items[0].parts.cell.bounds.width == 300.0 && items[0].parts.cell.bounds.height == 24.0,
            "a set tile size sizes every tile");
    RecordingPainter short_tiles;
    static_cast<void>(window.paint(short_tiles, {0.0, 0.0, 600.0, 260.0}));
    require(painted(short_tiles, "Delta 0") && !painted(short_tiles, "1 KB"),
            "a tile shows only the lines its height has room for");

    bool refused = false;
    try {
        (*view).tile_size = Size{10.0, 10.0};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    bool half_refused = false;
    try {
        (*view).tile_size = Size{0.0, 50.0};
    } catch (const std::invalid_argument&) {
        half_refused = true;
    }
    require(refused && half_refused && (*view).tile_size.read() == Size{300.0, 24.0},
            "a tile side outside the bounds, or one automatic side, is refused");
    (*view).tile_size = Size{};
    require((*view).tile_size.read() == Size{}, "an empty size returns to automatic tiles");
}

std::vector<ListViewGroup> sample_groups() {
    std::vector<ListViewGroup> groups;
    groups.push_back({.stable_id = "a", .header = "Alpha"});
    groups.push_back({.stable_id = "b", .header = "Beta", .header_alignment = ListViewAlignment::right});
    groups.push_back({.stable_id = "c", .header = "Empty"});
    return groups;
}

// Six items over two groups and the default group: items 0 and 3 in b, 1
// and 4 in a, 2 in none and 5 in a group that does not exist.
std::shared_ptr<ListView> grouped_view() {
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("list"));
    (*view).set_columns(sample_columns());
    std::vector<ListViewItem> items = sample_items(6U);
    items[0].group = "b";
    items[3].group = "b";
    items[1].group = "a";
    items[4].group = "a";
    items[5].group = "missing";
    (*view).set_items(std::move(items));
    (*view).set_groups(sample_groups());
    return view;
}

// Groups (Windows Forms' Groups and ShowGroups): a header line per group
// that holds items, the default group first, items in model order within
// a group; keys, ranges and accessibility follow the shown order.
void groups() {
    std::shared_ptr<ListView> view = grouped_view();
    Window window(view, {400.0, 320.0});
    window.perform_layout();
    require((*view).show_groups.read() && (*view).grouped() && (*view).default_group_header.read() == "Default",
            "a view with groups shows them by default");
    const std::vector<ListView::Group> groups = (*view).realized_groups();
    require(groups.size() == 3U && groups[0].index == 3U && groups[0].stable_id == "list.default-group" &&
                groups[1].index == 0U && groups[1].stable_id == "list.group.a" && groups[2].index == 1U &&
                groups[2].stable_id == "list.group.b",
            "the default group comes first, then the groups that hold items, in order");
    const std::vector<ListView::Item> items = (*view).realized_items();
    const std::vector<std::size_t> shown{2U, 5U, 1U, 4U, 0U, 3U};
    bool ordered = items.size() == shown.size();
    for (std::size_t position = 0; ordered && position < shown.size(); ++position) {
        ordered = items[position].index == shown[position] &&
                  (position == 0U || items[position].parts.cell.bounds.y > items[position - 1U].parts.cell.bounds.y);
    }
    require(ordered, "items are shown group by group, in model order within a group");
    require(groups[1].parts.group_caption.present && groups[1].parts.group_divider.present &&
                groups[1].parts.group_band.bounds.y >= items[1].parts.cell.bounds.y + items[1].parts.cell.bounds.height &&
                items[2].parts.cell.bounds.y >= groups[1].parts.group_band.bounds.y +
                                                    groups[1].parts.group_band.bounds.height - 0.001,
            "a group's header line stands between the groups, above its items");
    RecordingPainter painter;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 400.0, 320.0}));
    require(painted(painter, "Default") && painted(painter, "Alpha") && painted(painter, "Beta") &&
                !painted(painter, "Empty"),
            "headers are painted; a group without items is not shown");
    require(!(*view).item_index_at(to_window(*view, centre(groups[1].parts.group_caption.bounds))) &&
                (*view).item_index_at(to_window(*view, centre(items[2].parts.cell.bounds))) ==
                    std::optional<std::size_t>(1U),
            "a header is not an item; an item under the point is");

    require(window.request_focus(view), "the view takes focus");
    (*view).set_focused_index(std::size_t{2U});
    require(key(window, PhysicalKey::down) && (*view).focused_index() == std::optional<std::size_t>(5U) &&
                key(window, PhysicalKey::down) && (*view).focused_index() == std::optional<std::size_t>(1U),
            "Down follows the shown order across a header");
    require(key(window, PhysicalKey::end) && (*view).focused_index() == std::optional<std::size_t>(3U) &&
                key(window, PhysicalKey::home) && (*view).focused_index() == std::optional<std::size_t>(2U),
            "Home and End go to the first and last shown items");
    click(window, to_window(*view, centre(items[1].parts.text.bounds)));
    click(window, to_window(*view, centre(items[3].parts.text.bounds)), Modifier::shift);
    require((*view).selected_count() == 3U && (*view).is_selected(5U) && (*view).is_selected(1U) &&
                (*view).is_selected(4U) && !(*view).is_selected(2U) && !(*view).is_selected(0U),
            "a Shift range runs through the shown order");

    const std::vector<SemanticNode> children = (*view).semantic_virtual_children();
    require(children.size() == 3U + 3U && children[3].role == SemanticRole::group &&
                children[3].name == "Default" && children[3].children.size() == 2U &&
                children[3].children[0].name == "Charlie 2" && children[3].children[0].row_index == 1U &&
                children[4].name == "Alpha" && children[4].children[0].row_index == 3U &&
                children[5].stable_id == "list.group.b",
            "each shown group is a group node of its items, rows counted in the shown order");

    (*view).view = ListViewView::small_icon;
    window.perform_layout();
    const std::vector<ListView::Item> icons = (*view).realized_items();
    require(icons.size() == 6U && icons[1].parts.cell.bounds.y == icons[0].parts.cell.bounds.y &&
                icons[2].parts.cell.bounds.y > icons[1].parts.cell.bounds.y &&
                icons[2].parts.cell.bounds.x == icons[0].parts.cell.bounds.x,
            "an icon view starts each group on a new row");
    (*view).view = ListViewView::list;
    window.perform_layout();
    require(!(*view).grouped() && (*view).realized_groups().empty(), "the list view does not show groups");
    (*view).view = ListViewView::details;
    (*view).show_groups = false;
    window.perform_layout();
    require(!(*view).grouped() && (*view).realized_items()[0].index == 0U, "show_groups off shows the model order");
    (*view).show_groups = true;
    (*view).default_group_header = "Other";
    window.perform_layout();
    require((*view).realized_groups().size() == 3U, "groups show again");
    RecordingPainter renamed;
    static_cast<void>(window.paint(renamed, {0.0, 0.0, 400.0, 320.0}));
    require(painted(renamed, "Other"), "the default group's header can be renamed");

    bool refused = false;
    try {
        std::vector<ListViewGroup> repeated = sample_groups();
        repeated[1].stable_id = "a";
        (*view).set_groups(std::move(repeated));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).groups().size() == 3U, "a repeated group ID is refused");
}

// A long grouped view scrolls by lines of two extents.
void grouped_scrolling() {
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("list"));
    (*view).set_columns(sample_columns());
    std::vector<ListViewItem> items = sample_items(90U);
    for (std::size_t index = 0; index < items.size(); ++index) items[index].group = index % 3U == 0U ? "a" : "b";
    (*view).set_items(std::move(items));
    (*view).set_groups(sample_groups());
    Window window(view, {400.0, 200.0});
    window.perform_layout();
    require((*view).parts().vertical_scroll_bar.present, "a long grouped view scrolls");
    (*view).ensure_visible(89U);
    window.perform_layout();
    std::vector<ListView::Item> items_shown = (*view).realized_items();
    const Rect viewport = (*view).parts().well.bounds;
    require(!items_shown.empty() && items_shown.back().index == 89U &&
                items_shown.back().parts.cell.bounds.y + items_shown.back().parts.cell.bounds.height <=
                    viewport.y + viewport.height,
            "ensure_visible scrolls the last shown item fully into view");
    (*view).ensure_visible(0U);
    window.perform_layout();
    const std::vector<ListView::Group> groups = (*view).realized_groups();
    require(!groups.empty() && groups[0].stable_id == "list.group.a" && (*view).top_index() == 0U,
            "scrolling back to a group's first item shows its header");
}

// One owner-draw call as a handler saw it.
struct ContentCall final {
    std::size_t index{};
    std::size_t column{};
    std::string stable_id{};
    Rect content{};
    bool selected{};
};

// Draws a marker string into each item's content and records the call;
// asks for the default content when `draw_default` is set.
struct DrawContent final {
    std::vector<ContentCall>* calls{};
    bool draw_default{};
    void operator()(ItemContentPaint& item) const {
        (*calls).push_back({item.index, item.column, std::string(item.stable_id), item.content,
                            item.states.contains(AnatomyState::selected)});
        item.painter.draw_text_utf8({item.content.x, item.content.y}, "owner", item.font, item.text);
        item.draw_default = draw_default;
    }
};

// Replaces the view's items from inside its paint.
struct ReplaceItems final {
    ListView* view{};
    std::size_t* calls{};
    void operator()(ItemContentPaint&) const {
        ++*calls;
        (*view).set_items(sample_items(2U));
    }
};

// Leaves and re-enters the owner-draw mode, so that the next paint covers
// the whole view: a window paints only what changed.
void repaint_owner_drawn(ListView& view) {
    view.draw_mode = ItemDrawMode::normal;
    view.draw_mode = ItemDrawMode::owner_draw;
}

// Content-only owner drawing (owner decision W-Q6): the application draws
// each item's content and, in the details view, each sub-item cell; the
// plate and focus cue stay by role, and draw_default adds the default
// content.
void owner_draw() {
    std::shared_ptr<ListView> view = details_view(3U);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    require((*view).draw_mode.read() == ItemDrawMode::normal, "a view draws its own content by default");
    bool refused = false;
    try {
        (*view).draw_mode = static_cast<ItemDrawMode>(9);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).draw_mode.read() == ItemDrawMode::normal, "an unknown draw mode is refused");

    std::vector<ContentCall> calls;
    SubscriptionToken drawing = (*view).draw_item_content().subscribe(DrawContent{&calls, false});
    RecordingPainter normal;
    static_cast<void>(window.paint(normal, {0.0, 0.0, 400.0, 240.0}));
    require(calls.empty() && painted(normal, "Delta 0"), "the normal mode raises nothing");

    (*view).draw_mode = ItemDrawMode::owner_draw;
    (*view).set_selected(1U, true);
    window.perform_layout();
    RecordingPainter owner;
    static_cast<void>(window.paint(owner, {0.0, 0.0, 400.0, 240.0}));
    const std::vector<ListView::Item> items = (*view).realized_items();
    require(calls.size() == 9U && calls[0].index == 0U && calls[0].column == 0U && calls[1].column == 1U &&
                calls[2].column == 2U && calls[3].index == 1U && calls[3].stable_id == "item-1" && calls[3].selected,
            "each row raises its content, then each visible sub-item, in paint order");
    require(calls[0].content == items[0].parts.text.bounds, "a row's content is its icon and text area");
    require(!painted(owner, "Delta 0") && !painted(owner, "1 KB") && painted(owner, "owner") && painted(owner, "Name"),
            "owner-drawn rows show only what the handler drew; headers stay the view's");

    drawing.disconnect();
    calls.clear();
    SubscriptionToken with_default = (*view).draw_item_content().subscribe(DrawContent{&calls, true});
    repaint_owner_drawn(*view);
    RecordingPainter both;
    static_cast<void>(window.paint(both, {0.0, 0.0, 400.0, 240.0}));
    require(painted(both, "owner") && painted(both, "Delta 0") && painted(both, "1 KB"),
            "draw_default adds the default content after the handler's");
    with_default.disconnect();

    calls.clear();
    (*view).view = ListViewView::tile;
    SubscriptionToken tiles = (*view).draw_item_content().subscribe(DrawContent{&calls, false});
    repaint_owner_drawn(*view);
    window.perform_layout();
    RecordingPainter tiled;
    static_cast<void>(window.paint(tiled, {0.0, 0.0, 400.0, 240.0}));
    require(calls.size() == 9U && calls[0].column == 0U && calls[1].column == 1U && calls[2].column == 2U &&
                calls[1].content.y > calls[0].content.y && !painted(tiled, "1 KB"),
            "a tile raises its content, then each sub-item line");
    tiles.disconnect();
    (*view).view = ListViewView::details;

    std::size_t replaced = 0U;
    SubscriptionToken replacing = (*view).draw_item_content().subscribe(ReplaceItems{view.get(), &replaced});
    repaint_owner_drawn(*view);
    RecordingPainter interrupted;
    static_cast<void>(window.paint(interrupted, {0.0, 0.0, 400.0, 240.0}));
    require(replaced == 1U && (*view).item_count() == 2U,
            "a handler that replaces the items ends that paint at once");
    replacing.disconnect();
}

// The insertion mark (Windows Forms' InsertionMark): a drop-role bar in the
// gap beside an item, across rows in the details and list views and
// between cells in the icon views, cleared by any change to the items or
// their order.
void insertion_mark() {
    std::shared_ptr<ListView> view = details_view(6U);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    require(!(*view).insertion_mark().has_value() && !(*view).parts().insertion_mark.present,
            "a view starts without an insertion mark");
    RecordingPainter plain;
    static_cast<void>(window.paint(plain, {0.0, 0.0, 400.0, 240.0}));

    require((*view).show_insertion_mark({.index = 2U, .after = false}), "a mark beside an item is shown");
    window.perform_layout();
    std::vector<ListView::Item> items = (*view).realized_items();
    ListView::Parts parts = (*view).parts();
    const Rect row = items[2].parts.cell.bounds;
    require(parts.insertion_mark.present && parts.insertion_mark.states.contains(AnatomyState::drop_accepted) &&
                std::abs(parts.insertion_mark.bounds.y + parts.insertion_mark.bounds.height * 0.5 - row.y) < 0.001 &&
                parts.insertion_mark.bounds.height == 2.0,
            "a details view's mark lies across the top of its row, drop-accepted");
    RecordingPainter marked;
    static_cast<void>(window.paint(marked, {0.0, 0.0, 400.0, 240.0}));
    require(marked.fills > plain.fills, "the mark is painted");

    require((*view).show_insertion_mark({.index = 2U, .after = true}), "a mark after an item is shown");
    window.perform_layout();
    parts = (*view).parts();
    require(std::abs(parts.insertion_mark.bounds.y + parts.insertion_mark.bounds.height * 0.5 -
                     (row.y + row.height)) < 0.001,
            "a mark after the item lies across its bottom");

    const Point lower_half{row.x + 20.0, row.y + row.height * 0.75};
    const std::optional<ListViewInsertionMark> nearest = (*view).insertion_mark_at(to_window(*view, lower_half));
    require(nearest && (*nearest).index == 2U && (*nearest).after, "a point past an item's middle names the gap after it");
    const Point far_below{row.x + 20.0, 1000.0};
    const std::optional<ListViewInsertionMark> last = (*view).insertion_mark_at(to_window(*view, far_below));
    require(last && (*last).index == 5U && (*last).after, "a point past the last item names the gap after it");

    require(!(*view).show_insertion_mark({.index = 6U, .after = false}) && !(*view).insertion_mark().has_value(),
            "an index past the items is refused and clears the mark");
    require((*view).show_insertion_mark({.index = 1U, .after = false}), "the mark is shown again");
    (*view).sort_items(0U, ListViewSortOrder::ascending);
    require(!(*view).insertion_mark().has_value(), "sorting clears the mark");
    require((*view).show_insertion_mark({.index = 1U, .after = false}), "the mark is shown once more");
    (*view).set_items(sample_items(4U));
    require(!(*view).insertion_mark().has_value(), "replacing the items clears the mark");

    (*view).view = ListViewView::large_icon;
    window.perform_layout();
    require((*view).show_insertion_mark({.index = 1U, .after = true}), "an icon view shows a mark");
    window.perform_layout();
    items = (*view).realized_items();
    parts = (*view).parts();
    const Rect cell = items[1].parts.cell.bounds;
    require(parts.insertion_mark.present && parts.insertion_mark.bounds.width == 2.0 &&
                std::abs(parts.insertion_mark.bounds.x + 1.0 - (cell.x + cell.width)) < 0.001,
            "an icon view's mark stands down the cell's right side when after it");
    (*view).clear_insertion_mark();
    require(!(*view).insertion_mark().has_value() && !(*view).parts().insertion_mark.present,
            "clear_insertion_mark removes the mark");
}

void label_editing() {
    std::shared_ptr<ListView> view = details_view(20U);
    const std::shared_ptr<Control> view_control = view;
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    std::vector<ListViewLabelEdit> before;
    std::vector<ListViewLabelEdit> after;
    bool cancel_before = false;
    bool cancel_after = false;
    SubscriptionToken before_token = (*view).before_label_edit().subscribe(RecordLabelEdits{&before, &cancel_before});
    SubscriptionToken after_token = (*view).after_label_edit().subscribe(RecordLabelEdits{&after, &cancel_after});
    require(window.request_focus(view), "the view takes focus");
    (*view).set_focused_index(2U);
    require(!key(window, PhysicalKey::f2) && !(*view).begin_label_edit(2U) && before.empty() &&
                !(*view).label_editor(),
            "without label_edit, F2 and begin_label_edit begin nothing and make no editor");

    (*view).label_edit = true;
    require(key(window, PhysicalKey::f2) && (*view).label_edit_index() == 2U && before.size() == 1U &&
                before[0].index == 2U && before[0].item_id == "item-2" && !before[0].label,
            "F2 raises before_label_edit for the focused item and puts it in edit");
    const std::shared_ptr<TextBox> editor = (*view).label_editor();
    window.perform_layout();
    std::vector<ListView::Item> items = (*view).realized_items();
    require(editor && window.focused_control() == as_control(editor) && (*editor).text.read() == "Charlie 2" &&
                (*editor).selected_text() == "Charlie 2",
            "the editor takes focus with the item's text selected");
    require((*view).parts().label_editor.present &&
                (*view).parts().label_editor.bounds == items[2].parts.text.bounds,
            "the label_editor part covers the item's text");
    require(window.dispatch_text({"Renamed"}) && key(window, PhysicalKey::enter) && !(*view).label_edit_index() &&
                after.size() == 1U && after[0].label == std::optional<std::string>("Renamed") &&
                (*view).items()[2].text == "Renamed" && window.focused_control() == view_control &&
                !(*view).parts().label_editor.present,
            "Enter commits the typed label, closes the editor and returns focus to the view");

    require(key(window, PhysicalKey::f2) && window.dispatch_text({"Dropped"}) && key(window, PhysicalKey::escape) &&
                after.size() == 2U && !after[1].label && (*view).items()[2].text == "Renamed" &&
                window.focused_control() == view_control,
            "Escape cancels: after_label_edit has no label and the text stays");

    cancel_after = true;
    require(key(window, PhysicalKey::f2) && window.dispatch_text({"Refused"}) && key(window, PhysicalKey::enter) &&
                after.size() == 3U && after[2].label == std::optional<std::string>("Refused") &&
                (*view).items()[2].text == "Renamed",
            "an after_label_edit handler refuses the label and the item keeps its text");
    cancel_after = false;

    cancel_before = true;
    require(!(*view).begin_label_edit(4U) && !(*view).label_edit_index() && before.size() == 4U,
            "a before_label_edit handler keeps the item out of edit");
    cancel_before = false;

    require((*view).begin_label_edit(1U) && window.dispatch_text({"Clicked"}), "begin_label_edit opens an edit");
    items = (*view).realized_items();
    click(window, to_window(*view, centre(items[5].parts.text.bounds)));
    require(!(*view).label_edit_index() && (*view).items()[1].text == "Clicked" && (*view).is_selected(5U) &&
                (*view).focused_index() == 5U,
            "a press elsewhere in the view commits the edit and then selects as usual");

    require((*view).begin_label_edit(3U) && window.dispatch_text({"Focus"}) && window.request_focus(view) &&
                !(*view).label_edit_index() && (*view).items()[3].text == "Focus",
            "focus leaving the editor commits the edit");

    require((*view).begin_label_edit(0U) && (*view).begin_label_edit(6U) && (*view).label_edit_index() == 6U &&
                (*view).items()[0].text == "Delta 0" && after.back().index == 0U,
            "beginning another edit commits the one in progress");
    (*view).ensure_visible(19U);
    window.perform_layout();
    require((*view).label_edit_index() == 6U && window.focused_control() == as_control(editor) &&
                !(*view).parts().label_editor.present,
            "scrolling keeps the edit, its editor at no size while the item is out of view");
    (*view).ensure_visible(6U);
    window.perform_layout();
    require((*view).parts().label_editor.present, "the editor returns with its item");

    const std::size_t ended = after.size();
    (*view).sort_items(0U, ListViewSortOrder::ascending);
    require(!(*view).label_edit_index() && after.size() == ended + 1U && !after.back().label &&
                after.back().item_id == "item-6",
            "sorting the items cancels the edit");

    require((*view).begin_label_edit(1U), "an edit opens again");
    (*view).label_edit = false;
    require(!(*view).label_edit_index() && after.size() == ended + 2U && !after.back().label,
            "turning label_edit off cancels the edit");

    // An after_label_edit handler that replaces the items: the commit finds
    // no item with the edited ID to rename.
    (*view).label_edit = true;
    require((*view).begin_label_edit(7U) && window.dispatch_text({"Lost"}), "an edit of the eighth item opens");
    SubscriptionToken reload = (*view).after_label_edit().subscribe(ReloadOnAfterEdit{view.get()});
    (*view).end_label_edit();
    require((*view).item_count() == 3U && !(*view).label_edit_index() && (*view).items()[0].text == "Delta 0",
            "a handler that replaces the items leaves the new items as they are");
    reload.disconnect();
    bool threw = false;
    try {
        static_cast<void>((*view).begin_label_edit(3U));
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "begin_label_edit refuses an index past the end");
}

// The slow second click (Windows' list view): a single press on the text of
// the item that was already focused and selected begins its label edit once
// the window's double-click interval passes without a second press.
void slow_click_label_editing() {
    std::shared_ptr<ListView> view = details_view(20U);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    (*view).label_edit = true;
    require(window.request_focus(view), "the view takes focus");
    const std::chrono::milliseconds interval(window.presentation_settings().double_click_milliseconds);
    const std::chrono::milliseconds margin(50);
    std::vector<ListView::Item> items = (*view).realized_items();
    const Point second = to_window(*view, centre(items[2].parts.text.bounds));
    const Point fourth = to_window(*view, centre(items[4].parts.text.bounds));

    click(window, second);
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval + margin));
    require((*view).focused_index() == 2U && (*view).is_selected(2U) && !(*view).label_edit_index(),
            "a press that focuses and selects an item begins no edit");

    click(window, second);
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval / 2));
    require(!(*view).label_edit_index(), "the slow click waits for the double-click interval");
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval + margin));
    require((*view).label_edit_index() == 2U, "a slow second click on the focused item's text begins its edit");
    require(key(window, PhysicalKey::escape) && !(*view).label_edit_index(), "Escape cancels the slow click's edit");

    std::vector<std::size_t> activations;
    SubscriptionToken activated = (*view).item_activated().subscribe(RecordActivation{&activations});
    click(window, second);
    click(window, second, Modifier::none, 2U);
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval + margin));
    require(activations == std::vector<std::size_t>({2U}) && !(*view).label_edit_index(),
            "a double click activates the item and begins no edit");

    click(window, second, Modifier::control);
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval + margin));
    require(!(*view).label_edit_index(), "a press with a modifier begins no edit");

    click(window, fourth);
    click(window, fourth);
    static_cast<void>(key(window, PhysicalKey::a));
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval + margin));
    require(!(*view).label_edit_index(), "a key pressed in the wait drops the slow click");

    click(window, fourth);
    (*view).sort_items(0U, ListViewSortOrder::descending);
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval + margin));
    require(!(*view).label_edit_index(), "a changed model drops the slow click");

    window.perform_layout();
    items = (*view).realized_items();
    const Point first = to_window(*view, centre(items[0].parts.text.bounds));
    click(window, first);
    click(window, first);
    (*view).label_edit = false;
    (*view).label_edit = true;
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval + margin));
    require(!(*view).label_edit_index(), "turning label_edit off drops the slow click");

    click(window, first);
    PointerEvent wheel{PointerAction::wheel, PointerButton::none, first};
    wheel.wheel_delta = {0.0, -1.0};
    static_cast<void>(window.dispatch_pointer(wheel));
    static_cast<void>(window.poll_frame_schedule(FrameClock::now() + interval + margin));
    require(!(*view).label_edit_index(), "scrolling with the wheel drops the slow click");
}

void virtual_label_editing() {
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("virtual"));
    (*view).set_columns(sample_columns());
    const std::shared_ptr<CountingSource> source = std::make_shared<CountingSource>(1000U);
    (*view).set_virtual_source(source);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    std::vector<ListViewLabelEdit> after;
    bool cancel = false;
    SubscriptionToken token = (*view).after_label_edit().subscribe(RecordLabelEdits{&after, &cancel});
    (*view).label_edit = true;
    require((*view).begin_label_edit(4U) && (*(*view).label_editor()).text.read() == "Row 4",
            "a virtual item's label is edited from the source's text");
    require(window.dispatch_text({"Four"}) && key(window, PhysicalKey::enter) && after.size() == 1U &&
                after[0].item_id == "row-4" && after[0].label == std::optional<std::string>("Four"),
            "the application receives the label; the source still owns the text");
    (*source).set_count(2U);
    require((*view).begin_label_edit(1U), "an edit opens");
    (*view).refresh_virtual_items();
    require(!(*view).label_edit_index() && after.size() == 2U && !after[1].label,
            "refresh_virtual_items cancels the edit");
}

void scrolling() {
    std::shared_ptr<ListView> view = details_view(200U);
    std::vector<ListViewColumn> wide = sample_columns();
    wide[2].width = 400.0;
    (*view).set_columns(wide);
    Window window(view, {300.0, 200.0});
    window.perform_layout();
    const ListView::Parts parts = (*view).parts();
    require(parts.vertical_scroll_bar.present && parts.horizontal_scroll_bar.present && parts.scroll_corner.present,
            "a view with more rows and columns than fit shows both bars and their corner");
    const std::size_t rows = (*view).realized_items().size();
    require(rows < 20U, "only the visible rows are realized");
    const Point inside = to_window(*view, centre((*view).realized_items()[0].parts.cell.bounds));
    PointerEvent wheel{PointerAction::wheel, PointerButton::none, inside};
    wheel.wheel_delta = {0.0, -1.0};
    static_cast<void>(window.dispatch_pointer(wheel));
    require((*view).top_index() == 3U, "the wheel scrolls three rows");
    (*view).set_horizontal_offset(50.0);
    require((*view).horizontal_offset() == 50.0 &&
                (*view).realized_columns()[0].parts.column_header.bounds.width < 140.0,
            "a sideways offset scrolls the columns");
    (*view).ensure_visible(150U);
    require((*view).top_index() <= 150U && (*view).top_index() + rows > 150U, "ensure_visible scrolls to an item");
}

void virtual_mode() {
    std::shared_ptr<CountingSource> source = std::make_shared<CountingSource>(1'000'000U);
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("virtual"));
    (*view).set_columns(sample_columns());
    (*view).set_virtual_source(source);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    require((*view).virtual_mode() && (*view).item_count() == 1'000'000U && (*view).items().empty(),
            "a virtual view takes its count from the source");
    (*source).retrievals = 0U;
    const std::vector<ListView::Item> items = (*view).realized_items();
    require(items.size() < 20U && items[0].stable_id == "row-0", "a virtual view realizes a screenful");
    RecordingPainter painter;
    (*source).retrievals = 0U;
    static_cast<void>(window.paint(painter, {0.0, 0.0, 400.0, 240.0}));
    require((*source).retrievals > 0U && (*source).retrievals <= 40U && painted(painter, "Row 3"),
            "painting retrieves only the visible items");
    require(window.request_focus(view) && key(window, PhysicalKey::end) && (*view).focused_index() == 999'999U &&
                (*view).is_selected(999'999U),
            "End reaches the last virtual item");
    require((*view).realized_items().back().stable_id == "row-999999", "the view scrolls to the end");
    require(key(window, PhysicalKey::a, Modifier::control) && (*view).selected_count() == 1'000'000U,
            "select-all selects every virtual item");
    (*source).set_count(10U);
    (*view).refresh_virtual_items();
    require((*view).item_count() == 10U && (*view).selected_count() == 10U && !(*view).focused_index().has_value(),
            "a refresh keeps the selection below the new count and drops a focus past it");
    (*view).check_boxes = true;
    SubscriptionToken stored = (*view).item_checking().subscribe(StoreVirtualCheck{source.get()});
    window.perform_layout();
    const std::vector<ListView::Item> rows = (*view).realized_items();
    click(window, to_window(*view, centre(rows[2].parts.check_well.bounds)));
    require((*view).item_checked(2U) && (*view).realized_items()[2].states.checked,
            "a virtual check is stored by the application and shown from the source");
    bool threw = false;
    try {
        (*view).set_items(sample_items(2U));
    } catch (const std::logic_error&) {
        threw = true;
    }
    require(threw, "a virtual view refuses ordinary items");
    threw = false;
    try {
        (*view).set_item_checked(0U, true);
    } catch (const std::logic_error&) {
        threw = true;
    }
    require(threw, "a virtual view's source owns the checked state");
}

void virtual_splice() {
    // Audit N94: bulk insertion and removal in a million-item virtual list
    // without a refresh; selection and focus follow their items.
    std::shared_ptr<CountingSource> source = std::make_shared<CountingSource>(1'000'000U);
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("splice"));
    (*view).set_columns(sample_columns());
    (*view).set_virtual_source(source);
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    require(window.request_focus(view), "the splice view takes focus");
    require(key(window, PhysicalKey::end) && (*view).focused_index() == 999'999U, "focus at the end");
    (*view).set_selected(10U, true);
    (*view).set_selected(600'000U, true);
    (*view).set_selected(900'000U, true);
    (*source).set_count(1'000'000U + 250'000U);
    (*view).splice_virtual_items(500'000U, 0U, 250'000U);
    require((*view).item_count() == 1'250'000U && (*view).is_selected(10U) && (*view).is_selected(850'000U) &&
                (*view).is_selected(1'150'000U) && (*view).is_selected(1'249'999U) &&
                (*view).focused_index() == 1'249'999U && !(*view).is_selected(600'000U),
            "a bulk insertion moves the selection and focus past it");
    (*source).set_count(1'250'000U - 300'000U);
    (*view).splice_virtual_items(800'000U, 300'000U, 0U);
    require((*view).item_count() == 950'000U && (*view).is_selected(10U) && (*view).selected_count() == 3U &&
                (*view).is_selected(850'000U) &&
                (*view).focused_index() == 949'999U,
            "a bulk removal drops the selection inside it and shifts the rest");
    window.perform_layout();
    require((*view).realized_items().back().stable_id == "row-949999", "the view still shows the focused end");
    bool threw = false;
    try {
        (*view).splice_virtual_items(0U, 1U, 0U);
    } catch (const std::logic_error&) {
        threw = true;
    }
    require(threw && (*view).item_count() == 950'000U, "a splice the source does not match is refused");
    threw = false;
    try {
        (*view).splice_virtual_items(950'001U, 0U, 0U);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "a splice past the items is refused");
    const ListViewSpliceRemap remap{5U, 8U, 1U};
    require(remap.apply(std::optional<std::size_t>{4U}) == 4U && !remap.apply(std::optional<std::size_t>{6U}) &&
                remap.apply(std::optional<std::size_t>{9U}) == 7U && !remap.apply(std::nullopt),
            "the splice remap keeps, drops and shifts indices");
}

void semantics() {
    std::shared_ptr<ListView> view = details_view(3U);
    (*view).accessible_name = "Files";
    Window window(view, {400.0, 240.0});
    window.perform_layout();
    const SemanticDescriptor descriptor = (*view).semantic_descriptor();
    require(descriptor.role == SemanticRole::table && descriptor.name == "Files" && descriptor.row_count == 3U &&
                descriptor.column_count == 3U && has_semantic_state(descriptor.states, SemanticState::multiselectable),
            "the details view is a named multiple-selection table sized by its items and columns");
    const std::vector<SemanticNode> children = (*view).semantic_virtual_children();
    require(children.size() == 3U + 3U && children[0].role == SemanticRole::column_header &&
                children[0].name == "Name" && children[0].column_index == 1U && children[2].column_index == 3U &&
                children[3].role == SemanticRole::row && children[3].name == "Delta 0" &&
                children[3].row_index == 1U && children[3].description.empty(),
            "column headers carry their column and rows carry their row");
    const SemanticNode& row = children[3];
    require(row.children.size() == 3U && row.children[0].role == SemanticRole::cell &&
                row.children[0].value == "Delta 0" && row.children[1].name == "Size" &&
                row.children[1].value == "1 KB" && row.children[2].value == "Image" &&
                row.children[2].row_index == 1U && row.children[2].column_index == 3U,
            "a row holds one cell per column, the first holding the item's text");
    (*view).multi_select = false;
    (*view).view = ListViewView::list;
    window.perform_layout();
    const SemanticDescriptor list = (*view).semantic_descriptor();
    require(list.role == SemanticRole::list && !list.row_count &&
                !has_semantic_state(list.states, SemanticState::multiselectable),
            "the other views are lists, multiple-selection only while multi_select is set");
    (*view).view = ListViewView::details;
    window.perform_layout();
    std::vector<std::size_t> clicks;
    SubscriptionToken clicked = (*view).column_click().subscribe(RecordColumnClick{&clicks});
    require((*view).on_semantic_child_action("list.column.kind", SemanticAction::press, {}) && clicks.size() == 1U &&
                clicks[0] == 2U,
            "pressing a header node raises column_click");
    require((*view).on_semantic_child_action("item-1", SemanticAction::select, {}) && (*view).is_selected(1U) &&
                (*view).semantic_descriptor().value == "alpha 1",
            "the select action selects an item, and the list's value names it");
    require(!(*view).on_semantic_child_action("missing", SemanticAction::select, {}),
            "an unknown item refuses the action");
}

// The properties on capabilities (B7): a refused write leaves the cell, an
// equal write does nothing, and the hand-written hooks keep what the setters
// did.
void capabilities() {
    std::shared_ptr<ListView> view = details_view(4U);
    Window window(view, {400.0, 240.0});
    window.perform_layout();

    // tile_size lays the view out again only while it shows tiles.
    (*view).tile_size = Size{120.0, 48.0};
    require(!has_dirty((*view).dirty(), Dirty::arrange), "a tile size outside the tile view lays nothing out");
    (*view).view = ListViewView::tile;
    window.perform_layout();
    (*view).tile_size = Size{140.0, 48.0};
    require(has_dirty((*view).dirty(), Dirty::arrange), "a tile size in the tile view lays the tiles out again");
    (*view).view = ListViewView::details;
    window.perform_layout();

    // Turning multiple selection off keeps the focused item and raises one
    // selection change; the equal write after it raises none.
    int changes = 0;
    const SubscriptionToken counted = (*view).selection_changed().subscribe(CountSelection{&changes});
    (*view).select_all();
    (*view).set_focused_index(2U);
    changes = 0;
    (*view).multi_select = false;
    require(changes == 1 && (*view).selected_count() == 1U && (*view).is_selected(2U),
            "single selection keeps the focused item and publishes once");
    (*view).multi_select = false;
    require(changes == 1, "an equal multi_select write raises nothing");
    static_cast<void>(counted);

    // An image list of the view's own window is taken; another window's, or
    // a disposed one, is refused and the cell keeps its value.
    const std::shared_ptr<ImageList> images = std::make_shared<ImageList>(window, Size{16.0, 16.0});
    (*view).small_image_list = images;
    require((*view).small_image_list.read() == images, "the window's image list is taken");
    std::shared_ptr<Panel> other_root = make_control<Panel>(StableId("capabilities.other"));
    const std::unique_ptr<Window> other_window = std::make_unique<Window>(other_root, Size{10.0, 10.0});
    const std::shared_ptr<ImageList> foreign = std::make_shared<ImageList>(*other_window);
    bool refused = false;
    try {
        (*view).large_image_list = foreign;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).large_image_list.read() == nullptr,
            "another window's image list is refused without a change");
    const std::shared_ptr<ImageList> disposed = std::make_shared<ImageList>(window, Size{16.0, 16.0});
    (*disposed).dispose();
    refused = false;
    try {
        (*view).small_image_list = disposed;
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).small_image_list.read() == images, "a disposed image list is refused");
    (*view).small_image_list = nullptr;
    require(!(*view).small_image_list.read(), "null removes the image list");

    // A new font reaches the label editor of an edit in progress.
    require(window.request_focus(view), "the view takes focus");
    (*view).label_edit = true;
    require((*view).begin_label_edit(1U), "an edit begins");
    const FontSpec larger{FontRole::content, 14.0, 400, false};
    (*view).font = larger;
    require((*(*view).label_editor()).font.read() == larger, "the label editor takes the view's new font");
    (*view).cancel_label_edit();

    // Refusals leave the cells as they were.
    refused = false;
    try {
        (*view).view = static_cast<ListViewView>(9);
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).view.read() == ListViewView::details, "an unknown view is refused");
    refused = false;
    try {
        (*view).default_group_header = std::string("\xff");
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).default_group_header.read() == "Default", "an invalid UTF-8 header is refused");
    refused = false;
    try {
        (*view).font = FontSpec{FontRole::content, -1.0, 400, false};
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*view).font.read() == larger, "an invalid font is refused");
}

void refusals() {
    std::shared_ptr<ListView> view = make_control<ListView>(StableId("refusals"));
    bool threw = false;
    try {
        std::vector<ListViewItem> items = sample_items(2U);
        items[1].stable_id = items[0].stable_id;
        (*view).set_items(std::move(items));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*view).item_count() == 0U, "duplicate item IDs are refused and leave the model unchanged");
    threw = false;
    try {
        std::vector<ListViewColumn> columns = sample_columns();
        columns[0].width = 5.0;
        (*view).set_columns(std::move(columns));
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    require(threw && (*view).columns().empty(), "a column narrower than the minimum is refused");
    threw = false;
    try {
        (*view).set_selected(0U, true);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "selecting past the end is refused");
    threw = false;
    try {
        (*view).refresh_virtual_items();
    } catch (const std::logic_error&) {
        threw = true;
    }
    require(threw, "refreshing an ordinary view is refused");
    (*view).set_items(sample_items(2U));
    threw = false;
    try {
        (*view).sort_items(0U, ListViewSortOrder::ascending);
    } catch (const std::out_of_range&) {
        threw = true;
    }
    require(threw, "sorting by a missing column is refused");
}

} // namespace

int main() {
    details_parts_and_paint();
    selection_by_pointer_and_keyboard();
    type_ahead();
    check_boxes();
    columns_click_sort_and_resize();
    views_lay_out_items();
    tile_view();
    groups();
    grouped_scrolling();
    owner_draw();
    insertion_mark();
    label_editing();
    slow_click_label_editing();
    virtual_label_editing();
    scrolling();
    virtual_mode();
    virtual_splice();
    semantics();
    capabilities();
    refusals();
    std::cout << "ListView: parts, views, selection, keys, checks, columns, sorting, resizing, owner drawing, "
                 "insertion mark, label editing, "
                 "virtual mode, "
                 "scrolling, semantics, capabilities and refusals passed\n";
    return 0;
}
