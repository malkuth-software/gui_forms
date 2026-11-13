/* Kit part: a list view's columns, items, groups, selection, focus and sort
 * glyph (GF_TABLE_LIST_VIEW, since 1.4), on a created window's root: the
 * calls a C or C# program fills and reads a view with, their refusals, the
 * settings it reaches as properties (view, activation, header_style) and
 * the events it subscribes to (selection_changed, item_activated,
 * column_click). */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_list_view_column column_record(const char* stable_id, const char* text, double width) {
    gf_list_view_column made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.stable_id = gf_kit_text(stable_id);
    made.text = gf_kit_text(text);
    made.width = width;
    return made;
}

static gf_list_view_item item_record(const char* stable_id, const char* text, const gf_utf8_view* sub_items,
                                     uint64_t sub_item_count) {
    gf_list_view_item made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.stable_id = gf_kit_text(stable_id);
    made.text = gf_kit_text(text);
    made.sub_items = sub_items;
    made.sub_item_count = sub_item_count;
    return made;
}

static gf_list_view_group group_record(const char* stable_id, const char* header) {
    gf_list_view_group made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.stable_id = gf_kit_text(stable_id);
    made.header = gf_kit_text(header);
    return made;
}

static gf_value enumeration_value(int64_t choice) {
    gf_value made;
    memset(&made, 0, sizeof(made));
    made.struct_size = (uint32_t)sizeof(made);
    made.kind = GF_VALUE_KIND_ENUMERATION;
    made.signed_integer = choice;
    return made;
}

/* Whether the enumerated property reads back as the choice named. */
static int choice_is(gf_kit* kit, gf_object view, const char* property, const char* expected) {
    gf_value read;
    memset(&read, 0, sizeof(read));
    read.struct_size = (uint32_t)sizeof(read);
    char text[32];
    uint64_t required = 0u;
    if ((*kit).properties.get(view, gf_kit_text(property), &read, text, sizeof(text), &required) != GF_STATUS_OK) {
        return 0;
    }
    return read.kind == GF_VALUE_KIND_ENUMERATION && gf_kit_view_equals(read.text, expected);
}

static void settings(gf_kit* kit, gf_object view) {
    const gf_value details = enumeration_value(0);
    const gf_value one_click = enumeration_value(1);
    const gf_value no_header = enumeration_value(2);
    const gf_value clickable = enumeration_value(0);
    const gf_value unknown = enumeration_value(9);
    gf_kit_status(kit, (*kit).properties.set(view, gf_kit_text("view"), &details), GF_STATUS_OK,
                  "set the details view");
    gf_kit_check(kit, choice_is(kit, view, "view", "details"), "the view reads back as details");
    gf_kit_status(kit, (*kit).properties.set(view, gf_kit_text("activation"), &one_click), GF_STATUS_OK,
                  "set one-click activation");
    gf_kit_check(kit, choice_is(kit, view, "activation", "one_click"), "the activation reads back as one_click");
    gf_kit_status(kit, (*kit).properties.set(view, gf_kit_text("header_style"), &no_header), GF_STATUS_OK,
                  "hide the column headers");
    gf_kit_check(kit, choice_is(kit, view, "header_style", "none"), "the header style reads back as none");
    gf_kit_status(kit, (*kit).properties.set(view, gf_kit_text("header_style"), &clickable), GF_STATUS_OK,
                  "show clickable column headers");
    gf_kit_status(kit, (*kit).properties.set(view, gf_kit_text("view"), &unknown), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown view is refused");
}

static void columns(gf_kit* kit, gf_object view) {
    gf_list_view_column records[2];
    records[0] = column_record("name", "Name", 0.0);
    records[1] = column_record("size", "Size", 80.0);
    records[1].alignment = GF_LIST_VIEW_ALIGNMENT_RIGHT;
    gf_kit_status(kit, (*kit).list_view.set_columns(view, records, 2u), GF_STATUS_OK, "set two columns");
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).list_view.column_count(view, &count), GF_STATUS_OK, "count the columns");
    gf_kit_check(kit, count == 2u, "two columns");

    gf_list_view_column read;
    memset(&read, 0, sizeof(read));
    read.struct_size = (uint32_t)sizeof(read);
    char text[32];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).list_view.column_at(view, 1u, &read, text, 2u, &required),
                  GF_STATUS_BUFFER_TOO_SMALL, "a short buffer is refused");
    gf_kit_check(kit, required == 8u && read.stable_id.size == 0u, "the size is given and the record unwritten");
    gf_kit_status(kit, (*kit).list_view.column_at(view, 1u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read a column");
    gf_kit_check(kit, gf_kit_view_equals(read.stable_id, "size") && gf_kit_view_equals(read.text, "Size") &&
                          read.width == 80.0 && read.alignment == GF_LIST_VIEW_ALIGNMENT_RIGHT,
                 "the column reads back as set");
    gf_kit_status(kit, (*kit).list_view.column_at(view, 0u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the first column");
    gf_kit_check(kit, read.width == 120.0, "a zero width takes the default");
    gf_kit_status(kit, (*kit).list_view.column_at(view, 2u, &read, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "a column past the columns is not found");

    gf_kit_status(kit, (*kit).list_view.set_column_width(view, 0u, 150.0), GF_STATUS_OK, "widen a column");
    gf_kit_status(kit, (*kit).list_view.column_at(view, 0u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the widened column");
    gf_kit_check(kit, read.width == 150.0, "the width is the one set");
    gf_kit_status(kit, (*kit).list_view.set_column_width(view, 5u, 150.0), GF_STATUS_NOT_FOUND,
                  "a width past the columns is refused");
    gf_kit_status(kit, (*kit).list_view.set_column_width(view, 0u, 1.0), GF_STATUS_INVALID_ARGUMENT,
                  "a width below the minimum is refused");

    gf_list_view_column repeated[2];
    repeated[0] = column_record("name", "Name", 0.0);
    repeated[1] = column_record("name", "Again", 0.0);
    gf_kit_status(kit, (*kit).list_view.set_columns(view, repeated, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "a repeated column id is refused");
    gf_list_view_column misaligned = column_record("odd", "Odd", 0.0);
    misaligned.alignment = 7u;
    gf_kit_status(kit, (*kit).list_view.set_columns(view, &misaligned, 1u), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown alignment is refused");
    gf_kit_status(kit, (*kit).list_view.set_columns(view, records, GF_LIST_VIEW_MAXIMUM_COLUMNS + 1u),
                  GF_STATUS_LIMIT_EXCEEDED, "more columns than a view holds are refused");
    gf_kit_status(kit, (*kit).list_view.column_count(view, &count), GF_STATUS_OK, "count the columns again");
    gf_kit_check(kit, count == 2u, "a refused replacement changes nothing");
}

static void items(gf_kit* kit, gf_object view) {
    const gf_utf8_view alpha_sizes[1] = {gf_kit_text("12 KB")};
    const gf_utf8_view bravo_sizes[1] = {gf_kit_text("3 KB")};
    gf_list_view_item records[3];
    records[0] = item_record("alpha", "Charlie notes", alpha_sizes, 1u);
    records[1] = item_record("bravo", "alpha report", bravo_sizes, 1u);
    records[2] = item_record("charlie", "Bravo draft", NULL, 0u);
    records[1].checked = 1u;
    records[2].group = gf_kit_text("drafts");
    gf_kit_status(kit, (*kit).list_view.set_items(view, records, 3u), GF_STATUS_OK, "set three items");
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).list_view.item_count(view, &count), GF_STATUS_OK, "count the items");
    gf_kit_check(kit, count == 3u, "three items");

    gf_list_view_item read;
    memset(&read, 0, sizeof(read));
    read.struct_size = (uint32_t)sizeof(read);
    char text[64];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).list_view.item_at(view, 2u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read an item");
    gf_kit_check(kit, gf_kit_view_equals(read.stable_id, "charlie") && gf_kit_view_equals(read.text, "Bravo draft") &&
                          gf_kit_view_equals(read.group, "drafts") && read.image_key.size == 0u &&
                          read.sub_items == NULL && read.sub_item_count == 0u && read.checked == 0u,
                 "the item reads back as set");
    gf_kit_status(kit, (*kit).list_view.item_at(view, 1u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read a checked item");
    gf_kit_check(kit, read.checked == 1u && read.sub_item_count == 1u, "the checked item has its sub-item");
    gf_kit_status(kit, (*kit).list_view.item_sub_item(view, 1u, 0u, text, sizeof(text), &required), GF_STATUS_OK,
                  "read a sub-item");
    gf_kit_check(kit, required == 4u && memcmp(text, "3 KB", 4u) == 0, "the sub-item is the one set");
    gf_kit_status(kit, (*kit).list_view.item_sub_item(view, 1u, 1u, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "a sub-item past the sub-items is not found");
    gf_kit_status(kit, (*kit).list_view.item_at(view, 3u, &read, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "an item past the items is not found");

    uint64_t index = 9u;
    gf_kit_status(kit, (*kit).list_view.index_of(view, gf_kit_text("bravo"), &index), GF_STATUS_OK,
                  "find an item by id");
    gf_kit_check(kit, index == 1u, "bravo is the second item");
    gf_kit_status(kit, (*kit).list_view.index_of(view, gf_kit_text("nobody"), &index), GF_STATUS_NOT_FOUND,
                  "an unknown id is not found");

    gf_kit_status(kit, (*kit).list_view.set_item_checked(view, 0u, 1u), GF_STATUS_OK, "check an item");
    gf_kit_status(kit, (*kit).list_view.item_at(view, 0u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the checked item");
    gf_kit_check(kit, read.checked == 1u, "the item reads back checked");
    gf_kit_status(kit, (*kit).list_view.set_item_checked(view, 0u, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "checked is 0 or 1");
    gf_kit_status(kit, (*kit).list_view.set_item_checked(view, 7u, 1u), GF_STATUS_NOT_FOUND,
                  "an item past the items is not checked");

    gf_list_view_item repeated[2];
    repeated[0] = item_record("same", "One", NULL, 0u);
    repeated[1] = item_record("same", "Two", NULL, 0u);
    gf_kit_status(kit, (*kit).list_view.set_items(view, repeated, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "a repeated item id is refused");
    gf_list_view_item crowded = item_record("crowded", "Crowded", alpha_sizes, GF_LIST_VIEW_MAXIMUM_SUB_ITEMS + 1u);
    gf_kit_status(kit, (*kit).list_view.set_items(view, &crowded, 1u), GF_STATUS_LIMIT_EXCEEDED,
                  "more sub-items than an item carries are refused");
    gf_list_view_item missing = item_record("missing", "Missing", NULL, 2u);
    gf_kit_status(kit, (*kit).list_view.set_items(view, &missing, 1u), GF_STATUS_INVALID_ARGUMENT,
                  "sub-items without their array are refused");
    gf_kit_status(kit, (*kit).list_view.item_count(view, &count), GF_STATUS_OK, "count the items again");
    gf_kit_check(kit, count == 3u, "a refused replacement changes nothing");

    gf_kit_status(kit, (*kit).list_view.sort_items(view, 0u, GF_LIST_VIEW_SORT_ORDER_ASCENDING), GF_STATUS_OK,
                  "sort the items by name");
    gf_kit_status(kit, (*kit).list_view.index_of(view, gf_kit_text("bravo"), &index), GF_STATUS_OK,
                  "find the sorted item");
    gf_kit_check(kit, index == 0u, "alpha report sorts first, case folded");
    gf_kit_status(kit, (*kit).list_view.sort_items(view, 4u, GF_LIST_VIEW_SORT_ORDER_ASCENDING), GF_STATUS_NOT_FOUND,
                  "a sort by a column past the columns is refused");
    gf_kit_status(kit, (*kit).list_view.sort_items(view, 0u, 5u), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown sort order is refused");
    gf_kit_status(kit, (*kit).list_view.set_sort_indicator(view, 1, GF_LIST_VIEW_SORT_ORDER_DESCENDING), GF_STATUS_OK,
                  "show a sort glyph");
    gf_kit_status(kit, (*kit).list_view.set_sort_indicator(view, -1, GF_LIST_VIEW_SORT_ORDER_NONE), GF_STATUS_OK,
                  "remove the sort glyph");
    gf_kit_status(kit, (*kit).list_view.set_sort_indicator(view, 2, GF_LIST_VIEW_SORT_ORDER_ASCENDING),
                  GF_STATUS_NOT_FOUND, "a glyph past the columns is refused");
    gf_kit_status(kit, (*kit).list_view.set_sort_indicator(view, -2, GF_LIST_VIEW_SORT_ORDER_ASCENDING),
                  GF_STATUS_INVALID_ARGUMENT, "a column below -1 is refused");
}

static void groups(gf_kit* kit, gf_object view) {
    gf_list_view_group records[2];
    records[0] = group_record("reports", "Reports");
    records[1] = group_record("drafts", "Drafts");
    records[1].header_alignment = GF_LIST_VIEW_ALIGNMENT_CENTER;
    gf_kit_status(kit, (*kit).list_view.set_groups(view, records, 2u), GF_STATUS_OK, "set two groups");
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).list_view.group_count(view, &count), GF_STATUS_OK, "count the groups");
    gf_kit_check(kit, count == 2u, "two groups");
    gf_list_view_group read;
    memset(&read, 0, sizeof(read));
    read.struct_size = (uint32_t)sizeof(read);
    char text[32];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).list_view.group_at(view, 1u, &read, text, sizeof(text), &required), GF_STATUS_OK,
                  "read a group");
    gf_kit_check(kit, gf_kit_view_equals(read.stable_id, "drafts") && gf_kit_view_equals(read.header, "Drafts") &&
                          read.header_alignment == GF_LIST_VIEW_ALIGNMENT_CENTER,
                 "the group reads back as set");
    gf_kit_status(kit, (*kit).list_view.group_at(view, 2u, &read, text, sizeof(text), &required),
                  GF_STATUS_NOT_FOUND, "a group past the groups is not found");
    gf_list_view_group nameless = group_record("", "Nameless");
    gf_kit_status(kit, (*kit).list_view.set_groups(view, &nameless, 1u), GF_STATUS_INVALID_ARGUMENT,
                  "a group without an id is refused");
}

static void selection_and_events(gf_kit* kit, gf_object view) {
    gf_kit_recorder selection;
    gf_kit_recorder activated;
    gf_kit_recorder clicked;
    memset(&selection, 0, sizeof(selection));
    memset(&activated, 0, sizeof(activated));
    memset(&clicked, 0, sizeof(clicked));
    selection.kit = kit;
    activated.kit = kit;
    clicked.kit = kit;
    const gf_callback selection_callback = gf_kit_recording_callback(&selection);
    const gf_callback activated_callback = gf_kit_recording_callback(&activated);
    const gf_callback clicked_callback = gf_kit_recording_callback(&clicked);
    gf_subscription subscriptions[3] = {0u, 0u, 0u};
    gf_kit_status(kit, (*kit).events.subscribe(view, gf_kit_text("selection_changed"), &selection_callback,
                                               &subscriptions[0]),
                  GF_STATUS_OK, "subscribe to selection changes");
    gf_kit_status(kit, (*kit).events.subscribe(view, gf_kit_text("item_activated"), &activated_callback,
                                               &subscriptions[1]),
                  GF_STATUS_OK, "subscribe to item activation");
    gf_kit_status(kit, (*kit).events.subscribe(view, gf_kit_text("column_click"), &clicked_callback,
                                               &subscriptions[2]),
                  GF_STATUS_OK, "subscribe to column clicks");

    gf_kit_status(kit, (*kit).list_view.clear_selection(view), GF_STATUS_OK, "clear the selection");
    const uint32_t before = selection.invoked;
    gf_kit_status(kit, (*kit).list_view.set_selected(view, 2u, 1u), GF_STATUS_OK, "select an item");
    gf_kit_check(kit, selection.invoked == before + 1u, "selection_changed is raised once");
    uint64_t indices[4] = {9u, 9u, 9u, 9u};
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).list_view.selected_items(view, indices, 0u, &count), GF_STATUS_BUFFER_TOO_SMALL,
                  "a short array is refused");
    gf_kit_check(kit, count == 1u && indices[0] == 9u, "the count is given, no index written");
    gf_kit_status(kit, (*kit).list_view.selected_items(view, indices, 4u, &count), GF_STATUS_OK,
                  "read the selection");
    gf_kit_check(kit, count == 1u && indices[0] == 2u, "the third item is selected");
    gf_kit_status(kit, (*kit).list_view.set_selected(view, 2u, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "selected is 0 or 1");
    gf_kit_status(kit, (*kit).list_view.set_selected(view, 9u, 1u), GF_STATUS_NOT_FOUND,
                  "an item past the items is not selected");
    gf_kit_status(kit, (*kit).list_view.select_all(view), GF_STATUS_OK, "select every item");
    gf_kit_status(kit, (*kit).list_view.selected_items(view, indices, 4u, &count), GF_STATUS_OK,
                  "read the whole selection");
    gf_kit_check(kit, count == 3u && indices[0] == 0u && indices[2] == 2u, "every item is selected, ascending");

    int64_t focused = -2;
    gf_kit_status(kit, (*kit).list_view.set_focused_item(view, 1), GF_STATUS_OK, "focus an item");
    gf_kit_status(kit, (*kit).list_view.focused_item(view, &focused), GF_STATUS_OK, "read the focused item");
    gf_kit_check(kit, focused == 1, "the second item has the focus cue");
    gf_kit_status(kit, (*kit).list_view.set_focused_item(view, -1), GF_STATUS_OK, "remove the focus cue");
    gf_kit_status(kit, (*kit).list_view.focused_item(view, &focused), GF_STATUS_OK, "read no focused item");
    gf_kit_check(kit, focused == -1, "no item has the focus cue");
    gf_kit_status(kit, (*kit).list_view.set_focused_item(view, 5), GF_STATUS_NOT_FOUND,
                  "an item past the items is not focused");
    gf_kit_status(kit, (*kit).list_view.ensure_visible(view, 2u), GF_STATUS_OK, "scroll an item into view");
    gf_kit_status(kit, (*kit).list_view.ensure_visible(view, 3u), GF_STATUS_NOT_FOUND,
                  "an item past the items is not scrolled to");

    /* Activation and header clicks come from input; neither was raised. */
    gf_kit_check(kit, activated.invoked == 0u && clicked.invoked == 0u, "no input, no activation or column click");
    for (uint32_t index = 0u; index < 3u; ++index) {
        gf_kit_status(kit, (*kit).events.disconnect(subscriptions[index]), GF_STATUS_OK, "end a subscription");
    }
}

void gf_kit_list_view(gf_kit* kit) {
    const gf_object view = gf_kit_create(kit, "list_view", "kit.list");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(view, &window), GF_STATUS_OK, "make the list view's window");
    const gf_object label = gf_kit_create(kit, "label", "kit.list.label");
    uint64_t count = 9u;
    gf_kit_status(kit, (*kit).list_view.item_count(label, &count), GF_STATUS_WRONG_HANDLE_KIND,
                  "a label is not a list view");

    settings(kit, view);
    columns(kit, view);
    items(kit, view);
    groups(kit, view);
    selection_and_events(kit, view);

    gf_kit_status(kit, (*kit).list_view.set_items(view, NULL, 0u), GF_STATUS_OK, "clear the items");
    gf_kit_status(kit, (*kit).list_view.item_count(view, &count), GF_STATUS_OK, "count the cleared items");
    gf_kit_check(kit, count == 0u, "no items remain");

    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the label");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the list view's window");
    gf_kit_status(kit, (*kit).objects.release(view), GF_STATUS_OK, "release the list view");
}
