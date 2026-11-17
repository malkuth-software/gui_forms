// GF_TABLE_LIST_VIEW: a list view's columns, items, groups, selection, focus
// and sort glyph, over the C++ ListView. See gf_abi.h.

#include "abi_control_entry.hpp"
#include "abi_records.hpp"
#include "abi_tables.hpp"

#include "gui_forms/controls/list_view.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gui_forms::abi1 {
namespace {

constexpr SlotIdentity column_count_slot{GF_TABLE_LIST_VIEW, 0U};
constexpr SlotIdentity column_at_slot{GF_TABLE_LIST_VIEW, 1U};
constexpr SlotIdentity set_columns_slot{GF_TABLE_LIST_VIEW, 2U};
constexpr SlotIdentity set_column_width_slot{GF_TABLE_LIST_VIEW, 3U};
constexpr SlotIdentity item_count_slot{GF_TABLE_LIST_VIEW, 4U};
constexpr SlotIdentity item_at_slot{GF_TABLE_LIST_VIEW, 5U};
constexpr SlotIdentity item_sub_item_slot{GF_TABLE_LIST_VIEW, 6U};
constexpr SlotIdentity set_items_slot{GF_TABLE_LIST_VIEW, 7U};
constexpr SlotIdentity index_of_slot{GF_TABLE_LIST_VIEW, 8U};
constexpr SlotIdentity set_item_checked_slot{GF_TABLE_LIST_VIEW, 9U};
constexpr SlotIdentity group_count_slot{GF_TABLE_LIST_VIEW, 10U};
constexpr SlotIdentity group_at_slot{GF_TABLE_LIST_VIEW, 11U};
constexpr SlotIdentity set_groups_slot{GF_TABLE_LIST_VIEW, 12U};
constexpr SlotIdentity selected_items_slot{GF_TABLE_LIST_VIEW, 13U};
constexpr SlotIdentity set_selected_slot{GF_TABLE_LIST_VIEW, 14U};
constexpr SlotIdentity select_all_slot{GF_TABLE_LIST_VIEW, 15U};
constexpr SlotIdentity clear_selection_slot{GF_TABLE_LIST_VIEW, 16U};
constexpr SlotIdentity focused_item_slot{GF_TABLE_LIST_VIEW, 17U};
constexpr SlotIdentity set_focused_item_slot{GF_TABLE_LIST_VIEW, 18U};
constexpr SlotIdentity ensure_visible_slot{GF_TABLE_LIST_VIEW, 19U};
constexpr SlotIdentity set_sort_indicator_slot{GF_TABLE_LIST_VIEW, 20U};
constexpr SlotIdentity sort_items_slot{GF_TABLE_LIST_VIEW, 21U};

constexpr const char* not_list_view = "the control is not a list view";

// The 1.5 sizes of the records this table reads and writes.
constexpr std::uint32_t column_size_1_5 = 48U;
constexpr std::uint32_t item_size_1_5 = 88U;
constexpr std::uint32_t group_size_1_5 = 40U;
static_assert(sizeof(gf_list_view_column) == column_size_1_5);
static_assert(sizeof(gf_list_view_item) == item_size_1_5);
static_assert(sizeof(gf_list_view_group) == group_size_1_5);

// The C limits are the C++ view's.
static_assert(GF_LIST_VIEW_MAXIMUM_COLUMNS == ListView::maximum_columns);
static_assert(GF_LIST_VIEW_MAXIMUM_ITEMS == ListView::maximum_items);
static_assert(GF_LIST_VIEW_MAXIMUM_GROUPS == ListView::maximum_groups);

// gf_list_view_alignment and gf_list_view_sort_order number their choices
// as ListViewAlignment and ListViewSortOrder do.
static_assert(GF_LIST_VIEW_ALIGNMENT_RIGHT == static_cast<std::uint32_t>(ListViewAlignment::right));
static_assert(GF_LIST_VIEW_SORT_ORDER_DESCENDING == static_cast<std::uint32_t>(ListViewSortOrder::descending));

// A column's width when the caller's record gives zero (ListViewColumn's).
constexpr double default_column_width = 120.0;

using ListViewPtr = std::shared_ptr<ListView>;

// -- records -----------------------------------------------------------------------

// The column a caller's record describes, its fields checked before the
// view is touched (the view checks the identity and width again).
gf_status read_column(const gf_list_view_column& record, SlotIdentity slot, ListViewColumn& column) {
    if (record.alignment > GF_LIST_VIEW_ALIGNMENT_RIGHT || !std::isfinite(record.width)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a column has a known alignment and a finite width");
    }
    std::string_view stable_id;
    std::string_view text;
    gf_status read = read_text(record.stable_id, slot, stable_id);
    if (read == GF_STATUS_OK) read = read_text(record.text, slot, text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    ListViewColumn made{};
    made.stable_id = std::string(stable_id);
    made.text = std::string(text);
    made.width = record.width == 0.0 ? default_column_width : record.width;
    made.alignment = static_cast<ListViewAlignment>(record.alignment);
    column = std::move(made);
    return GF_STATUS_OK;
}

// The item a caller's record describes; its texts are copied.
gf_status read_item(const gf_list_view_item& record, SlotIdentity slot, ListViewItem& item) {
    if (record.checked > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an item's checked is 0 or 1");
    }
    if (record.sub_item_count > GF_LIST_VIEW_MAXIMUM_SUB_ITEMS) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "an item has at most 64 sub-items");
    }
    if (record.sub_items == nullptr && record.sub_item_count != 0U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "an item with sub-items needs its sub_items array");
    }
    std::string_view stable_id;
    std::string_view text;
    std::string_view image_key;
    std::string_view group;
    gf_status read = read_text(record.stable_id, slot, stable_id);
    if (read == GF_STATUS_OK) read = read_text(record.text, slot, text);
    if (read == GF_STATUS_OK) read = read_text(record.image_key, slot, image_key);
    if (read == GF_STATUS_OK) read = read_text(record.group, slot, group);
    if (read != GF_STATUS_OK) {
        return read;
    }
    ListViewItem made{};
    made.stable_id = std::string(stable_id);
    made.text = std::string(text);
    made.image_key = std::string(image_key);
    made.group = std::string(group);
    made.checked = record.checked == 1U;
    made.sub_items.reserve(static_cast<std::size_t>(record.sub_item_count));
    for (std::uint64_t index = 0U; index < record.sub_item_count; ++index) {
        std::string_view sub_item;
        const gf_status sub_item_read = read_text(record.sub_items[index], slot, sub_item);
        if (sub_item_read != GF_STATUS_OK) {
            return sub_item_read;
        }
        made.sub_items.emplace_back(sub_item);
    }
    item = std::move(made);
    return GF_STATUS_OK;
}

gf_status read_group(const gf_list_view_group& record, SlotIdentity slot, ListViewGroup& group) {
    if (record.header_alignment > GF_LIST_VIEW_ALIGNMENT_RIGHT) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "a group's header alignment is a known alignment");
    }
    std::string_view stable_id;
    std::string_view header;
    gf_status read = read_text(record.stable_id, slot, stable_id);
    if (read == GF_STATUS_OK) read = read_text(record.header, slot, header);
    if (read != GF_STATUS_OK) {
        return read;
    }
    ListViewGroup made{};
    made.stable_id = std::string(stable_id);
    made.header = std::string(header);
    made.header_alignment = static_cast<ListViewAlignment>(record.header_alignment);
    group = std::move(made);
    return GF_STATUS_OK;
}

// count records of a caller's array (stride from the first element's
// struct_size, at least minimum), each read by read_one into `read`, whole
// before the view changes. Every caller bounds count (at most
// GF_LIST_VIEW_MAXIMUM_ITEMS), so the offsets cannot overflow.
template <typename Record, typename Element>
gf_status read_array(const Record* records, std::uint64_t count, std::uint64_t maximum, std::uint32_t minimum,
                     SlotIdentity slot, gf_status (*read_one)(const Record&, SlotIdentity, Element&),
                     std::vector<Element>& read) {
    if (count > maximum) {
        return fail(GF_STATUS_LIMIT_EXCEEDED, slot, "the array holds more records than the view does");
    }
    std::uint32_t stride = 0U;
    const gf_status checked = check_array(records, count, minimum, slot, stride);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    std::vector<Element> made;
    made.reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0U; index < count; ++index) {
        Element element;
        const gf_status converted = read_one(element_at(records, stride, index), slot, element);
        if (converted != GF_STATUS_OK) {
            return converted;
        }
        made.push_back(std::move(element));
    }
    read = std::move(made);
    return GF_STATUS_OK;
}

// Whether a caller's output record and text buffer are usable: the record
// of at least its 1.5 size, a size output, and a buffer unless the capacity
// is zero.
template <typename Record>
gf_status check_output(const Record* record, std::uint32_t minimum, const char* text_buffer,
                       std::uint64_t text_capacity, const std::uint64_t* text_required, SlotIdentity slot) {
    if (record == nullptr || (*record).struct_size < minimum || text_required == nullptr ||
        (text_buffer == nullptr && text_capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot,
                    "an output record of at least the 1.5 size, a text size output, and a buffer unless its "
                    "capacity is zero are needed");
    }
    return GF_STATUS_OK;
}

// Copies count texts into text_buffer one after another and points views
// there (an empty text keeps a null view). text_required receives their
// total size; a short buffer gives GF_STATUS_BUFFER_TOO_SMALL, writing no
// text and no view.
gf_status copy_texts(const std::string_view* texts, gf_utf8_view* views, std::size_t count, char* text_buffer,
                     std::uint64_t text_capacity, std::uint64_t* text_required, SlotIdentity slot) {
    std::uint64_t total = 0U;
    for (std::size_t text = 0U; text < count; ++text) total += texts[text].size();
    *text_required = total;
    if (text_capacity < total) {
        return fail(GF_STATUS_BUFFER_TOO_SMALL, slot, "the text buffer is smaller than the record's text");
    }
    std::uint64_t offset = 0U;
    for (std::size_t text = 0U; text < count; ++text) {
        views[text] = gf_utf8_view{};
        if (!texts[text].empty()) {
            std::memcpy(text_buffer + offset, texts[text].data(), texts[text].size());
            views[text].data = text_buffer + offset;
            views[text].size = texts[text].size();
        }
        offset += texts[text].size();
    }
    return GF_STATUS_OK;
}

// -- the view ---------------------------------------------------------------------

gf_status require_column(const ListView& view, std::uint64_t column, SlotIdentity slot) {
    if (column >= view.columns().size()) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the column is past the view's columns");
    }
    return GF_STATUS_OK;
}

gf_status require_item(const ListView& view, std::uint64_t index, SlotIdentity slot) {
    if (index >= view.item_count()) {
        return fail(GF_STATUS_NOT_FOUND, slot, "the item is past the view's items");
    }
    return GF_STATUS_OK;
}

// The view's own items are what the item records read and write; a virtual
// view's belong to its source.
gf_status require_ordinary(const ListView& view, SlotIdentity slot) {
    if (view.virtual_mode()) {
        return fail(GF_STATUS_INVALID_STATE, slot, "a virtual list view's items belong to its source");
    }
    return GF_STATUS_OK;
}

gf_status check_sort_order(gf_list_view_sort_order order, SlotIdentity slot) {
    if (order > GF_LIST_VIEW_SORT_ORDER_DESCENDING) {
        return fail(GF_STATUS_INVALID_ARGUMENT, slot, "the sort order is NONE, ASCENDING or DESCENDING");
    }
    return GF_STATUS_OK;
}

// -- columns -----------------------------------------------------------------------

gf_status list_view_column_count_body(gf_object view, std::uint64_t* count) {
    if (count == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, column_count_slot, "column_count needs a count output");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, column_count_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    *count = (*typed).columns().size();
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_column_count(gf_object view, std::uint64_t* count) noexcept {
    return call_slot(column_count_slot, list_view_column_count_body, view, count);
}

gf_status list_view_column_at_body(gf_object view, std::uint64_t index, gf_list_view_column* column,
                                   char* text_buffer, std::uint64_t text_capacity,
                                   std::uint64_t* text_required) {
    const gf_status usable =
        check_output(column, column_size_1_5, text_buffer, text_capacity, text_required, column_at_slot);
    if (usable != GF_STATUS_OK) {
        return usable;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, column_at_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status present = require_column(*typed, index, column_at_slot);
    if (present != GF_STATUS_OK) {
        return present;
    }
    const ListViewColumn& held = (*typed).columns()[static_cast<std::size_t>(index)];
    const std::string_view texts[2] = {held.stable_id, held.text};
    gf_utf8_view views[2] = {};
    const gf_status copied = copy_texts(texts, views, 2U, text_buffer, text_capacity, text_required,
                                        column_at_slot);
    if (copied != GF_STATUS_OK) {
        return copied;
    }
    gf_list_view_column complete{};
    complete.struct_size = column_size_1_5;
    complete.alignment = static_cast<gf_list_view_alignment>(held.alignment);
    complete.width = held.width;
    complete.stable_id = views[0];
    complete.text = views[1];
    write_record(complete, column);
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_column_at(gf_object view, std::uint64_t index, gf_list_view_column* column,
                                      char* text_buffer, std::uint64_t text_capacity,
                                      std::uint64_t* text_required) noexcept {
    return call_slot(column_at_slot, list_view_column_at_body, view, index, column, text_buffer, text_capacity, text_required);
}

gf_status list_view_set_columns_body(gf_object view, const gf_list_view_column* columns,
                                     std::uint64_t count) {
    std::vector<ListViewColumn> read;
    const gf_status converted = read_array(columns, count, GF_LIST_VIEW_MAXIMUM_COLUMNS, column_size_1_5,
                                           set_columns_slot, &read_column, read);
    if (converted != GF_STATUS_OK) {
        return converted;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, set_columns_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*typed).set_columns(std::move(read));
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_set_columns(gf_object view, const gf_list_view_column* columns,
                                        std::uint64_t count) noexcept {
    return call_slot(set_columns_slot, list_view_set_columns_body, view, columns, count);
}

gf_status list_view_set_column_width_body(gf_object view, std::uint64_t column, double width) {
    if (!std::isfinite(width)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_column_width_slot, "a column's width is finite");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, set_column_width_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status present = require_column(*typed, column, set_column_width_slot);
    if (present != GF_STATUS_OK) {
        return present;
    }
    (*typed).set_column_width(static_cast<std::size_t>(column), width);
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_set_column_width(gf_object view, std::uint64_t column, double width) noexcept {
    return call_slot(set_column_width_slot, list_view_set_column_width_body, view, column, width);
}

// -- items -------------------------------------------------------------------------

gf_status list_view_item_count_body(gf_object view, std::uint64_t* count) {
    if (count == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, item_count_slot, "item_count needs a count output");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, item_count_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    *count = (*typed).item_count();
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_item_count(gf_object view, std::uint64_t* count) noexcept {
    return call_slot(item_count_slot, list_view_item_count_body, view, count);
}

gf_status list_view_item_at_body(gf_object view, std::uint64_t index, gf_list_view_item* item, char* text_buffer,
                                 std::uint64_t text_capacity, std::uint64_t* text_required) {
    const gf_status usable =
        check_output(item, item_size_1_5, text_buffer, text_capacity, text_required, item_at_slot);
    if (usable != GF_STATUS_OK) {
        return usable;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, item_at_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    gf_status checked = require_ordinary(*typed, item_at_slot);
    if (checked == GF_STATUS_OK) checked = require_item(*typed, index, item_at_slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    // items is declared after the view it borrows from, and used
    // before this slot returns.
    const BorrowedSpan<ListViewItem> items = (*typed).items();
    const ListViewItem& held = items[static_cast<std::size_t>(index)];
    const std::string_view texts[4] = {held.stable_id, held.text, held.image_key, held.group};
    gf_utf8_view views[4] = {};
    const gf_status copied = copy_texts(texts, views, 4U, text_buffer, text_capacity, text_required,
                                        item_at_slot);
    if (copied != GF_STATUS_OK) {
        return copied;
    }
    gf_list_view_item complete{};
    complete.struct_size = item_size_1_5;
    complete.checked = held.checked ? 1U : 0U;
    complete.stable_id = views[0];
    complete.text = views[1];
    complete.image_key = views[2];
    complete.group = views[3];
    complete.sub_items = nullptr;
    complete.sub_item_count = held.sub_items.size();
    write_record(complete, item);
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_item_at(gf_object view, std::uint64_t index, gf_list_view_item* item, char* text_buffer,
                                    std::uint64_t text_capacity, std::uint64_t* text_required) noexcept {
    return call_slot(item_at_slot, list_view_item_at_body, view, index, item, text_buffer, text_capacity, text_required);
}

gf_status list_view_item_sub_item_body(gf_object view, std::uint64_t index, std::uint64_t sub_item, char* buffer,
                                       std::uint64_t capacity, std::uint64_t* required) {
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, item_sub_item_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    gf_status checked = require_ordinary(*typed, item_sub_item_slot);
    if (checked == GF_STATUS_OK) checked = require_item(*typed, index, item_sub_item_slot);
    if (checked != GF_STATUS_OK) {
        return checked;
    }
    const BorrowedSpan<ListViewItem> items = (*typed).items();
    const std::vector<std::string>& sub_items = items[static_cast<std::size_t>(index)].sub_items;
    if (sub_item >= sub_items.size()) {
        return fail(GF_STATUS_NOT_FOUND, item_sub_item_slot, "the sub-item is past the item's sub-items");
    }
    return write_text(sub_items[static_cast<std::size_t>(sub_item)], buffer, capacity, required,
                      item_sub_item_slot);
}

gf_status GF_CALL list_view_item_sub_item(gf_object view, std::uint64_t index, std::uint64_t sub_item, char* buffer,
                                          std::uint64_t capacity, std::uint64_t* required) noexcept {
    return call_slot(item_sub_item_slot, list_view_item_sub_item_body, view, index, sub_item, buffer, capacity, required);
}

gf_status list_view_set_items_body(gf_object view, const gf_list_view_item* items, std::uint64_t count) {
    std::vector<ListViewItem> read;
    const gf_status converted =
        read_array(items, count, GF_LIST_VIEW_MAXIMUM_ITEMS, item_size_1_5, set_items_slot, &read_item, read);
    if (converted != GF_STATUS_OK) {
        return converted;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, set_items_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status ordinary = require_ordinary(*typed, set_items_slot);
    if (ordinary != GF_STATUS_OK) {
        return ordinary;
    }
    (*typed).set_items(std::move(read));
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_set_items(gf_object view, const gf_list_view_item* items, std::uint64_t count) noexcept {
    return call_slot(set_items_slot, list_view_set_items_body, view, items, count);
}

gf_status list_view_index_of_body(gf_object view, gf_utf8_view item_id, std::uint64_t* index) {
    if (index == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, index_of_slot, "index_of needs an index output");
    }
    std::string_view text;
    const gf_status read = read_text(item_id, index_of_slot, text);
    if (read != GF_STATUS_OK) {
        return read;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, index_of_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const stxlib::Optional<std::size_t> found = (*typed).index_of(text);
    if (found.absent()) {
        return fail(GF_STATUS_NOT_FOUND, index_of_slot, "no item of the view has that stable id");
    }
    *index = *found.value();
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_index_of(gf_object view, gf_utf8_view item_id, std::uint64_t* index) noexcept {
    return call_slot(index_of_slot, list_view_index_of_body, view, item_id, index);
}

gf_status list_view_set_item_checked_body(gf_object view, std::uint64_t index, std::uint32_t checked) {
    if (checked > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_item_checked_slot, "checked is 0 or 1");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, set_item_checked_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    gf_status usable = require_ordinary(*typed, set_item_checked_slot);
    if (usable == GF_STATUS_OK) usable = require_item(*typed, index, set_item_checked_slot);
    if (usable != GF_STATUS_OK) {
        return usable;
    }
    (*typed).set_item_checked(static_cast<std::size_t>(index), checked == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_set_item_checked(gf_object view, std::uint64_t index, std::uint32_t checked) noexcept {
    return call_slot(set_item_checked_slot, list_view_set_item_checked_body, view, index, checked);
}

// -- groups ------------------------------------------------------------------------

gf_status list_view_group_count_body(gf_object view, std::uint64_t* count) {
    if (count == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, group_count_slot, "group_count needs a count output");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, group_count_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    *count = (*typed).groups().size();
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_group_count(gf_object view, std::uint64_t* count) noexcept {
    return call_slot(group_count_slot, list_view_group_count_body, view, count);
}

gf_status list_view_group_at_body(gf_object view, std::uint64_t index, gf_list_view_group* group,
                                  char* text_buffer, std::uint64_t text_capacity,
                                  std::uint64_t* text_required) {
    const gf_status usable =
        check_output(group, group_size_1_5, text_buffer, text_capacity, text_required, group_at_slot);
    if (usable != GF_STATUS_OK) {
        return usable;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, group_at_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    if (index >= (*typed).groups().size()) {
        return fail(GF_STATUS_NOT_FOUND, group_at_slot, "the group is past the view's groups");
    }
    const ListViewGroup& held = (*typed).groups()[static_cast<std::size_t>(index)];
    const std::string_view texts[2] = {held.stable_id, held.header};
    gf_utf8_view views[2] = {};
    const gf_status copied = copy_texts(texts, views, 2U, text_buffer, text_capacity, text_required,
                                        group_at_slot);
    if (copied != GF_STATUS_OK) {
        return copied;
    }
    gf_list_view_group complete{};
    complete.struct_size = group_size_1_5;
    complete.header_alignment = static_cast<gf_list_view_alignment>(held.header_alignment);
    complete.stable_id = views[0];
    complete.header = views[1];
    write_record(complete, group);
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_group_at(gf_object view, std::uint64_t index, gf_list_view_group* group,
                                     char* text_buffer, std::uint64_t text_capacity,
                                     std::uint64_t* text_required) noexcept {
    return call_slot(group_at_slot, list_view_group_at_body, view, index, group, text_buffer, text_capacity, text_required);
}

gf_status list_view_set_groups_body(gf_object view, const gf_list_view_group* groups,
                                    std::uint64_t count) {
    std::vector<ListViewGroup> read;
    const gf_status converted = read_array(groups, count, GF_LIST_VIEW_MAXIMUM_GROUPS, group_size_1_5,
                                           set_groups_slot, &read_group, read);
    if (converted != GF_STATUS_OK) {
        return converted;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, set_groups_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*typed).set_groups(std::move(read));
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_set_groups(gf_object view, const gf_list_view_group* groups,
                                       std::uint64_t count) noexcept {
    return call_slot(set_groups_slot, list_view_set_groups_body, view, groups, count);
}

// -- selection, focus and sorting -----------------------------------------------------

gf_status list_view_selected_items_body(gf_object view, std::uint64_t* indices, std::uint64_t capacity,
                                        std::uint64_t* count) {
    if (count == nullptr || (indices == nullptr && capacity != 0U)) {
        return fail(GF_STATUS_INVALID_ARGUMENT, selected_items_slot,
                    "selected_items needs a count output, and an array unless its capacity is zero");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, selected_items_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    // The view lends no view of its selection: it is copied once here,
    // at the boundary.
    const std::vector<std::size_t> held = (*typed).selected_indices();
    *count = held.size();
    if (capacity < held.size()) {
        return fail(GF_STATUS_BUFFER_TOO_SMALL, selected_items_slot,
                    "the array is smaller than the number of selected items");
    }
    for (std::size_t position = 0U; position < held.size(); ++position) indices[position] = held[position];
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_selected_items(gf_object view, std::uint64_t* indices, std::uint64_t capacity,
                                           std::uint64_t* count) noexcept {
    return call_slot(selected_items_slot, list_view_selected_items_body, view, indices, capacity, count);
}

gf_status list_view_set_selected_body(gf_object view, std::uint64_t index, std::uint32_t selected) {
    if (selected > 1U) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_selected_slot, "selected is 0 or 1");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, set_selected_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status present = require_item(*typed, index, set_selected_slot);
    if (present != GF_STATUS_OK) {
        return present;
    }
    (*typed).set_selected(static_cast<std::size_t>(index), selected == 1U);
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_set_selected(gf_object view, std::uint64_t index, std::uint32_t selected) noexcept {
    return call_slot(set_selected_slot, list_view_set_selected_body, view, index, selected);
}

gf_status list_view_select_all_body(gf_object view) {
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, select_all_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*typed).select_all();
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_select_all(gf_object view) noexcept {
    return call_slot(select_all_slot, list_view_select_all_body, view);
}

gf_status list_view_clear_selection_body(gf_object view) {
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, clear_selection_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    (*typed).clear_selection();
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_clear_selection(gf_object view) noexcept {
    return call_slot(clear_selection_slot, list_view_clear_selection_body, view);
}

gf_status list_view_focused_item_body(gf_object view, std::int64_t* index) {
    if (index == nullptr) {
        return fail(GF_STATUS_INVALID_ARGUMENT, focused_item_slot, "focused_item needs an index output");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, focused_item_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    // An index is below ListView::maximum_items, so it fits a signed
    // 64-bit value.
    const stxlib::Optional<std::size_t> focused = (*typed).focused_index();
    *index = focused.present() ? static_cast<std::int64_t>(*focused.value()) : std::int64_t{-1};
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_focused_item(gf_object view, std::int64_t* index) noexcept {
    return call_slot(focused_item_slot, list_view_focused_item_body, view, index);
}

gf_status list_view_set_focused_item_body(gf_object view, std::int64_t index) {
    if (index < -1) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_focused_item_slot, "the focused item is an index or -1");
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, set_focused_item_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    if (index == -1) {
        (*typed).set_focused_index(stxlib::Optional<std::size_t>::none());
        return GF_STATUS_OK;
    }
    const std::uint64_t position = static_cast<std::uint64_t>(index);
    const gf_status present = require_item(*typed, position, set_focused_item_slot);
    if (present != GF_STATUS_OK) {
        return present;
    }
    (*typed).set_focused_index(stxlib::Optional<std::size_t>::of(static_cast<std::size_t>(position)));
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_set_focused_item(gf_object view, std::int64_t index) noexcept {
    return call_slot(set_focused_item_slot, list_view_set_focused_item_body, view, index);
}

gf_status list_view_ensure_visible_body(gf_object view, std::uint64_t index) {
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, ensure_visible_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    const gf_status present = require_item(*typed, index, ensure_visible_slot);
    if (present != GF_STATUS_OK) {
        return present;
    }
    (*typed).ensure_visible(static_cast<std::size_t>(index));
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_ensure_visible(gf_object view, std::uint64_t index) noexcept {
    return call_slot(ensure_visible_slot, list_view_ensure_visible_body, view, index);
}

gf_status list_view_set_sort_indicator_body(gf_object view, std::int64_t column,
                                            gf_list_view_sort_order order) {
    if (column < -1) {
        return fail(GF_STATUS_INVALID_ARGUMENT, set_sort_indicator_slot, "the sorted column is an index or -1");
    }
    const gf_status known = check_sort_order(order, set_sort_indicator_slot);
    if (known != GF_STATUS_OK) {
        return known;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, set_sort_indicator_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    if (column == -1) {
        (*typed).set_sort_indicator(stxlib::Optional<std::size_t>::none(), ListViewSortOrder::none);
        return GF_STATUS_OK;
    }
    const std::uint64_t position = static_cast<std::uint64_t>(column);
    const gf_status present = require_column(*typed, position, set_sort_indicator_slot);
    if (present != GF_STATUS_OK) {
        return present;
    }
    (*typed).set_sort_indicator(stxlib::Optional<std::size_t>::of(static_cast<std::size_t>(position)), static_cast<ListViewSortOrder>(order));
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_set_sort_indicator(gf_object view, std::int64_t column,
                                               gf_list_view_sort_order order) noexcept {
    return call_slot(set_sort_indicator_slot, list_view_set_sort_indicator_body, view, column, order);
}

gf_status list_view_sort_items_body(gf_object view, std::uint64_t column, gf_list_view_sort_order order) {
    const gf_status known = check_sort_order(order, sort_items_slot);
    if (known != GF_STATUS_OK) {
        return known;
    }
    ListViewPtr typed;
    const gf_status entered = enter_typed_control(view, sort_items_slot, not_list_view, typed);
    if (entered != GF_STATUS_OK) {
        return entered;
    }
    gf_status usable = require_ordinary(*typed, sort_items_slot);
    if (usable == GF_STATUS_OK) usable = require_column(*typed, column, sort_items_slot);
    if (usable != GF_STATUS_OK) {
        return usable;
    }
    (*typed).sort_items(static_cast<std::size_t>(column), static_cast<ListViewSortOrder>(order));
    return GF_STATUS_OK;
}

gf_status GF_CALL list_view_sort_items(gf_object view, std::uint64_t column, gf_list_view_sort_order order) noexcept {
    return call_slot(sort_items_slot, list_view_sort_items_body, view, column, order);
}

constexpr gf_list_view_table table{
    .struct_size = sizeof(gf_list_view_table),
    .abi_minor = GF_ABI_MINOR,
    .column_count = &list_view_column_count,
    .column_at = &list_view_column_at,
    .set_columns = &list_view_set_columns,
    .set_column_width = &list_view_set_column_width,
    .item_count = &list_view_item_count,
    .item_at = &list_view_item_at,
    .item_sub_item = &list_view_item_sub_item,
    .set_items = &list_view_set_items,
    .index_of = &list_view_index_of,
    .set_item_checked = &list_view_set_item_checked,
    .group_count = &list_view_group_count,
    .group_at = &list_view_group_at,
    .set_groups = &list_view_set_groups,
    .selected_items = &list_view_selected_items,
    .set_selected = &list_view_set_selected,
    .select_all = &list_view_select_all,
    .clear_selection = &list_view_clear_selection,
    .focused_item = &list_view_focused_item,
    .set_focused_item = &list_view_set_focused_item,
    .ensure_visible = &list_view_ensure_visible,
    .set_sort_indicator = &list_view_set_sort_indicator,
    .sort_items = &list_view_sort_items,
};

} // namespace

const gf_list_view_table& list_view_table() noexcept {
    return table;
}

} // namespace gui_forms::abi1
