/* Kit part: components through create, and create_with (stage C5). */

#include "gf_conformance_internal.h"

#include <string.h>

/* The types create refuses: those that need another object (create_with
 * makes them), the components a window owns, and the service. */
static const char* const refused_by_create[] = {
    "anchored_overlay_layer", "dock_site", "flags_value_editor", "page_strip",
    "color_command", "font_command", "gallery_command",
    "background_worker", "binding", "binding_context", "binding_source", "error_provider",
    "help_provider", "image_list", "notify_icon", "ribbon_tool_tip", "semantic_feedback",
    "timer", "tool_tip",
    "live_surface",
};

/* Of those, the ones neither slot makes: a binding (bindings.bind makes
 * one) and the service. */
static const char* const made_by_neither[] = {
    "binding",
    "live_surface",
};

/* Of those, the components bound to a window. */
static const char* const bound_to_window[] = {
    "background_worker", "binding_context", "binding_source", "error_provider", "help_provider", "image_list",
    "notify_icon", "ribbon_tool_tip", "semantic_feedback", "timer", "tool_tip",
};

static int listed(gf_utf8_view id, const char* const* names, size_t count) {
    size_t index = 0u;
    for (index = 0u; index < count; ++index) {
        if (gf_kit_view_equals(id, names[index])) {
            return 1;
        }
    }
    return 0;
}

int gf_kit_create_refuses(gf_utf8_view id) {
    const int refused = listed(id, refused_by_create, sizeof(refused_by_create) / sizeof(refused_by_create[0]));
    return refused;
}

/* A window over a new root, whose reference the window holds. */
static gf_object make_window(gf_kit* kit, const char* root_id) {
    gf_object root = 0u;
    if ((*kit).objects.create(gf_kit_text("panel"), gf_kit_text(root_id), &root) != GF_STATUS_OK) {
        return 0u;
    }
    gf_object window = 0u;
    const gf_status made = (*kit).windows.create(root, &window);
    (void)(*kit).objects.release(root);
    if (made != GF_STATUS_OK) {
        return 0u;
    }
    return window;
}

static gf_create_arguments blank_arguments(void) {
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    return arguments;
}

static const gf_create_choice sample_choices[] = {
    {{"none", 4u}, 0},
    {{"bold", 4u}, 1},
    {{"italic", 6u}, 2},
};

gf_object gf_kit_make_any(gf_kit* kit, gf_utf8_view id, const char* stable_id, gf_object* owner) {
    *owner = 0u;
    gf_object object = 0u;
    if (!gf_kit_create_refuses(id)) {
        if ((*kit).objects.create(id, gf_kit_text(stable_id), &object) != GF_STATUS_OK) {
            return 0u;
        }
        return object;
    }
    if (listed(id, made_by_neither, sizeof(made_by_neither) / sizeof(made_by_neither[0]))) {
        return 0u;
    }
    gf_create_arguments arguments = blank_arguments();
    const char* owner_type = NULL;
    if (listed(id, bound_to_window, sizeof(bound_to_window) / sizeof(bound_to_window[0]))) {
        *owner = make_window(kit, "kit.create.window");
        if (*owner == 0u) {
            return 0u;
        }
        arguments.owner = *owner;
    } else if (gf_kit_view_equals(id, "anchored_overlay_layer")) {
        owner_type = "button";
    } else if (gf_kit_view_equals(id, "dock_site")) {
        owner_type = "dock_workspace";
    } else if (gf_kit_view_equals(id, "page_strip")) {
        owner_type = "page_view";
    } else if (gf_kit_view_equals(id, "flags_value_editor")) {
        arguments.flags = GF_CREATE_FLAGS_COMBINABLE;
        arguments.enumeration = gf_kit_text("font_style");
        arguments.choices = sample_choices;
        arguments.choice_count = 3u;
        arguments.value = 3;
    } else {
        owner_type = "command";
    }
    if (owner_type != NULL) {
        if ((*kit).objects.create(gf_kit_text(owner_type), gf_kit_text("kit.create.owner"), owner) != GF_STATUS_OK) {
            return 0u;
        }
        arguments.owner = *owner;
    }
    if ((*kit).objects.create_with(id, gf_kit_text(stable_id), &arguments, &object) != GF_STATUS_OK) {
        return 0u;
    }
    return object;
}

static void components(gf_kit* kit) {
    const gf_object command = gf_kit_create(kit, "command", "kit.create.command");
    char buffer[32];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).objects.type_id(command, buffer, sizeof(buffer), &required), GF_STATUS_OK,
                  "a component's type id");
    gf_kit_check(kit, required == 7u && memcmp(buffer, "command", 7u) == 0, "a command's type id is command");
    gf_object_state state = GF_OBJECT_STATE_DISPOSED;
    gf_kit_status(kit, (*kit).objects.state(command, &state), GF_STATUS_OK, "a component's state");
    gf_kit_check(kit, state == GF_OBJECT_STATE_ALIVE, "a new component is alive");
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).objects.child_count(command, &count), GF_STATUS_WRONG_HANDLE_KIND,
                  "the tree slots refuse a component");
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[8];
    gf_kit_status(kit, (*kit).properties.get(command, gf_kit_text("text"), &value, text, sizeof(text), &required),
                  GF_STATUS_WRONG_HANDLE_KIND, "the properties table refuses a component");
    const gf_object workspace = gf_kit_create(kit, "dock_workspace", "kit.create.workspace");
    gf_kit_status(kit, (*kit).objects.stable_id(workspace, buffer, sizeof(buffer), &required), GF_STATUS_OK,
                  "a dock workspace's stable id");
    gf_kit_check(kit, required == 20u && memcmp(buffer, "kit.create.workspace", 20u) == 0,
                 "a component made from its id keeps it");
    gf_kit_status(kit, (*kit).objects.dispose(command), GF_STATUS_OK, "dispose a component");
    gf_kit_status(kit, (*kit).objects.state(command, &state), GF_STATUS_OK, "a disposed component's state");
    gf_kit_check(kit, state == GF_OBJECT_STATE_DISPOSED, "a disposed component reports it");
    gf_kit_status(kit, (*kit).objects.release(command), GF_STATUS_OK, "release the command");
    gf_kit_status(kit, (*kit).objects.release(workspace), GF_STATUS_OK, "release the workspace");
}

static void with_owners(gf_kit* kit) {
    const gf_object workspace = gf_kit_create(kit, "dock_workspace", "kit.with.workspace");
    gf_create_arguments arguments = blank_arguments();
    arguments.owner = workspace;
    gf_object site = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("dock_site"), gf_kit_text("kit.with.site"), &arguments,
                                                  &site),
                  GF_STATUS_OK, "a dock site over its workspace");
    gf_kit_status(kit, (*kit).objects.release(workspace), GF_STATUS_OK, "release the workspace's handle");
    gf_object_state state = GF_OBJECT_STATE_DISPOSED;
    gf_kit_status(kit, (*kit).objects.state(site, &state), GF_STATUS_OK, "the site lives on");
    gf_kit_check(kit, state == GF_OBJECT_STATE_ALIVE, "the site keeps its workspace");
    gf_kit_status(kit, (*kit).objects.release(site), GF_STATUS_OK, "release the site");

    const gf_object label = gf_kit_create(kit, "label", "kit.with.label");
    arguments.owner = label;
    gf_object wrong = 9u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("dock_site"), gf_kit_text("kit.with.wrong"), &arguments,
                                                  &wrong),
                  GF_STATUS_WRONG_HANDLE_KIND, "an owner of the wrong type is refused");
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("page_strip"), gf_kit_text("kit.with.wrong"), &arguments,
                                                  &wrong),
                  GF_STATUS_WRONG_HANDLE_KIND, "a page strip needs a page view or a navigation pane");
    arguments.owner = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("color_command"), gf_kit_text("kit.with.none"),
                                                  &arguments, &wrong),
                  GF_STATUS_INVALID_ARGUMENT, "a command extension needs its command");
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("timer"), gf_kit_text("kit.with.timer"), &arguments,
                                                  &wrong),
                  GF_STATUS_INVALID_ARGUMENT, "a timer needs its window");
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("binding"), gf_kit_text("kit.with.binding"),
                                                  &arguments, &wrong),
                  GF_STATUS_UNSUPPORTED_CAPABILITY, "a binding is made by bindings.bind");
    gf_kit_check(kit, wrong == 9u, "a refused create_with leaves its output");
    gf_object plain = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("label"), gf_kit_text("kit.with.plain"), &arguments,
                                                  &plain),
                  GF_STATUS_OK, "a type create makes is made by create_with with no arguments");
    gf_kit_status(kit, (*kit).objects.release(plain), GF_STATUS_OK, "release the plain label");
    arguments.owner = label;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("label"), gf_kit_text("kit.with.plain"), &arguments,
                                                  &plain),
                  GF_STATUS_INVALID_ARGUMENT, "a type that takes no owner refuses one");
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("timer"), gf_kit_text("kit.with.timer"), &arguments,
                                                  &wrong),
                  GF_STATUS_WRONG_HANDLE_KIND, "a timer's owner is a window");
    gf_kit_status(kit, (*kit).objects.release(label), GF_STATUS_OK, "release the label");
}

static void windows(gf_kit* kit) {
    const gf_object root = gf_kit_create(kit, "panel", "kit.window.root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window over a root");
    gf_object second = 7u;
    gf_kit_status(kit, (*kit).windows.create(root, &second), GF_STATUS_INVALID_STATE,
                  "a root already in a window is refused");
    gf_kit_check(kit, second == 7u, "a refused create leaves its output");
    char buffer[32];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).objects.type_id(window, buffer, sizeof(buffer), &required), GF_STATUS_OK,
                  "a window's type id");
    gf_kit_check(kit, required == 6u && memcmp(buffer, "window", 6u) == 0, "a window's type id is window");
    gf_kit_status(kit, (*kit).objects.stable_id(window, buffer, sizeof(buffer), &required), GF_STATUS_OK,
                  "a window's stable id");
    gf_kit_check(kit, required == 15u && memcmp(buffer, "kit.window.root", 15u) == 0,
                 "a window's stable id is its root's");
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).objects.child_count(window, &count), GF_STATUS_WRONG_HANDLE_KIND,
                  "a window is not a control");

    gf_create_arguments arguments = blank_arguments();
    arguments.owner = window;
    gf_object timer = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("timer"), gf_kit_text("kit.window.timer"), &arguments,
                                                  &timer),
                  GF_STATUS_OK, "a timer bound to a window");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the window before its timer");
    gf_kit_check(kit, gf_kit_state(kit, timer) == GF_OBJECT_STATE_ALIVE, "the timer does not keep its window");
    gf_kit_status(kit, (*kit).objects.release(timer), GF_STATUS_OK, "release the unbound timer");

    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "a released window frees its root");
    gf_kit_status(kit, (*kit).objects.dispose(window), GF_STATUS_OK, "dispose a window that has not run");
    arguments.owner = window;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("tool_tip"), gf_kit_text("kit.window.tips"),
                                                  &arguments, &timer),
                  GF_STATUS_DISPOSED, "a disposed window binds nothing");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the disposed window");
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "a disposed window frees its root");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the last window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the window's root");
}

static void enumerations(gf_kit* kit) {
    gf_create_arguments arguments = blank_arguments();
    arguments.flags = GF_CREATE_FLAGS_COMBINABLE;
    arguments.enumeration = gf_kit_text("font_style");
    arguments.choices = sample_choices;
    arguments.choice_count = 3u;
    arguments.value = 2;
    gf_object editor = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("flags_value_editor"), gf_kit_text("kit.enum.editor"),
                                                  &arguments, &editor),
                  GF_STATUS_OK, "a flags editor over the caller's enumeration");
    gf_kit_status(kit, (*kit).objects.release(editor), GF_STATUS_OK, "release the flags editor");

    gf_create_choice repeated[2];
    memcpy(&repeated[0], &sample_choices[1], sizeof(repeated[0]));
    memcpy(&repeated[1], &sample_choices[1], sizeof(repeated[1]));
    arguments.choices = repeated;
    arguments.choice_count = 2u;
    gf_object refused = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("flags_value_editor"), gf_kit_text("kit.enum.bad"),
                                                  &arguments, &refused),
                  GF_STATUS_INVALID_ARGUMENT, "repeated choice names are refused");
    arguments.choice_count = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("flags_value_editor"), gf_kit_text("kit.enum.bad"),
                                                  &arguments, &refused),
                  GF_STATUS_INVALID_ARGUMENT, "an enumeration with a name and no choices is refused");
    arguments.flags = 8u;
    arguments.choices = sample_choices;
    arguments.choice_count = 3u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("flags_value_editor"), gf_kit_text("kit.enum.bad"),
                                                  &arguments, &refused),
                  GF_STATUS_INVALID_ARGUMENT, "unknown flags are refused");
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("flags_value_editor"), gf_kit_text("kit.enum.bad"),
                                                  NULL, &refused),
                  GF_STATUS_INVALID_ARGUMENT, "create_with needs its arguments");
    gf_kit_check(kit, refused == 0u, "refused enumerations make nothing");
}

void gf_kit_create_with(gf_kit* kit) {
    components(kit);
    with_owners(kit);
    windows(kit);
    enumerations(kit);
}
