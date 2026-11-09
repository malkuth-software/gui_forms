/* Kit part: keyboard focus (since 1.1): controls.focus and focus_state, and
 * windows.focused_control, on a window from windows.create (no run needed)
 * and on a root passed straight to windows.run. A window disabled and
 * enabled again gives its last focused control focus back; a control
 * disabled inside it loses focus for good, as in Windows Forms. A composite
 * field (a combo box with an edit field, a numeric up-down) gives the caret
 * to its edit part and is still the window's focused control; focusable is
 * a schema property. */

#include "gf_conformance_internal.h"

#include <stddef.h>
#include <string.h>

/* The table sizes that carry the focus slots. */
static int tables_have_focus(const gf_kit* kit) {
    const int controls =
        (*kit).controls.struct_size >= offsetof(gf_controls_table, focus_state) + sizeof(void*) &&
        (*kit).controls.focus != NULL && (*kit).controls.focus_state != NULL;
    const int windows =
        (*kit).windows.struct_size >= offsetof(gf_windows_table, focused_control) + sizeof(void*) &&
        (*kit).windows.focused_control != NULL;
    return controls && windows;
}

static gf_value boolean_value(uint32_t boolean) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    value.kind = GF_VALUE_KIND_BOOLEAN;
    value.boolean = boolean;
    return value;
}

static void set_enabled(gf_kit* kit, gf_object object, uint32_t enabled, const char* what) {
    const gf_value value = boolean_value(enabled);
    gf_kit_status(kit, (*kit).properties.set(object, gf_kit_text("enabled"), &value), GF_STATUS_OK, what);
}

/* Whether window_or_root's window has a focused control; releases the
 * reference it reads. Returns 0 or 1, or 2 when the read failed. */
static int has_focused_control(gf_kit* kit, gf_object window_or_root, const char* what) {
    gf_object focused = 0u;
    const gf_status status = (*kit).windows.focused_control(window_or_root, &focused);
    gf_kit_status(kit, status, GF_STATUS_OK, what);
    if (status != GF_STATUS_OK) {
        return 2;
    }
    if (focused == 0u) {
        return 0;
    }
    gf_kit_status(kit, (*kit).objects.release(focused), GF_STATUS_OK, "release the focused control");
    return 1;
}

/* Whether window_or_root's window reports the control of stable_id as its
 * focused control; releases the reference it reads. */
static int focused_control_is(gf_kit* kit, gf_object window_or_root, const char* stable_id, const char* what) {
    gf_object focused = 0u;
    const gf_status status = (*kit).windows.focused_control(window_or_root, &focused);
    gf_kit_status(kit, status, GF_STATUS_OK, what);
    if (status != GF_STATUS_OK || focused == 0u) {
        return 0;
    }
    char text[64];
    uint64_t required = 0u;
    const gf_status named = (*kit).objects.stable_id(focused, text, sizeof(text), &required);
    const size_t length = strlen(stable_id);
    const int same = named == GF_STATUS_OK && required == length && memcmp(text, stable_id, length) == 0;
    gf_kit_status(kit, (*kit).objects.release(focused), GF_STATUS_OK, "release the focused control");
    return same;
}

/* Composite fields and the focusable property, on a window that has not run. */
static void composite_focus(gf_kit* kit) {
    uint32_t focused = 9u;
    uint32_t contains = 9u;
    const gf_object root = gf_kit_create(kit, "Panel", "kit.focus.composite");
    const gf_object list = gf_kit_create(kit, "ComboBox", "kit.focus.list");
    const gf_object combo = gf_kit_create(kit, "ComboBox", "kit.focus.combo");
    const gf_object number = gf_kit_create(kit, "NumericUpDown", "kit.focus.number");
    const gf_object button = gf_kit_create(kit, "Button", "kit.focus.button");
    gf_kit_status(kit, (*kit).objects.insert_child(root, list, 0u), GF_STATUS_OK, "put the list in the root");
    gf_kit_status(kit, (*kit).objects.insert_child(root, combo, 1u), GF_STATUS_OK, "put the combo box in the root");
    gf_kit_status(kit, (*kit).objects.insert_child(root, number, 2u), GF_STATUS_OK, "put the up-down in the root");
    gf_kit_status(kit, (*kit).objects.insert_child(root, button, 3u), GF_STATUS_OK, "put the button in the root");
    gf_value style;
    memset(&style, 0, sizeof(style));
    style.struct_size = (uint32_t)sizeof(style);
    style.kind = GF_VALUE_KIND_ENUMERATION;
    style.signed_integer = 1; /* drop_down: an edit field */
    gf_kit_status(kit, (*kit).properties.set(combo, gf_kit_text("drop_down_style"), &style), GF_STATUS_OK,
                  "give the combo box an edit field");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window for composites");

    gf_kit_status(kit, (*kit).controls.focus(list, &focused), GF_STATUS_OK, "focus a drop-down list combo box");
    gf_kit_check(kit, focused_control_is(kit, window, "kit.focus.list", "focused_control after the list"),
                 "the drop-down list is the focused control");

    gf_kit_status(kit, (*kit).controls.focus(combo, &focused), GF_STATUS_OK, "focus an editable combo box");
    gf_kit_status(kit, (*kit).controls.focus_state(combo, &focused, &contains), GF_STATUS_OK,
                  "focus_state of the editable combo box");
    gf_kit_check(kit, focused == 1u && contains == 1u, "the combo box is focused while its edit part has the caret");
    gf_kit_check(kit, focused_control_is(kit, window, "kit.focus.combo", "focused_control after the combo box"),
                 "focused_control reports the combo box, not its edit part");
    gf_kit_status(kit, (*kit).controls.focus_state(list, &focused, &contains), GF_STATUS_OK,
                  "focus_state of the list after the move");
    gf_kit_check(kit, focused == 0u && contains == 0u, "the list lost focus");

    gf_kit_status(kit, (*kit).controls.focus(number, &focused), GF_STATUS_OK, "focus a numeric up-down");
    gf_kit_status(kit, (*kit).controls.focus_state(number, &focused, &contains), GF_STATUS_OK,
                  "focus_state of the numeric up-down");
    gf_kit_check(kit, focused == 1u && contains == 1u, "the numeric up-down is focused through its edit part");
    gf_kit_check(kit, focused_control_is(kit, root, "kit.focus.number", "focused_control after the up-down"),
                 "focused_control reports the numeric up-down");
    set_enabled(kit, number, 0u, "disable the numeric up-down");
    gf_kit_status(kit, (*kit).controls.focus(number, &focused), GF_STATUS_INVALID_STATE,
                  "a disabled numeric up-down cannot take focus");
    set_enabled(kit, number, 1u, "enable the numeric up-down again");

    /* focusable is a schema property C can write. */
    gf_value value = boolean_value(0u);
    gf_kit_status(kit, (*kit).properties.set(button, gf_kit_text("focusable"), &value), GF_STATUS_OK,
                  "make the button unfocusable");
    gf_kit_status(kit, (*kit).controls.focus(button, &focused), GF_STATUS_INVALID_STATE,
                  "an unfocusable button cannot take focus");
    value = boolean_value(1u);
    gf_kit_status(kit, (*kit).properties.set(button, gf_kit_text("focusable"), &value), GF_STATUS_OK,
                  "make the button focusable again");
    gf_kit_status(kit, (*kit).controls.focus(button, &focused), GF_STATUS_OK, "focus the focusable button");
    value = boolean_value(1u);
    gf_kit_status(kit, (*kit).properties.set(root, gf_kit_text("focusable"), &value), GF_STATUS_OK,
                  "make the panel focusable");
    gf_kit_status(kit, (*kit).controls.focus(root, &focused), GF_STATUS_OK, "focus the focusable panel");

    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the composite window");
    gf_kit_status(kit, (*kit).objects.release(button), GF_STATUS_OK, "release the button");
    gf_kit_status(kit, (*kit).objects.release(number), GF_STATUS_OK, "release the numeric up-down");
    gf_kit_status(kit, (*kit).objects.release(combo), GF_STATUS_OK, "release the combo box");
    gf_kit_status(kit, (*kit).objects.release(list), GF_STATUS_OK, "release the list");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the composite root");
}

/* Focus on a window that has not run: it is recorded, not yet the keyboard's. */
void gf_kit_focus_refusals(gf_kit* kit) {
    gf_kit_check(kit, tables_have_focus(kit), "the controls and windows tables have the focus slots");
    if (!tables_have_focus(kit)) {
        return;
    }
    uint32_t focused = 9u;
    uint32_t contains = 9u;
    const gf_object loose = gf_kit_create(kit, "Button", "kit.focus.loose");
    gf_kit_status(kit, (*kit).controls.focus(loose, &focused), GF_STATUS_INVALID_STATE,
                  "a control not in a window cannot take focus");
    gf_kit_check(kit, focused == 9u, "a refused focus leaves its output");
    gf_kit_status(kit, (*kit).controls.focus_state(loose, &focused, &contains), GF_STATUS_OK,
                  "focus_state of a control not in a window");
    gf_kit_check(kit, focused == 0u && contains == 0u, "a control not in a window has no focus");
    gf_object none = 7u;
    gf_kit_status(kit, (*kit).windows.focused_control(loose, &none), GF_STATUS_INVALID_STATE,
                  "focused_control of a control not in a window");

    const gf_object root = gf_kit_create(kit, "Panel", "kit.focus");
    const gf_object field = gf_kit_create(kit, "Button", "kit.focus.field");
    gf_kit_status(kit, (*kit).objects.insert_child(root, field, 0u), GF_STATUS_OK, "put the field in the root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window for focus");
    gf_kit_check(kit, has_focused_control(kit, window, "focused_control of a new window") == 0,
                 "a new window has no focused control");
    gf_kit_status(kit, (*kit).windows.focused_control(window, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "focused_control needs an output");

    gf_kit_status(kit, (*kit).controls.focus(root, &focused), GF_STATUS_INVALID_STATE,
                  "a panel that is not focusable cannot take focus");
    focused = 9u;
    gf_kit_status(kit, (*kit).controls.focus(field, &focused), GF_STATUS_OK, "focus the field");
    gf_kit_check(kit, focused == 0u, "a window that has not run does not have the keyboard");
    gf_kit_status(kit, (*kit).controls.focus_state(field, &focused, &contains), GF_STATUS_OK,
                  "focus_state of the focused field");
    gf_kit_check(kit, focused == 1u && contains == 1u, "the field is focused and contains focus");
    gf_kit_status(kit, (*kit).controls.focus_state(root, &focused, &contains), GF_STATUS_OK,
                  "focus_state of the root");
    gf_kit_check(kit, focused == 0u && contains == 1u, "the root contains focus without having it");
    gf_kit_status(kit, (*kit).controls.focus_state(field, NULL, NULL), GF_STATUS_OK,
                  "focus_state takes null outputs");
    gf_kit_status(kit, (*kit).controls.focus(field, NULL), GF_STATUS_OK, "focus takes a null output");
    gf_kit_check(kit, has_focused_control(kit, window, "focused_control of the window") == 1,
                 "the window reports its focused control");
    gf_kit_check(kit, has_focused_control(kit, root, "focused_control of the root") == 1,
                 "the root reports its window's focused control");
    gf_kit_check(kit, has_focused_control(kit, field, "focused_control of the field") == 1,
                 "any control in the window reports it");

    /* The window disabled and enabled again gives focus back. */
    set_enabled(kit, root, 0u, "disable the window's root");
    gf_kit_check(kit, has_focused_control(kit, window, "focused_control of a disabled window") == 0,
                 "a disabled window has no focused control");
    gf_kit_status(kit, (*kit).controls.focus(field, &focused), GF_STATUS_INVALID_STATE,
                  "a control in a disabled window cannot take focus");
    set_enabled(kit, root, 1u, "enable the window's root again");
    gf_kit_check(kit, has_focused_control(kit, window, "focused_control of the enabled window") == 1,
                 "the enabled window gave its last focused control focus back");

    /* A control disabled inside the window loses focus for good. */
    set_enabled(kit, field, 0u, "disable the field");
    gf_kit_status(kit, (*kit).controls.focus(field, &focused), GF_STATUS_INVALID_STATE,
                  "a disabled control cannot take focus");
    gf_kit_check(kit, has_focused_control(kit, window, "focused_control after the field's disable") == 0,
                 "a disabled control loses focus");
    set_enabled(kit, field, 1u, "enable the field again");
    gf_kit_check(kit, has_focused_control(kit, window, "focused_control after the field's enable") == 0,
                 "an enabled control does not take focus back by itself");

    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the focus window");
    gf_kit_status(kit, (*kit).objects.release(field), GF_STATUS_OK, "release the field");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the focus window's root");
    gf_kit_status(kit, (*kit).objects.release(loose), GF_STATUS_OK, "release the loose button");
    composite_focus(kit);
}

/* The window part: what the ready callback saw. */
typedef struct focus_run {
    gf_kit* kit;
    gf_object root;
    gf_object field;
    gf_status focus;
    gf_status state;
    gf_status report;
    gf_status close;
    uint32_t focused;
    uint32_t state_focused;
    gf_object reported;
} focus_run;

static void GF_CALL on_focus_ready(void* context, gf_event* event) {
    focus_run* run = (focus_run*)context;
    gf_kit* kit = (*run).kit;
    (*run).focus = (*kit).controls.focus((*run).field, &(*run).focused);
    (*run).state = (*kit).controls.focus_state((*run).field, &(*run).state_focused, NULL);
    (*run).report = (*kit).windows.focused_control((*run).root, &(*run).reported);
    (*run).close = (*kit).windows.request_close((*event).sender);
}

/* Focus on a root passed straight to windows.run. */
void gf_kit_focus(gf_kit* kit) {
    if (!tables_have_focus(kit)) {
        return;
    }
    focus_run run;
    memset(&run, 0, sizeof(run));
    run.kit = kit;
    run.focus = -1;
    run.state = -1;
    run.report = -1;
    run.close = -1;
    run.focused = 9u;
    run.root = gf_kit_create(kit, "Panel", "kit.focus.run");
    run.field = gf_kit_create(kit, "Button", "kit.focus.run.field");
    gf_kit_status(kit, (*kit).objects.insert_child(run.root, run.field, 0u), GF_STATUS_OK,
                  "put the run's field in its root");
    const gf_callback ready = gf_kit_callback(&on_focus_ready, NULL, &run);
    gf_run_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.title = gf_kit_text("GUI.Forms C ABI focus");
    options.ready = &ready;
    gf_kit_status(kit, (*kit).windows.run(run.root, &options), GF_STATUS_OK, "run a root for focus");
    gf_kit_status(kit, run.focus, GF_STATUS_OK, "focus a control of a run's root");
    gf_kit_check(kit, run.focused == 0u || run.focused == 1u, "focus reports whether the window is active");
    gf_kit_status(kit, run.state, GF_STATUS_OK, "focus_state during the run");
    gf_kit_check(kit, run.state_focused == 1u, "the field is the run's focused control");
    gf_kit_status(kit, run.report, GF_STATUS_OK, "focused_control of a run's root");
    gf_kit_check(kit, run.reported != 0u, "the run's root reports its focused control");
    if (run.reported != 0u) {
        gf_kit_status(kit, (*kit).objects.release(run.reported), GF_STATUS_OK, "release the run's focused control");
    }
    gf_kit_status(kit, run.close, GF_STATUS_OK, "close the focus window");
    gf_kit_status(kit, (*kit).objects.release(run.field), GF_STATUS_OK, "release the run's field");
    gf_kit_status(kit, (*kit).objects.release(run.root), GF_STATUS_OK, "release the run's root");
}
