// In-place collections pilot (design accepted 2026-11-07): the collection
// edit's remapping, the dirty record's coalescing and take, and ListBox's
// `items` cell: refused edits change nothing, the selection and check
// states follow each edit, and the flush repaints only the shown rows an
// edit reaches (a replaced row; the rows from a structural edit down; none
// for an edit below the shown window).
#include "gui_forms/gui_forms.hpp"
#include "support/clip_tracking_painter.hpp"
#include "support/named_callbacks.hpp"

#include <cstddef>
#include <cstdint>
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

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "collection cell failure: " << message << '\n';
    std::exit(EXIT_FAILURE);
}

bool same_rect(const Rect a, const Rect b) {
    const bool same = a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    return same;
}

void test_edit_remap() {
    const CollectionEdit assigned = CollectionEdit::assigned(3U);
    require(assigned.kind() == CollectionEditKind::assign && assigned.structural() &&
                assigned.remap(2U) == std::optional<std::size_t>{2U} && !assigned.remap(3U),
            "an assignment keeps the positions below its length");
    const CollectionEdit inserted = CollectionEdit::inserted(2U, 2U);
    require(inserted.remap(1U) == std::optional<std::size_t>{1U} &&
                inserted.remap(2U) == std::optional<std::size_t>{4U},
            "an insertion moves the positions from its index");
    const CollectionEdit erased = CollectionEdit::erased(2U, 2U);
    require(erased.remap(1U) == std::optional<std::size_t>{1U} && !erased.remap(2U) && !erased.remap(3U) &&
                erased.remap(4U) == std::optional<std::size_t>{2U},
            "an erasure removes its positions and moves the later ones");
    const CollectionEdit replaced = CollectionEdit::replaced(5U);
    require(!replaced.structural() && replaced.count() == 1U &&
                replaced.remap(5U) == std::optional<std::size_t>{5U},
            "a replacement keeps every position");
}

void test_dirty_coalescing() {
    CollectionDirty dirty;
    std::vector<std::uint32_t> rows;
    require(!dirty.any() && !dirty.take(rows) && rows.empty(), "a new record is clean");
    dirty.reserve(100U);
    dirty.record(CollectionEdit::replaced(70U));
    dirty.record(CollectionEdit::replaced(3U));
    dirty.record(CollectionEdit::replaced(40U));
    require(dirty.any(), "a replacement makes the record dirty");
    std::optional<std::size_t> watermark = dirty.take(rows);
    require(!watermark && rows == std::vector<std::uint32_t>{3U, 40U, 70U} && !dirty.any(),
            "replacements are taken in increasing order and cleared");

    rows.clear();
    dirty.record(CollectionEdit::replaced(3U));
    dirty.record(CollectionEdit::replaced(50U));
    dirty.record(CollectionEdit::erased(20U, 1U));
    dirty.record(CollectionEdit::inserted(30U, 4U));
    dirty.record(CollectionEdit::replaced(60U));
    watermark = dirty.take(rows);
    require(watermark == std::optional<std::size_t>{20U} && rows == std::vector<std::uint32_t>{3U},
            "the lowest structural index wins and covers the later replacements");

    rows.assign(1U, 99U);
    dirty.record(CollectionEdit::replaced(3U));
    dirty.record(CollectionEdit::assigned(5U));
    watermark = dirty.take(rows);
    require(watermark == std::optional<std::size_t>{0U} && rows == std::vector<std::uint32_t>{99U},
            "an assignment makes everything dirty and keeps the caller's storage");
}

constexpr double list_width = 200.0;
// Four whole rows of 26 inside the 2-unit edge, and no partial row.
constexpr double list_height = 108.0;

std::vector<std::string> numbered(const std::size_t count) {
    std::vector<std::string> items;
    items.reserve(count);
    for (std::size_t index = 0U; index < count; ++index) items.push_back("Item " + std::to_string(index));
    return items;
}

// Lays out and paints the window, then answers the damage the next layout
// requests after `edit` ran.
struct ListFixture final {
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("collection.list"));
    std::unique_ptr<Window> window;

    ListFixture() {
        (*list).set_requested_bounds({0.0, 0.0, list_width, list_height});
        (*list).items = numbered(20U);
        window = std::make_unique<Window>(list, Size{list_width, list_height});
        settle();
    }

    void settle() {
        (*window).perform_layout();
        test_support::ClipTrackingPainter painter{};
        (*window).paint(painter, {0.0, 0.0, list_width, list_height});
        static_cast<void>((*window).take_damage());
    }

    [[nodiscard]] DamageRegion damage_after_layout() {
        (*window).perform_layout();
        DamageRegion damage = (*window).take_damage();
        settle();
        return damage;
    }
};

void test_list_damage_is_limited_to_shown_rows() {
    ListFixture fixture;
    ListBox& list = *fixture.list;
    require(list.top_index.read() == 0U, "the list starts at the top");

    list.items.replace(1U, "Replaced");
    DamageRegion damage = fixture.damage_after_layout();
    require(!damage.empty() && same_rect(damage.bounds(), {2.0, 28.0, list_width - 4.0, 26.0}),
            "a shown replaced row repaints only that row");

    list.items.replace(10U, "Hidden");
    damage = fixture.damage_after_layout();
    require(damage.empty(), "a replacement below the shown rows repaints nothing");

    list.items.insert(15U, "Below");
    list.items.erase(12U, 2U);
    damage = fixture.damage_after_layout();
    require(damage.empty(), "structural edits below the shown rows repaint nothing");

    list.items.append("Appended");
    list.items.append(std::vector<std::string>{"More", "Still more"});
    damage = fixture.damage_after_layout();
    require(damage.empty() && list.items.read().size() == 22U && list.items[21U] == "Still more",
            "appending below the shown rows repaints nothing");

    list.items.erase(2U, 1U);
    damage = fixture.damage_after_layout();
    require(!damage.empty() && same_rect(damage.bounds(), {2.0, 54.0, list_width - 4.0, list_height - 56.0}),
            "an erasure in the shown rows repaints from its row down");

    // One flush coalesces: row 1's bit and the watermark at row 2.
    list.items.replace(1U, "Again");
    list.items.replace(3U, "Covered");
    list.items.insert(2U, "Inserted");
    damage = fixture.damage_after_layout();
    require(!damage.empty() && same_rect(damage.bounds(), {2.0, 28.0, list_width - 4.0, list_height - 30.0}),
            "edits between two flushes coalesce into one damage");
    require(list.items[2U] == "Inserted" && list.items[4U] == "Covered", "the edits are stored in order");
}

void test_refused_edits_change_nothing() {
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("collection.refused"));
    (*list).items = {"Alpha", "Beta"};
    bool refused = false;
    try {
        (*list).items.insert(1U, std::string("\xFF"));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*list).items.read().size() == 2U, "an invalid item is refused before the edit");
    refused = false;
    try {
        (*list).items.erase(1U, 2U);
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused && (*list).items.read().size() == 2U, "an erasure past the end is refused");
    refused = false;
    try {
        static_cast<void>((*list).items[2U]);
    } catch (const std::out_of_range&) {
        refused = true;
    }
    require(refused, "an element read past the end is refused");
}

void test_selection_and_identities_follow_edits() {
    std::shared_ptr<ListBox> list = make_control<ListBox>(StableId("collection.selection"));
    (*list).items = numbered(8U);
    (*list).set_item_stable_ids({"a", "b", "c", "d", "e", "f", "g", "h"});
    (*list).select_index(5U);
    std::size_t changes{};
    SubscriptionToken changed = (*list).selection_changed().subscribe(
        test_support::IncrementCounter<std::size_t, const ListSelectionChange&>(changes));

    (*list).items.erase(1U, 1U);
    require((*list).selected_index() == std::optional<std::size_t>{4U} && changes == 1U,
            "an erasure above the selection moves it");
    require((*list).item_stable_id(4U) == "f" && (*list).item_stable_id(0U) == "a",
            "an erasure keeps the remaining identities aligned");
    (*list).items.replace(4U, "Renamed");
    require((*list).selected_index() == std::optional<std::size_t>{4U} && changes == 1U &&
                (*list).item_stable_id(4U) == "f",
            "a replacement keeps the selection and the identity");
    (*list).items.insert(0U, "First");
    require((*list).selected_index() == std::optional<std::size_t>{5U} && changes == 2U,
            "an insertion above the selection moves it");
    (*list).items.append("Last");
    require((*list).selected_index() == std::optional<std::size_t>{5U} && changes == 2U,
            "an append keeps the selection");
    (*list).items.erase(5U, 1U);
    require(!(*list).selected_index() && changes == 3U, "erasing the selected item clears the selection");
    (*list).items.clear();
    require((*list).items.read().empty() && (*list).top_index.read() == 0U, "clear empties the list");
    require(changed.connected(), "the subscription stays connected");
}

void test_checked_list_states_follow_edits() {
    std::shared_ptr<CheckedListBox> list = make_control<CheckedListBox>(StableId("collection.checked"));
    (*list).add_item("Alpha", CheckState::checked);
    (*list).add_item("Beta", CheckState::unchecked);
    (*list).add_item("Gamma", CheckState::indeterminate);
    (*list).items.insert(1U, "Inserted");
    require((*list).item_check_state(0U) == CheckState::checked &&
                (*list).item_check_state(1U) == CheckState::unchecked &&
                (*list).item_check_state(3U) == CheckState::indeterminate,
            "an insertion adds an unchecked state at its index");
    (*list).items.replace(3U, "Renamed");
    require((*list).item_check_state(3U) == CheckState::indeterminate, "a replacement keeps the state");
    (*list).items.erase(0U, 2U);
    require((*list).items.read().size() == 2U && (*list).item_check_state(1U) == CheckState::indeterminate,
            "an erasure removes the states with the items");
    (*list).items.append(std::vector<std::string>{"Delta", "Epsilon"});
    require((*list).items.read().size() == 4U && (*list).item_check_state(1U) == CheckState::indeterminate &&
                (*list).item_check_state(3U) == CheckState::unchecked,
            "an appended range adds unchecked states at the end");
    (*list).items.append(std::vector<std::string>{});
    require((*list).items.read().size() == 4U, "an empty append changes nothing");
    (*list).items = {"One", "Two", "Three"};
    require((*list).checked_indices().empty() && (*list).item_check_state(2U) == CheckState::unchecked,
            "an assignment resets every state");
}

// A grid of 30 rows shows a few; its edits repaint only the shown rows they
// reach. The cells' viewport is not public, so the damage is compared with
// the row height and against the shown rows' order.
struct GridFixture final {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("collection.grid"));
    std::unique_ptr<Window> window;

    GridFixture() {
        (*grid).set_requested_bounds({0.0, 0.0, 320.0, 160.0});
        (*grid).set_columns({{.name = "name", .header_text = "Name", .width = 120.0}});
        (*grid).allow_user_to_add_rows = false;
        std::vector<DataGridViewRow> rows;
        rows.reserve(30U);
        for (std::size_t index = 0U; index < 30U; ++index) {
            rows.push_back({.cells = {BindingValue{"Row " + std::to_string(index)}}});
        }
        (*grid).rows = std::move(rows);
        window = std::make_unique<Window>(grid, Size{320.0, 160.0});
        settle();
    }

    void settle() {
        (*window).perform_layout();
        test_support::ClipTrackingPainter painter{};
        (*window).paint(painter, {0.0, 0.0, 320.0, 160.0});
        static_cast<void>((*window).take_damage());
    }

    [[nodiscard]] DamageRegion damage_after_layout() {
        (*window).perform_layout();
        DamageRegion damage = (*window).take_damage();
        settle();
        return damage;
    }
};

void test_grid_damage_is_limited_to_shown_rows() {
    GridFixture fixture;
    DataGridView& grid = *fixture.grid;
    const double row_height = grid.row_height.read();

    grid.rows.replace(1U, {.stable_id = grid.rows[1U].stable_id, .cells = {BindingValue{std::string("One")}}});
    const DamageRegion first = fixture.damage_after_layout();
    require(!first.empty() && first.bounds().height <= row_height + 0.5 && first.bounds().width > 120.0,
            "a shown replaced row repaints only that row, row header included");
    grid.rows.replace(2U, {.cells = {BindingValue{std::string("Two")}}});
    const DamageRegion second = fixture.damage_after_layout();
    require(!second.empty() && second.bounds().y >= first.bounds().y + row_height - 0.5,
            "a replacement keeps the row's ID and repaints the row below the first");
    require(grid.rows[2U].stable_id == grid.row_id(DataGridViewRowPosition{2U}), "the replaced row keeps its ID");
    require(grid.set_cell_value(DataGridViewRowPosition{3U}, 0U, BindingValue{std::string("Three")}),
            "an unbound cell takes a value");
    const DamageRegion written = fixture.damage_after_layout();
    require(!written.empty() && written.bounds().height <= row_height + 0.5 &&
                written.bounds().y >= second.bounds().y + row_height - 0.5,
            "a cell value written into an unbound row repaints only that row");

    grid.rows.replace(25U, {.cells = {BindingValue{std::string("Hidden")}}});
    require(fixture.damage_after_layout().empty(), "a replacement below the shown rows repaints nothing");
    grid.rows.erase(27U, 2U);
    grid.rows.append({.cells = {BindingValue{std::string("Last")}}});
    // The scroll extent changes: the vertical bar, right of the cells,
    // repaints itself; no shown row does.
    const DamageRegion extent = fixture.damage_after_layout();
    const double cells_right = first.bounds().x + first.bounds().width;
    require(extent.empty() || extent.bounds().x >= cells_right - 0.5,
            "structural edits below the shown rows repaint only the scroll bar");

    grid.rows.erase(1U, 1U);
    const DamageRegion erased = fixture.damage_after_layout();
    DamageRegion cells;
    for (const Rect rect : erased.rectangles()) {
        if (rect.x < cells_right - 0.5) cells.add(rect);
    }
    require(!cells.empty() && cells.bounds().y == first.bounds().y && cells.bounds().height > 2.0 * row_height,
            "an erasure in the shown rows repaints from its row to the cells' bottom");
}

// The cell region (study decision 10): a value written into a bound-free
// cell lays nothing out and repaints that cell alone; a scroll before the
// flush folds the pending cell into its redraw of the whole grid.
void test_grid_cell_region() {
    GridFixture fixture;
    DataGridView& grid = *fixture.grid;
    const double row_height = grid.row_height.read();
    (*fixture.window).reset_activity_metrics();
    require(grid.set_cell_value(DataGridViewRowPosition{2U}, 0U, BindingValue{std::string("Changed")}),
            "the cell takes a value");
    (*fixture.window).perform_layout();
    const MetricsSnapshot metrics = (*fixture.window).metrics_snapshot();
    const DamageRegion cell = fixture.damage_after_layout();
    require(metrics.controls_arranged == 0U, "a cell write lays nothing out");
    require(!cell.empty() && cell.bounds().height <= row_height + 0.5 && cell.bounds().width <= 320.0,
            "a cell write repaints that cell's row at most");

    require(grid.set_cell_value(DataGridViewRowPosition{3U}, 0U, BindingValue{std::string("Again")}),
            "another cell takes a value");
    grid.set_first_displayed_row(DataGridViewRowPosition{5U});
    const DamageRegion scrolled = fixture.damage_after_layout();
    require(scrolled.bounds().height > 4.0 * row_height, "a scroll redraws the whole grid, the pending cell folded");
}

void test_grid_rows_refuse_and_keep_identity() {
    std::shared_ptr<DataGridView> grid = make_control<DataGridView>(StableId("collection.grid.ids"));
    (*grid).set_columns({{.name = "name", .header_text = "Name", .width = 120.0}});
    (*grid).rows = {{.stable_id = {"alpha"}}, {.stable_id = {"bravo"}}};
    bool refused = false;
    try {
        (*grid).rows.append({.stable_id = {"alpha"}});
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*grid).rows.read().size() == 2U, "a duplicate ID is refused before the edit");
    (*grid).rows.append(std::vector<DataGridViewRow>{{}, {.stable_id = {"charlie"}}, {}});
    require((*grid).rows.read().size() == 5U && (*grid).row_position(DataGridViewRowId{"charlie"}) ==
                std::optional<DataGridViewRowPosition>{DataGridViewRowPosition{3U}},
            "an appended range is indexed at its positions");
    require(!(*grid).rows[2U].stable_id.value.empty() && (*grid).rows[2U].stable_id != (*grid).rows[4U].stable_id,
            "rows appended without IDs get distinct generated ones");
    (*grid).rows.insert(1U, std::vector<DataGridViewRow>{{.stable_id = {"delta"}}, {.stable_id = {"echo"}}});
    require((*grid).rows.read().size() == 7U && (*grid).rows[1U].stable_id.value == "delta" &&
                (*grid).rows[2U].stable_id.value == "echo" &&
                (*grid).row_position(DataGridViewRowId{"charlie"}) ==
                    std::optional<DataGridViewRowPosition>{DataGridViewRowPosition{5U}},
            "an inserted range takes its positions and moves the later rows");
    bool range_refused = false;
    try {
        (*grid).rows.insert(0U, std::vector<DataGridViewRow>{{.stable_id = {"foxtrot"}}, {.stable_id = {"delta"}}});
    } catch (const std::invalid_argument&) {
        range_refused = true;
    }
    require(range_refused && (*grid).rows.read().size() == 7U && !(*grid).row_position(DataGridViewRowId{"foxtrot"}),
            "a range with a present ID is refused whole");
    (*grid).rows.erase(1U, 2U);
    (*grid).rows.replace(0U, {.stable_id = {"zulu"}});
    require(!(*grid).row_position(DataGridViewRowId{"alpha"}) &&
                (*grid).row_position(DataGridViewRowId{"zulu"}) ==
                    std::optional<DataGridViewRowPosition>{DataGridViewRowPosition{0U}},
            "a replacement naming another ID rekeys the row");
    (*grid).rows.erase(0U, 2U);
    require((*grid).row_position(DataGridViewRowId{"charlie"}) ==
                std::optional<DataGridViewRowPosition>{DataGridViewRowPosition{1U}},
            "an erasure moves the later rows' positions");
}

} // namespace

// A ComboBox's selection follows its item through edits, raising the
// selection, text and item changes in that order; a DomainUpDown kept
// sorted sorts after each edit and re-matches its selection to the text.
void test_choice_items_follow_edits() {
    std::shared_ptr<ComboBox> combo = make_control<ComboBox>(StableId("collection.combo"));
    (*combo).items = {"Alpha", "Bravo", "Charlie"};
    (*combo).selected_index = 1U;
    std::vector<std::string_view> raised;
    SubscriptionToken index_changed = (*combo).selected_index_changed().subscribe(
        test_support::PushConstant<std::vector<std::string_view>, std::string_view, std::optional<std::size_t>>(
            raised, "index"));
    SubscriptionToken text_changed = (*combo).text_changed().subscribe(
        test_support::PushConstant<std::vector<std::string_view>, std::string_view, const std::string&>(raised,
                                                                                                       "text"));
    SubscriptionToken items_changed = (*combo).items_changed().subscribe(
        test_support::PushConstant<std::vector<std::string_view>, std::string_view>(raised, "items"));
    (*combo).items.insert(0U, "Zero");
    require((*combo).selected_index.read() == std::optional<std::size_t>{2U} && (*combo).selected_text() == "Bravo" &&
                raised == std::vector<std::string_view>{"index", "items"},
            "an insertion before the selection moves it to its item");
    raised.clear();
    (*combo).items.replace(2U, "Beta");
    require((*combo).selected_index.read() == std::optional<std::size_t>{2U} &&
                raised == std::vector<std::string_view>{"text", "items"},
            "replacing the selected item keeps the index and changes the text");
    raised.clear();
    (*combo).items.erase(2U, 1U);
    require(!(*combo).selected_index.read() && raised == std::vector<std::string_view>{"index", "text", "items"},
            "erasing the selected item clears the selection");
    raised.clear();
    bool refused = false;
    try {
        (*combo).items.append(std::string("\xff"));
    } catch (const std::invalid_argument&) {
        refused = true;
    }
    require(refused && (*combo).items.read().size() == 3U && raised.empty(),
            "an item that is not UTF-8 is refused before the edit");

    std::shared_ptr<DomainUpDown> domain = make_control<DomainUpDown>(StableId("collection.domain"));
    (*domain).sorted = true;
    (*domain).items = {"lsb", "USB", "AM"};
    (*domain).text = "usb";
    require((*domain).selected_index.read() == std::optional<std::size_t>{2U}, "the text selects its item");
    (*domain).items.insert(0U, "cw");
    require((*domain).items[1U] == "cw" && (*domain).items[3U] == "USB" &&
                (*domain).selected_index.read() == std::optional<std::size_t>{3U},
            "a sorted domain sorts an insertion and re-matches the selection");
    (*domain).items.erase(3U, 1U);
    require(!(*domain).selected_index.read() && (*domain).text.read() == "usb",
            "erasing the selected item keeps the text and clears the selection");
}

int main() {
    test_edit_remap();
    test_dirty_coalescing();
    test_list_damage_is_limited_to_shown_rows();
    test_refused_edits_change_nothing();
    test_selection_and_identities_follow_edits();
    test_checked_list_states_follow_edits();
    test_grid_damage_is_limited_to_shown_rows();
    test_grid_rows_refuse_and_keep_identity();
    test_grid_cell_region();
    test_choice_items_follow_edits();
    return EXIT_SUCCESS;
}
