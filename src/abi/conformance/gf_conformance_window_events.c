/* Kit part: the interactions a window raises (since 1.1). A window made by
 * create lists and subscribes them like a component's, and its help request
 * reaches a subscriber that may answer it. Needs no window on screen. */

#include "gf_conformance_internal.h"

#include <stddef.h>
#include <string.h>

/* What a help_requested subscriber saw, and whether it answers. */
typedef struct help_probe {
    gf_kit* kit;
    uint32_t invoked;
    uint32_t released;
    int answer;
    int saw_target;
    int saw_keyboard;
    gf_status write_status;
    char target[32];
    /* When set, the callback disposes this window (A-gf-abi-03-F1). */
    gf_object dispose_window;
    gf_status dispose_status;
} help_probe;

/* True when a field's dotted name ends in leaf. */
static int field_is(const gf_event_field* field, const char* leaf) {
    const size_t length = strlen(leaf);
    if ((*field).name.size < length) {
        return 0;
    }
    const char* tail = (*field).name.data + ((*field).name.size - length);
    if (memcmp(tail, leaf, length) != 0) {
        return 0;
    }
    const int whole = (*field).name.size == length || tail[-1] == '.';
    return whole;
}

static void GF_CALL help_invoke(void* context, gf_event* event) {
    help_probe* probe = (help_probe*)context;
    ++(*probe).invoked;
    if ((*probe).dispose_window != 0u) {
        (*probe).dispose_status = (*(*probe).kit).objects.dispose((*probe).dispose_window);
        return;
    }
    uint64_t index = 0u;
    for (index = 0u; index < (*event).field_count; ++index) {
        const gf_event_field* field = &(*event).fields[index];
        if (field_is(field, "target_stable_id") && (*(*field).value).kind == GF_VALUE_KIND_TEXT) {
            const gf_utf8_view text = (*(*field).value).text;
            const size_t size = text.size < sizeof((*probe).target) - 1u ? (size_t)text.size
                                                                          : sizeof((*probe).target) - 1u;
            if (size > 0u) {
                memcpy((*probe).target, text.data, size);
            }
            (*probe).target[size] = '\0';
            (*probe).saw_target = 1;
        }
        if (field_is(field, "keyboard_initiated") && (*(*field).value).kind == GF_VALUE_KIND_BOOLEAN) {
            (*probe).saw_keyboard = (*(*field).value).boolean == 0u ? 1 : 2;
        }
        if ((*probe).answer && field_is(field, "handled")) {
            gf_value handled;
            memset(&handled, 0, sizeof(handled));
            handled.struct_size = (uint32_t)sizeof(handled);
            handled.kind = GF_VALUE_KIND_BOOLEAN;
            handled.boolean = 1u;
            (*probe).write_status = (*(*probe).kit).events.write_field(event, index, &handled);
        }
    }
}

static void GF_CALL help_release(void* context) {
    help_probe* probe = (help_probe*)context;
    ++(*probe).released;
}

/* The listing: twelve interactions, help_requested among them with a
 * mutable payload, the key events (since 1.1), mdi_child_activate last
 * (since 1.6), and an index past the end refused. */
static void window_listing(gf_kit* kit) {
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).windows.interaction_count(&count), GF_STATUS_OK, "a window's interaction_count");
    gf_kit_check(kit, count == 12u, "a window raises twelve interactions");
    int help = 0;
    int focus = 0;
    int keys = 0;
    int mdi = 0;
    uint64_t index = 0u;
    for (index = 0u; index < count; ++index) {
        gf_schema_interaction interaction;
        memset(&interaction, 0, sizeof(interaction));
        interaction.struct_size = (uint32_t)sizeof(interaction);
        if ((*kit).windows.interaction_at(index, &interaction) != GF_STATUS_OK) {
            continue;
        }
        if (gf_kit_view_equals(interaction.name, "help_requested") &&
            gf_kit_view_equals(interaction.payload, "target_stable_id: text, position: point, help_namespace: text, help_string: text, keyword: text, navigator: enumeration, keyboard_initiated: boolean, handled: boolean") && interaction.mutable_payload == 1u) {
            help = 1;
        }
        if (gf_kit_view_equals(interaction.name, "focused_control_changed")) {
            focus = 1;
        }
        if (index + 1u == count && gf_kit_view_equals(interaction.name, "mdi_child_activate") &&
            gf_kit_view_equals(interaction.payload, "previous: text optional, current: text optional") &&
            interaction.mutable_payload == 0u) {
            mdi = 1;
        }
        if ((gf_kit_view_equals(interaction.name, "key_down") || gf_kit_view_equals(interaction.name, "key_up")) &&
            gf_kit_view_equals(interaction.payload, "key_code: unsigned_integer, physical_key: unsigned_integer, scan_code: unsigned_integer, modifiers: enumeration, repeat: boolean, released_by_deactivation: boolean, handled: boolean, suppress_key_press: boolean") && interaction.mutable_payload == 1u) {
            ++keys;
        }
        if (gf_kit_view_equals(interaction.name, "key_press") &&
            gf_kit_view_equals(interaction.payload, "text: text, key_char: unsigned_integer, handled: boolean") && interaction.mutable_payload == 1u) {
            ++keys;
        }
        if (gf_kit_view_equals(interaction.name, "key_state_changed") &&
            gf_kit_view_equals(interaction.payload, "key_code: unsigned_integer, physical_key: unsigned_integer, scan_code: unsigned_integer, modifiers: enumeration, down: boolean, released_by_deactivation: boolean") && interaction.mutable_payload == 0u) {
            ++keys;
        }
    }
    gf_kit_check(kit, keys == 4, "the listing has key_down, key_press, key_up and key_state_changed");
    gf_kit_check(kit, help, "the listing has help_requested with a mutable HelpRequestEvent");
    gf_kit_check(kit, focus, "the listing has focused_control_changed");
    gf_kit_check(kit, mdi, "the listing ends with mdi_child_activate (since 1.6)");
    gf_schema_interaction past;
    memset(&past, 0, sizeof(past));
    past.struct_size = (uint32_t)sizeof(past);
    gf_kit_status(kit, (*kit).windows.interaction_at(count, &past), GF_STATUS_INVALID_ARGUMENT,
                  "interaction_at past the end is refused");
    gf_kit_status(kit, (*kit).windows.interaction_count(NULL), GF_STATUS_INVALID_ARGUMENT,
                  "interaction_count needs an output");
}

/* Subscribing, the help request and its answer, and the end of the
 * window's subscriptions. */
static void window_help(gf_kit* kit) {
    const gf_object root = gf_kit_create(kit, "Panel", "kit.window-events");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window for its events");
    help_probe probe;
    memset(&probe, 0, sizeof(probe));
    probe.kit = kit;
    const gf_callback help = gf_kit_callback(&help_invoke, &help_release, &probe);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(window, gf_kit_text("help_requested"), &help, &subscription),
                  GF_STATUS_OK, "subscribe to a window's help_requested");
    gf_kit_recorder focus;
    memset(&focus, 0, sizeof(focus));
    focus.kit = kit;
    const gf_callback focus_callback = gf_kit_recording_callback(&focus);
    gf_subscription focus_subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(window, gf_kit_text("focused_control_changed"), &focus_callback,
                                               &focus_subscription),
                  GF_STATUS_OK, "subscribe to a window's focused_control_changed");
    const gf_callback unused = gf_kit_callback(&help_invoke, NULL, &probe);
    gf_subscription refused = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(window, gf_kit_text("no_such_window_event"), &unused, &refused),
                  GF_STATUS_NOT_FOUND, "an unknown window event is refused");

    uint32_t handled = 7u;
    gf_kit_status(kit, (*kit).windows.request_help(window, &handled), GF_STATUS_OK, "request a window's help");
    gf_kit_check(kit, probe.invoked == 1u && handled == 0u, "an unanswered request reaches the subscriber once");
    gf_kit_check(kit, probe.saw_target && strcmp(probe.target, "kit.window-events") == 0,
                 "the request targets the window's root");
    gf_kit_check(kit, probe.saw_keyboard == 1, "request_help is not keyboard-initiated");
    probe.answer = 1;
    gf_kit_status(kit, (*kit).windows.request_help(window, &handled), GF_STATUS_OK, "request help again");
    gf_kit_check(kit, probe.write_status == GF_STATUS_OK && handled == 1u,
                 "a subscriber that sets handled answers the request");
    gf_kit_status(kit, (*kit).windows.request_help(root, &handled), GF_STATUS_WRONG_HANDLE_KIND,
                  "request_help takes a window, not its root");
    gf_kit_status(kit, (*kit).windows.request_help(window, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "request_help needs a handled output");

    /* Key state (since 1.1): nothing is held in a window that has seen no
     * key; subscribing the key events is accepted. */
    uint32_t down = 7u;
    gf_kit_status(kit, (*kit).windows.key_state(window, 0x04u, &down), GF_STATUS_OK, "read a key's state");
    gf_kit_check(kit, down == 0u, "no key is down in a window that has seen none");
    gf_kit_status(kit, (*kit).windows.key_state(root, 0x04u, &down), GF_STATUS_WRONG_HANDLE_KIND,
                  "key_state takes a window, not its root");
    gf_kit_status(kit, (*kit).windows.key_state(window, 0x04u, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "key_state needs a down output");
    gf_kit_recorder keys;
    memset(&keys, 0, sizeof(keys));
    keys.kit = kit;
    const gf_callback keys_callback = gf_kit_recording_callback(&keys);
    gf_subscription key_subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(window, gf_kit_text("key_state_changed"), &keys_callback,
                                               &key_subscription),
                  GF_STATUS_OK, "subscribe to a window's key_state_changed");
    const gf_object field = gf_kit_create(kit, "TextBox", "kit.window-events.field");
    gf_subscription control_key = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(field, gf_kit_text("key_down"), &keys_callback, &control_key),
                  GF_STATUS_OK, "subscribe to a control's key_down");
    gf_subscription control_press = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(field, gf_kit_text("key_press"), &keys_callback, &control_press),
                  GF_STATUS_OK, "subscribe to a control's key_press");
    gf_subscription control_preview = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(field, gf_kit_text("preview_key_down"), &keys_callback,
                                               &control_preview),
                  GF_STATUS_OK, "subscribe to a control's preview_key_down");
    gf_kit_status(kit, (*kit).events.disconnect(control_key), GF_STATUS_OK, "end the key_down subscription");
    gf_kit_status(kit, (*kit).events.disconnect(control_press), GF_STATUS_OK, "end the key_press subscription");
    gf_kit_status(kit, (*kit).events.disconnect(control_preview), GF_STATUS_OK,
                  "end the preview_key_down subscription");

    /* Pointer location (since 1.3): a window that has seen no pointer
     * event reports zero and outside, for the window and for its root;
     * every output may be null; a control in no window is refused. */
    const int has_pointer_location =
        (*kit).windows.struct_size >= offsetof(gf_windows_table, pointer_location) + sizeof(void*) &&
        (*kit).windows.pointer_location != NULL;
    gf_kit_check(kit, has_pointer_location, "the windows table has pointer_location");
    if (has_pointer_location) {
        gf_logical_point location = {7.0, 7.0};
        gf_logical_point window_position = {7.0, 7.0};
        uint32_t inside = 7u;
        gf_kit_status(kit, (*kit).windows.pointer_location(window, &location, &window_position, &inside),
                      GF_STATUS_OK, "read a window's pointer location");
        gf_kit_check(kit, location.x == 0.0 && location.y == 0.0 && window_position.x == 0.0 &&
                              window_position.y == 0.0 && inside == 0u,
                     "a window that has seen no pointer event reports zero, outside");
        inside = 7u;
        gf_kit_status(kit, (*kit).windows.pointer_location(root, &location, NULL, &inside), GF_STATUS_OK,
                      "read the pointer location relative to the window's root");
        gf_kit_check(kit, inside == 0u, "the root reports its window's pointer state");
        gf_kit_status(kit, (*kit).windows.pointer_location(window, NULL, NULL, NULL), GF_STATUS_OK,
                      "pointer_location takes null outputs");
        inside = 7u;
        gf_kit_status(kit, (*kit).windows.pointer_location(field, &location, &window_position, &inside),
                      GF_STATUS_INVALID_STATE, "pointer_location of a control not in a window");
        gf_kit_check(kit, inside == 7u, "a refused pointer_location leaves its outputs");
    }
    gf_kit_status(kit, (*kit).objects.release(field), GF_STATUS_OK, "release the key events control");

    gf_kit_status(kit, (*kit).objects.dispose(window), GF_STATUS_OK, "dispose the window");
    gf_kit_check(kit, probe.released == 1u && focus.released == 1u,
                 "destroying the window releases each subscription's callback once");
    gf_kit_status(kit, (*kit).windows.key_state(window, 0x04u, &down), GF_STATUS_DISPOSED,
                  "a disposed window refuses key_state");
    if (has_pointer_location) {
        gf_kit_status(kit, (*kit).windows.pointer_location(window, NULL, NULL, NULL), GF_STATUS_DISPOSED,
                      "a disposed window refuses pointer_location");
    }
    gf_kit_check(kit, focus.off_thread_calls == 0u, "the releases run on the UI thread");
    gf_kit_status(kit, (*kit).windows.request_help(window, &handled), GF_STATUS_DISPOSED,
                  "a disposed window refuses request_help");
    gf_kit_status(kit, (*kit).events.subscribe(window, gf_kit_text("help_requested"), &unused, &refused),
                  GF_STATUS_DISPOSED, "a disposed window refuses new subscriptions");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the events window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the events window's root");
    gf_kit_check(kit, probe.released == 1u, "releasing the window releases nothing more");
}

/* A-gf-abi-03-F1: a help_requested callback that disposes its window (one
 * that has not run, so the dispose destroys it) and leaves handled unset.
 * request_help must not read the destroyed window after the callback; it
 * reports the request unanswered. Under AddressSanitizer the old read of
 * the window's help fallbacks was a use after free. */
static void window_help_disposed(gf_kit* kit) {
    const gf_object root = gf_kit_create(kit, "Panel", "kit.window-help-disposed");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window to dispose in its help");
    help_probe probe;
    memset(&probe, 0, sizeof(probe));
    probe.kit = kit;
    probe.dispose_window = window;
    probe.dispose_status = GF_STATUS_INTERNAL;
    const gf_callback help = gf_kit_callback(&help_invoke, &help_release, &probe);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(window, gf_kit_text("help_requested"), &help, &subscription),
                  GF_STATUS_OK, "subscribe to the help of a window the callback disposes");
    uint32_t handled = 7u;
    gf_kit_status(kit, (*kit).windows.request_help(window, &handled), GF_STATUS_OK,
                  "request help of a window its callback disposes");
    gf_kit_check(kit, probe.invoked == 1u && probe.dispose_status == GF_STATUS_OK,
                 "the callback ran once and disposed the window");
    gf_kit_check(kit, handled == 0u, "a window disposed in its help callback reports the request unanswered");
    gf_kit_status(kit, (*kit).windows.request_help(window, &handled), GF_STATUS_DISPOSED,
                  "the disposed window refuses request_help");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the disposed window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the disposed window's root");
    gf_kit_check(kit, probe.released == 1u, "the subscription's callback was released once");
}

void gf_kit_window_events(gf_kit* kit) {
    window_listing(kit);
    window_help(kit);
    window_help_disposed(kit);
}
