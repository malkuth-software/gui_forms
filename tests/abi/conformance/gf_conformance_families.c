/* Kit part: the family tables, GF_TABLE_CONTROLS to GF_TABLE_PROPERTY_GRID
 * (stage C5), and themes.load_pixels. */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_create_arguments blank_arguments(void) {
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    return arguments;
}

static void layout(gf_kit* kit) {
    const gf_object panel = gf_kit_create(kit, "panel", "kit.families.layout");
    gf_kit_status(kit, (*kit).controls.suspend_layout(panel), GF_STATUS_OK, "suspend layout");
    gf_kit_status(kit, (*kit).controls.suspend_layout(panel), GF_STATUS_OK, "suspensions nest");
    gf_layout_transaction state;
    memset(&state, 0, sizeof(state));
    state.struct_size = (uint32_t)sizeof(state);
    gf_kit_status(kit, (*kit).controls.layout_state(panel, &state), GF_STATUS_OK, "read the layout state");
    gf_kit_check(kit, state.suspend_depth == 2u && state.reserved == 0u, "two suspensions are open");
    gf_kit_status(kit, (*kit).controls.resume_layout(panel, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "perform_layout is 0 or 1");
    gf_kit_status(kit, (*kit).controls.resume_layout(panel, 0u), GF_STATUS_OK, "resume one");
    gf_kit_status(kit, (*kit).controls.resume_layout(panel, 1u), GF_STATUS_OK, "resume the other");
    gf_kit_status(kit, (*kit).controls.resume_layout(panel, 1u), GF_STATUS_OK,
                  "an unmatched resume is tolerated");
    gf_kit_status(kit, (*kit).controls.layout_state(panel, &state), GF_STATUS_OK, "read it again");
    gf_kit_check(kit, state.suspend_depth == 0u, "no suspension is open");
    gf_layout_transaction short_record;
    memset(&short_record, 0, sizeof(short_record));
    short_record.struct_size = 8u;
    gf_kit_status(kit, (*kit).controls.layout_state(panel, &short_record), GF_STATUS_INVALID_ARGUMENT,
                  "a short layout record is refused");
    gf_kit_status(kit, (*kit).controls.perform_layout(panel), GF_STATUS_OK, "perform layout");

    gf_value bounds;
    memset(&bounds, 0, sizeof(bounds));
    bounds.struct_size = (uint32_t)sizeof(bounds);
    bounds.kind = GF_VALUE_KIND_RECTANGLE;
    bounds.rectangle.x = 10.0;
    bounds.rectangle.y = 20.0;
    bounds.rectangle.width = 100.0;
    bounds.rectangle.height = 50.0;
    gf_kit_status(kit, (*kit).properties.set(panel, gf_kit_text("bounds"), &bounds), GF_STATUS_OK,
                  "place the panel");
    gf_logical_rect placed;
    memset(&placed, 0, sizeof(placed));
    gf_kit_status(kit, (*kit).controls.window_bounds(panel, &placed), GF_STATUS_OK, "read the window bounds");
    gf_kit_check(kit, placed.width >= 0.0 && placed.height >= 0.0, "window bounds have a size");
    gf_kit_status(kit, (*kit).controls.window_bounds(panel, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "window_bounds needs an output");

    uint32_t captured = 9u;
    gf_kit_status(kit, (*kit).controls.pointer_capture(panel, &captured), GF_STATUS_OK, "read pointer capture");
    gf_kit_check(kit, captured == 0u, "a detached panel holds no capture");
    gf_kit_status(kit, (*kit).controls.set_pointer_capture(panel, 2u), GF_STATUS_INVALID_ARGUMENT,
                  "captured is 0 or 1");
    gf_kit_status(kit, (*kit).controls.set_pointer_capture(panel, 0u), GF_STATUS_OK, "release a capture not held");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the layout panel");
}

static void scrolling(gf_kit* kit) {
    const gf_object panel = gf_kit_create(kit, "panel", "kit.families.scroll");
    const gf_object child = gf_kit_create(kit, "button", "kit.families.scroll.child");
    gf_kit_status(kit, (*kit).objects.insert_child(panel, child, 0u), GF_STATUS_OK, "add the scrolled child");
    gf_scroll_axis axis;
    memset(&axis, 0, sizeof(axis));
    axis.enabled = 1u;
    axis.visible = 1u;
    axis.minimum = 0.0;
    axis.maximum = 200.0;
    axis.large_change = 20.0;
    axis.small_change = 2.0;
    axis.value = 40.0;
    gf_kit_status(kit, (*kit).controls.set_scroll_axis(panel, GF_SCROLL_ORIENTATION_VERTICAL, &axis), GF_STATUS_OK,
                  "set the vertical axis");
    gf_scroll_snapshot state;
    memset(&state, 0, sizeof(state));
    state.struct_size = (uint32_t)sizeof(state);
    gf_kit_status(kit, (*kit).controls.scroll_state(panel, &state), GF_STATUS_OK, "read the scroll state");
    gf_kit_check(kit, state.vertical.maximum == 200.0 && state.vertical.large_change == 20.0,
                 "the axis reads back");
    gf_scroll_axis bad = axis;
    bad.value = 300.0;
    gf_kit_status(kit, (*kit).controls.set_scroll_axis(panel, GF_SCROLL_ORIENTATION_VERTICAL, &bad),
                  GF_STATUS_INVALID_ARGUMENT, "a value past the range is refused");
    gf_kit_status(kit, (*kit).controls.set_scroll_axis(panel, 2u, &axis), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown orientation is refused");
    gf_kit_status(kit, (*kit).controls.scroll_into_view(panel, child), GF_STATUS_OK, "scroll the child into view");
    gf_kit_status(kit, (*kit).controls.scroll_state(child, &state), GF_STATUS_WRONG_HANDLE_KIND,
                  "a button does not scroll");
    gf_kit_status(kit, (*kit).objects.release(child), GF_STATUS_OK, "release the scrolled child");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the scroll panel");
}

static void text_editing(gf_kit* kit) {
    const gf_object box = gf_kit_create(kit, "text_box", "kit.families.text");
    gf_kit_status(kit, gf_kit_set_text(kit, box, "text", "hello"), GF_STATUS_OK, "give the box text");
    gf_kit_status(kit, (*kit).text_editing.clear_undo_history(box), GF_STATUS_OK, "start the history afresh");
    gf_kit_status(kit, (*kit).text_editing.select(box, 1u, 4u), GF_STATUS_OK, "select");
    uint64_t anchor = 0u;
    uint64_t caret = 0u;
    gf_kit_status(kit, (*kit).text_editing.selection(box, &anchor, &caret), GF_STATUS_OK, "read the selection");
    gf_kit_check(kit, anchor == 1u && caret == 4u, "the selection reads back");
    gf_kit_status(kit, (*kit).text_editing.select(box, 0u, 9u), GF_STATUS_INVALID_ARGUMENT,
                  "a selection past the text is refused");
    uint64_t boundary = 0u;
    gf_kit_status(kit, (*kit).text_editing.grapheme_boundary(box, 1u, 1, &boundary), GF_STATUS_OK,
                  "the next boundary");
    gf_kit_check(kit, boundary == 2u, "one byte per grapheme in ASCII");
    gf_kit_status(kit, (*kit).text_editing.grapheme_boundary(box, 0u, -1, &boundary), GF_STATUS_OK,
                  "before the start");
    gf_kit_check(kit, boundary == 0u, "the start stays");
    gf_kit_status(kit, (*kit).text_editing.grapheme_boundary(box, 0u, 2, &boundary), GF_STATUS_INVALID_ARGUMENT,
                  "a direction is 1 or -1");
    uint32_t changed = 0u;
    gf_kit_status(kit, (*kit).text_editing.replace_range(box, 0u, 5u, gf_kit_text("world"), &changed), GF_STATUS_OK,
                  "replace the text");
    gf_kit_check(kit, changed == 1u, "the replacement changed the text");
    gf_kit_status(kit, (*kit).text_editing.replace_range(box, 4u, 3u, gf_kit_text("x"), &changed),
                  GF_STATUS_INVALID_ARGUMENT, "a range past the text is refused");
    gf_kit_status(kit, (*kit).text_editing.undo(box, &changed), GF_STATUS_OK, "undo");
    gf_kit_check(kit, changed == 1u, "the replacement is undone");
    gf_kit_status(kit, (*kit).text_editing.redo(box, &changed), GF_STATUS_OK, "redo");
    gf_kit_check(kit, changed == 1u, "the replacement is redone");
    gf_kit_status(kit, (*kit).text_editing.redo(box, &changed), GF_STATUS_OK, "redo with nothing left");
    gf_kit_check(kit, changed == 0u, "nothing is redone");
    const gf_object label = gf_kit_create(kit, "label", "kit.families.text.label");
    gf_kit_status(kit, (*kit).text_editing.undo(label, &changed), GF_STATUS_WRONG_HANDLE_KIND,
                  "a label is not a text box");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the label");
    gf_kit_status(kit, (*kit).objects.release(box), GF_STATUS_OK, "release the text box");
}

static void property_grid(gf_kit* kit) {
    const gf_object grid = gf_kit_create(kit, "property_grid", "kit.families.grid");
    const gf_object button = gf_kit_create(kit, "button", "kit.families.grid.button");
    gf_kit_status(kit, (*kit).property_grid.select_objects(grid, &button, 1u), GF_STATUS_OK, "select the button");
    gf_kit_status(kit, (*kit).property_grid.refresh(grid), GF_STATUS_OK, "refresh the grid");
    uint32_t accepted = 9u;
    gf_kit_status(kit, (*kit).property_grid.try_set_text(grid, gf_kit_text("text"), gf_kit_text("Edited"), &accepted),
                  GF_STATUS_OK, "set a property as text");
    gf_kit_check(kit, accepted == 1u, "the text is accepted");
    uint32_t reset = 9u;
    gf_kit_status(kit, (*kit).property_grid.reset_property(grid, gf_kit_text("text"), &reset), GF_STATUS_OK,
                  "reset a property");
    uint32_t activated = 9u;
    gf_kit_status(kit, (*kit).property_grid.activate_editor(grid, gf_kit_text("text"), &activated), GF_STATUS_OK,
                  "activate an editor");
    gf_kit_check(kit, reset <= 1u && activated <= 1u, "the outcomes are 0 or 1");
    gf_kit_status(kit, (*kit).property_grid.reset_property(grid, gf_kit_text(""), &reset),
                  GF_STATUS_INVALID_ARGUMENT, "a property name is not empty");
    gf_kit_status(kit, (*kit).property_grid.select_objects(grid, NULL, 0u), GF_STATUS_OK, "clear the selection");
    gf_kit_status(kit, (*kit).property_grid.select_objects(grid, NULL, 1u), GF_STATUS_INVALID_ARGUMENT,
                  "objects are given with a count");
    gf_kit_status(kit, (*kit).property_grid.refresh(button), GF_STATUS_WRONG_HANDLE_KIND,
                  "a button is not a property grid");
    gf_kit_status(kit, (*kit).objects.release(button), GF_STATUS_OK, "release the selected button");
    gf_kit_status(kit, (*kit).objects.release(grid), GF_STATUS_OK, "release the grid");
}

static void popups(gf_kit* kit) {
    const gf_object root = gf_kit_create(kit, "panel", "kit.families.popups.root");
    const gf_object owner = gf_kit_create(kit, "button", "kit.families.popups.owner");
    const gf_object popup = gf_kit_create(kit, "panel", "kit.families.popups.popup");
    gf_kit_status(kit, (*kit).popups.open_popup(owner, popup), GF_STATUS_INVALID_STATE,
                  "an owner outside a window cannot open a popup");
    gf_kit_status(kit, (*kit).popups.close_popup(popup), GF_STATUS_NOT_FOUND, "a popup not open is not found");
    gf_kit_status(kit, (*kit).objects.insert_child(root, owner, 0u), GF_STATUS_OK, "put the owner in the root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "a window over the root");
    gf_create_arguments arguments = blank_arguments();
    arguments.owner = window;
    gf_object tips = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("tool_tip"), gf_kit_text("kit.families.tips"),
                                                  &arguments, &tips),
                  GF_STATUS_OK, "a tool_tip bound to the window");
    gf_kit_status(kit, (*kit).popups.set_tool_tip(tips, owner, gf_kit_text("Help")), GF_STATUS_OK,
                  "give the owner a tooltip");
    gf_kit_status(kit, (*kit).popups.show_tool_tip(tips, owner, 0u), GF_STATUS_OK, "show it");
    gf_kit_status(kit, (*kit).popups.show_tool_tip(tips, owner, 500u), GF_STATUS_OK, "show it for a while");
    gf_kit_status(kit, (*kit).popups.hide_tool_tip(tips), GF_STATUS_OK, "hide it");
    gf_kit_status(kit, (*kit).popups.hide_tool_tip(owner), GF_STATUS_WRONG_HANDLE_KIND,
                  "a button is not a tool_tip");
    gf_kit_status(kit, (*kit).objects.release(tips), GF_STATUS_OK, "release the tool_tip");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the window");
    gf_kit_status(kit, (*kit).objects.release(popup), GF_STATUS_OK, "release the popup");
    gf_kit_status(kit, (*kit).objects.release(owner), GF_STATUS_OK, "release the owner");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the root");
}

static void pixels(gf_kit* kit) {
    /* 2 x 1 premultiplied BGRA, rows 12 bytes apart. */
    const uint8_t two_pixels[12] = {0u, 0u, 255u, 255u, 0u, 255u, 0u, 255u, 0u, 0u, 0u, 0u};
    uint64_t image = 0u;
    gf_kit_status(kit, (*kit).themes.load_pixels(two_pixels, 2u, 1u, 12u, &image), GF_STATUS_OK, "load pixels");
    gf_kit_check(kit, image != 0u, "loaded pixels have an id");
    uint64_t refused = 5u;
    gf_kit_status(kit, (*kit).themes.load_pixels(two_pixels, 2u, 1u, 4u, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "rows shorter than their pixels are refused");
    gf_kit_status(kit, (*kit).themes.load_pixels(NULL, 2u, 1u, 8u, &refused), GF_STATUS_INVALID_ARGUMENT,
                  "pixels are given for a non-empty image");
    gf_kit_check(kit, refused == 5u, "a refused load leaves its output");
    gf_kit_status(kit, (*kit).themes.remove_image(image), GF_STATUS_OK, "remove the pixels");
}

void gf_kit_families(gf_kit* kit) {
    layout(kit);
    scrolling(kit);
    text_editing(kit);
    property_grid(kit);
    popups(kit);
    pixels(kit);
}
