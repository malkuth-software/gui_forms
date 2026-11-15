/* Kit part: keyboard focus (since 1.1): controls.focus and focus_state, and
 * windows.focused_control, on a window from windows.create (no run needed)
 * and on a root passed straight to windows.run. A window disabled and
 * enabled again gives its last focused control focus back; a control
 * disabled inside it loses focus for good, as in Windows Forms. A composite
 * field (a combo box with an edit field, a numeric up-down) gives the caret
 * to its edit part and is still the window's focused control; focusable is
 * a schema property. The controls table's relative pointer slots (since
 * 1.5) are checked here too, on a window that has not run. */

#include "gf_conformance_internal.h"

#include <math.h>
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

    /* The business card in the hand (since 1.5): over the window once, over
     * a control in it not again, and refused for a control in no window.
     * Releasing the window closes its layer. */
    if ((*kit).windows.struct_size >= offsetof(gf_windows_table, show_business_card) + sizeof(void*)) {
        uint32_t shown = 9u;
        gf_kit_status(kit, (*kit).windows.show_business_card(window, &shown), GF_STATUS_OK,
                      "show the business card over the window");
        gf_kit_check(kit, shown == 1u, "the card comes out");
        gf_kit_status(kit, (*kit).windows.show_business_card(field, &shown), GF_STATUS_OK,
                      "show the business card over a control's window");
        gf_kit_check(kit, shown == 0u, "a window shows one card at a time");
        gf_kit_status(kit, (*kit).windows.show_business_card(loose, &shown), GF_STATUS_INVALID_STATE,
                      "show_business_card of a control not in a window");
    }

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

/* The controls table's relative pointer slots (since 1.5), on a control
 * outside a window and on a window that has not run. A window that has not
 * run is normally inactive, so the request is refused as NOT_ACTIVE; a host
 * that reports it active grants it, and then the mode is released. */
static int controls_have_relative_pointer(const gf_kit* kit) {
    const int present =
        (*kit).controls.struct_size >= offsetof(gf_controls_table, set_pointer_release_regions) + sizeof(void*) &&
        (*kit).controls.request_relative_pointer != NULL && (*kit).controls.release_relative_pointer != NULL &&
        (*kit).controls.relative_pointer_state != NULL && (*kit).controls.warp_pointer != NULL &&
        (*kit).controls.set_pointer_release_regions != NULL;
    return present;
}

static gf_relative_pointer_request relative_request(gf_relative_pointer_mode mode) {
    gf_relative_pointer_request request;
    memset(&request, 0, sizeof(request));
    request.struct_size = (uint32_t)sizeof(request);
    request.mode = mode;
    return request;
}

void gf_kit_relative_pointer(gf_kit* kit) {
    if (!controls_have_relative_pointer(kit)) {
        return;
    }
    gf_relative_pointer_grant grant;
    memset(&grant, 0, sizeof(grant));
    grant.struct_size = (uint32_t)sizeof(grant);
    gf_relative_pointer_state state;
    memset(&state, 0, sizeof(state));
    state.struct_size = (uint32_t)sizeof(state);
    uint32_t released = 9u;
    uint32_t warped = 9u;
    gf_logical_point position;
    position.x = 4.0;
    position.y = 4.0;

    /* A control outside a window. */
    const gf_object loose = gf_kit_create(kit, "Button", "kit.relative.loose");
    gf_relative_pointer_request request = relative_request(GF_RELATIVE_POINTER_MODE_LOCKED);
    gf_kit_status(kit, (*kit).controls.request_relative_pointer(loose, &request, &grant), GF_STATUS_OK,
                  "request a relative pointer outside a window");
    gf_kit_check(kit, grant.result == GF_RELATIVE_POINTER_RESULT_DETACHED,
                 "a control outside a window is refused as detached");
    gf_kit_status(kit, (*kit).controls.request_relative_pointer(loose, NULL, &grant), GF_STATUS_INVALID_ARGUMENT,
                  "a null request is refused");
    gf_kit_status(kit, (*kit).controls.request_relative_pointer(loose, &request, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "a null grant is refused");
    request.raw_motion = 2u;
    gf_kit_status(kit, (*kit).controls.request_relative_pointer(loose, &request, &grant), GF_STATUS_INVALID_ARGUMENT,
                  "a request flag that is not 0 or 1 is refused");
    request = relative_request(3u);
    gf_kit_status(kit, (*kit).controls.request_relative_pointer(loose, &request, &grant), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown mode is refused");
    state.owned = 9u;
    gf_kit_status(kit, (*kit).controls.relative_pointer_state(loose, &state), GF_STATUS_OK,
                  "relative_pointer_state outside a window");
    gf_kit_check(kit, state.owned == 0u && state.active == 0u && state.grants == 0u,
                 "a control outside a window reports no relative pointer");
    gf_kit_status(kit, (*kit).controls.relative_pointer_state(loose, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "relative_pointer_state needs an output");
    gf_kit_status(kit, (*kit).controls.release_relative_pointer(loose, &released), GF_STATUS_OK,
                  "release_relative_pointer without a mode");
    gf_kit_check(kit, released == 0u, "a control without a mode releases nothing");
    gf_kit_status(kit, (*kit).controls.warp_pointer(loose, position, &warped), GF_STATUS_OK,
                  "warp_pointer without a mode");
    gf_kit_check(kit, warped == 0u, "a control without a mode does not warp");
    gf_logical_point nowhere = position;
    nowhere.x = NAN;
    warped = 9u;
    gf_kit_status(kit, (*kit).controls.warp_pointer(loose, nowhere, &warped), GF_STATUS_INVALID_ARGUMENT,
                  "a position that is not finite is refused");
    gf_kit_check(kit, warped == 9u, "a refused warp leaves its output");
    gf_logical_rect region;
    region.x = 0.0;
    region.y = 0.0;
    region.width = 20.0;
    region.height = 10.0;
    gf_kit_status(kit, (*kit).controls.set_pointer_release_regions(loose, &region, 1u), GF_STATUS_INVALID_STATE,
                  "release regions need a window");

    /* A control in a window that has not run. */
    const gf_object root = gf_kit_create(kit, "Panel", "kit.relative");
    const gf_object view = gf_kit_create(kit, "Button", "kit.relative.view");
    gf_kit_status(kit, (*kit).objects.insert_child(root, view, 0u), GF_STATUS_OK, "put the view in the root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window for the relative pointer");
    gf_kit_status(kit, (*kit).controls.set_pointer_release_regions(view, &region, 1u), GF_STATUS_OK,
                  "set one release region");
    gf_logical_rect regions[9];
    for (uint32_t index = 0u; index < 9u; ++index) {
        regions[index] = region;
    }
    gf_kit_status(kit, (*kit).controls.set_pointer_release_regions(view, regions, 9u), GF_STATUS_INVALID_ARGUMENT,
                  "more than 8 release regions are refused");
    gf_kit_status(kit, (*kit).controls.set_pointer_release_regions(view, NULL, 1u), GF_STATUS_INVALID_ARGUMENT,
                  "release regions need an array");
    gf_logical_rect broken = region;
    broken.width = NAN;
    gf_kit_status(kit, (*kit).controls.set_pointer_release_regions(view, &broken, 1u), GF_STATUS_INVALID_ARGUMENT,
                  "a release region that is not finite is refused");
    gf_kit_status(kit, (*kit).controls.set_pointer_release_regions(view, NULL, 0u), GF_STATUS_OK,
                  "count 0 clears the release regions");

    request = relative_request(GF_RELATIVE_POINTER_MODE_FREE);
    memset(&grant, 0, sizeof(grant));
    grant.struct_size = (uint32_t)sizeof(grant);
    gf_kit_status(kit, (*kit).controls.request_relative_pointer(view, &request, &grant), GF_STATUS_OK,
                  "request a free relative pointer in a window");
    const int granted =
        grant.result == GF_RELATIVE_POINTER_RESULT_GRANTED || grant.result == GF_RELATIVE_POINTER_RESULT_DEGRADED;
    gf_kit_check(kit, granted || grant.result == GF_RELATIVE_POINTER_RESULT_NOT_ACTIVE,
                 "a window that has not run refuses as not active, or grants");
    gf_kit_status(kit, (*kit).controls.relative_pointer_state(view, &state), GF_STATUS_OK,
                  "relative_pointer_state in a window");
    gf_kit_check(kit, state.owned == (granted ? 1u : 0u) && state.active == (granted ? 1u : 0u),
                 "the state reports whether the view holds the mode");
    gf_kit_check(kit, state.grants == (granted ? 1u : 0u), "the state counts the grant");
    released = 9u;
    gf_kit_status(kit, (*kit).controls.release_relative_pointer(view, &released), GF_STATUS_OK,
                  "release the view's relative pointer");
    gf_kit_check(kit, released == (granted ? 1u : 0u), "release reports whether a mode ended");
    gf_kit_status(kit, (*kit).controls.release_relative_pointer(view, NULL), GF_STATUS_OK,
                  "release_relative_pointer takes a null output");
    gf_kit_status(kit, (*kit).controls.warp_pointer(view, position, NULL), GF_STATUS_OK,
                  "warp_pointer takes a null output");

    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the relative pointer window");
    gf_kit_status(kit, (*kit).objects.release(view), GF_STATUS_OK, "release the view");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the relative pointer root");
    gf_kit_status(kit, (*kit).objects.release(loose), GF_STATUS_OK, "release the loose control");
}
