/* Kit part: a window's properties (since 1.1): the schema's "window" type,
 * properties.get, set and reset on a window from windows.create, and
 * windows.window_properties. The refusals and values need no window run;
 * the window part writes properties before and during one run. */

#include "gf_conformance_internal.h"

#include <stddef.h>
#include <string.h>

/* A 1x1 RGBA PNG. */
static const uint8_t icon_png[70] = {
    0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00,
    0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8, 0xcf, 0xc0, 0xf0, 0x1f, 0x00, 0x05, 0x00, 0x01, 0xff,
    0x89, 0x99, 0x3d, 0x1d, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82,
};

/* The windows table size that carries window_properties. */
static int windows_have_properties(const gf_kit* kit) {
    const int present =
        (*kit).windows.struct_size >= offsetof(gf_windows_table, window_properties) + sizeof(void*) &&
        (*kit).windows.window_properties != NULL;
    return present;
}

static gf_value blank_value(void) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    return value;
}

static gf_value text_value(const char* text) {
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_TEXT;
    value.text = gf_kit_text(text);
    return value;
}

static gf_value number_value(double number) {
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_NUMBER;
    value.number = number;
    return value;
}

static gf_value boolean_value(uint32_t boolean) {
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_BOOLEAN;
    value.boolean = boolean;
    return value;
}

static gf_value enumeration_value(int64_t number) {
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_ENUMERATION;
    value.signed_integer = number;
    return value;
}

static gf_value size_value(double width, double height) {
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_SIZE;
    value.size.width = width;
    value.size.height = height;
    return value;
}

static gf_value image_value(uint64_t image) {
    gf_value value = blank_value();
    value.kind = GF_VALUE_KIND_IMAGE;
    value.unsigned_integer = image;
    return value;
}

/* Reads a window property into value, its text into text. */
static gf_status read_property(gf_kit* kit, gf_object window, const char* name, gf_value* value, char* text,
                               uint64_t capacity) {
    *value = blank_value();
    uint64_t required = 0u;
    const gf_status status = (*kit).properties.get(window, gf_kit_text(name), value, text, capacity, &required);
    return status;
}

/* The schema lists the window type after the registry's types. */
static void window_schema(gf_kit* kit) {
    uint64_t count = 0u;
    uint64_t window_type = 0u;
    uint64_t display = 1u;
    gf_kit_status(kit, (*kit).schema.type_count(&count), GF_STATUS_OK, "type_count for the window type");
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text("window"), &window_type), GF_STATUS_OK,
                  "find the window type");
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text("Window"), &display), GF_STATUS_OK,
                  "find the window type by its display spelling");
    gf_kit_check(kit, window_type == display && window_type + 1u == count, "the window type is the last type");
    gf_schema_type record;
    memset(&record, 0, sizeof(record));
    record.struct_size = (uint32_t)sizeof(record);
    gf_kit_status(kit, (*kit).schema.type_at(window_type, &record), GF_STATUS_OK, "the window type's record");
    gf_kit_check(kit, record.kind == GF_SCHEMA_KIND_COMPONENT && gf_kit_view_equals(record.type_name, "Window") &&
                          record.property_count == 12u && record.interaction_count == 11u,
                 "the window type is a component with twelve properties and eleven interactions");
    uint64_t index = 0u;
    uint64_t found = 0u;
    for (index = 0u; index < record.property_count; ++index) {
        gf_schema_property property;
        memset(&property, 0, sizeof(property));
        property.struct_size = (uint32_t)sizeof(property);
        if ((*kit).schema.property_at(window_type, index, &property) != GF_STATUS_OK) {
            continue;
        }
        if (gf_kit_view_equals(property.name, "form_border_style") &&
            property.kind == GF_VALUE_KIND_ENUMERATION && property.choice_count == 7u &&
            gf_kit_view_equals(property.enumeration, "form_border_style")) {
            ++found;
        }
        if (gf_kit_view_equals(property.name, "opacity") && property.kind == GF_VALUE_KIND_NUMBER &&
            (property.flags & GF_SCHEMA_PROPERTY_FLAGS_WRITABLE) != 0u) {
            ++found;
        }
    }
    gf_kit_check(kit, found == 2u, "form_border_style is a seven-choice enumeration, opacity a writable number");
    gf_object none = 0u;
    gf_kit_status(kit, (*kit).objects.create(gf_kit_text("window"), gf_kit_text("kit.properties.none"), &none),
                  GF_STATUS_UNSUPPORTED_CAPABILITY, "objects.create does not make a window");
    gf_kit_check(kit, none == 0u, "a refused create leaves no object");
}

/* Values and refusals on a window that has not run. */
void gf_kit_window_properties_refusals(gf_kit* kit) {
    gf_kit_check(kit, windows_have_properties(kit), "the windows table has window_properties");
    if (!windows_have_properties(kit)) {
        return;
    }
    window_schema(kit);
    const gf_object root = gf_kit_create(kit, "Panel", "kit.properties");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window for its properties");
    char text[64];
    gf_value value = blank_value();

    gf_kit_status(kit, read_property(kit, window, "text", &value, text, sizeof(text)), GF_STATUS_OK,
                  "read the window text");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_TEXT && value.text.size == 0u, "the window text starts empty");
    value = text_value("Settings");
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("text"), &value), GF_STATUS_OK,
                  "write the window text");
    gf_kit_status(kit, read_property(kit, window, "text", &value, text, sizeof(text)), GF_STATUS_OK,
                  "read the written text");
    gf_kit_check(kit, value.text.size == 8u && memcmp(text, "Settings", 8u) == 0, "the text reads back");

    value = number_value(0.5);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("opacity"), &value), GF_STATUS_OK,
                  "write the opacity");
    value = number_value(1.5);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("opacity"), &value), GF_STATUS_INVALID_ARGUMENT,
                  "an opacity above 1 is refused");
    value = text_value("half");
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("opacity"), &value), GF_STATUS_INVALID_ARGUMENT,
                  "an opacity of another kind is refused");
    gf_kit_status(kit, read_property(kit, window, "opacity", &value, text, sizeof(text)), GF_STATUS_OK,
                  "read the opacity");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_NUMBER && value.number == 0.5, "a refused write changes nothing");
    gf_kit_status(kit, (*kit).properties.reset(window, gf_kit_text("opacity")), GF_STATUS_OK, "reset the opacity");
    gf_kit_status(kit, read_property(kit, window, "opacity", &value, text, sizeof(text)), GF_STATUS_OK,
                  "read the reset opacity");
    gf_kit_check(kit, value.number == 1.0, "reset makes the window opaque again");

    value = enumeration_value(3);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("form_border_style"), &value), GF_STATUS_OK,
                  "write a fixed dialog frame");
    gf_kit_status(kit, read_property(kit, window, "form_border_style", &value, text, sizeof(text)), GF_STATUS_OK,
                  "read the frame");
    gf_kit_check(kit, value.kind == GF_VALUE_KIND_ENUMERATION && value.signed_integer == 3 &&
                          value.text.size == 12u && memcmp(text, "fixed_dialog", 12u) == 0,
                 "the frame reads back with its choice's name");
    value = enumeration_value(9);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("form_border_style"), &value),
                  GF_STATUS_INVALID_ARGUMENT, "an undefined frame is refused");
    value = enumeration_value(2);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("window_state"), &value), GF_STATUS_OK,
                  "ask for a maximized window");
    value = boolean_value(0u);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("maximize_box"), &value), GF_STATUS_OK,
                  "remove the maximize box");
    value = size_value(320.0, 240.0);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("minimum_size"), &value), GF_STATUS_OK,
                  "write a minimum size");
    value = size_value(-1.0, 240.0);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("minimum_size"), &value),
                  GF_STATUS_INVALID_ARGUMENT, "a negative minimum size is refused");

    uint64_t image = 0u;
    gf_kit_status(kit, (*kit).themes.load_image(icon_png, sizeof(icon_png), &image), GF_STATUS_OK,
                  "load the icon image");
    value = image_value(image);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("icon"), &value), GF_STATUS_OK,
                  "write the icon");
    value = image_value(image + 100000u);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("icon"), &value), GF_STATUS_INVALID_ARGUMENT,
                  "an image that is not loaded is refused");

    value = boolean_value(1u);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("no_such_property"), &value), GF_STATUS_NOT_FOUND,
                  "a window has no property of an unknown name");
    uint64_t required = 0u;
    gf_kit_status(kit,
                  (*kit).properties.element_at(window, gf_kit_text("text"), 0u, &value, text, sizeof(text), &required),
                  GF_STATUS_INVALID_ARGUMENT, "no window property is a collection");

    gf_window_property_flags supported = 99u;
    gf_window_property_flags applied = 99u;
    gf_kit_status(kit, (*kit).windows.window_properties(window, &supported, &applied), GF_STATUS_OK,
                  "window_properties before the run");
    gf_kit_check(kit, supported == 0u && applied == 0u, "nothing is supported or applied before the run");
    gf_kit_status(kit, (*kit).windows.window_properties(root, &supported, &applied), GF_STATUS_WRONG_HANDLE_KIND,
                  "window_properties takes a window");

    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the property window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the property window's root");
}

/* The window part: what the ready callback saw. */
typedef struct properties_run {
    gf_kit* kit;
    gf_status report;
    gf_status write;
    gf_status close;
    gf_window_property_flags supported;
    gf_window_property_flags applied;
    gf_window_property_flags applied_after_write;
} properties_run;

static void GF_CALL on_properties_ready(void* context, gf_event* event) {
    properties_run* run = (properties_run*)context;
    gf_kit* kit = (*run).kit;
    const gf_object window = (*event).sender;
    (*run).report = (*kit).windows.window_properties(window, &(*run).supported, &(*run).applied);
    const gf_value opacity = number_value(0.75);
    (*run).write = (*kit).properties.set(window, gf_kit_text("opacity"), &opacity);
    (void)(*kit).windows.window_properties(window, NULL, &(*run).applied_after_write);
    (*run).close = (*kit).windows.request_close(window);
}

/* Properties written before the run reach the host when it starts; one
 * written during the run reaches it at once. */
void gf_kit_window_properties(gf_kit* kit) {
    if (!windows_have_properties(kit)) {
        return;
    }
    properties_run run;
    memset(&run, 0, sizeof(run));
    run.kit = kit;
    run.report = -1;
    run.write = -1;
    run.close = -1;
    const gf_object root = gf_kit_create(kit, "Panel", "kit.properties.run");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "create a window to run");
    gf_value value = text_value("GUI.Forms C ABI window properties");
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("text"), &value), GF_STATUS_OK,
                  "title the window before its run");
    value = boolean_value(0u);
    gf_kit_status(kit, (*kit).properties.set(window, gf_kit_text("minimize_box"), &value), GF_STATUS_OK,
                  "remove the minimize box before the run");
    const gf_callback ready = gf_kit_callback(&on_properties_ready, NULL, &run);
    gf_run_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.title = gf_kit_text("not the window text");
    options.ready = &ready;
    gf_kit_status(kit, (*kit).windows.run(window, &options), GF_STATUS_OK, "run a window with properties");
    gf_kit_status(kit, run.report, GF_STATUS_OK, "window_properties during the run");
    gf_kit_check(kit, (run.supported & GF_WINDOW_PROPERTY_TEXT) != 0u &&
                          (run.supported & GF_WINDOW_PROPERTY_OPACITY) != 0u,
                 "every host applies the text and the opacity");
    gf_kit_check(kit, (run.applied & GF_WINDOW_PROPERTY_TEXT) != 0u &&
                          (run.applied & GF_WINDOW_PROPERTY_MINIMIZE_BOX) ==
                              (run.supported & GF_WINDOW_PROPERTY_MINIMIZE_BOX),
                 "the properties written before the run were applied when it started");
    gf_kit_check(kit, (run.applied & GF_WINDOW_PROPERTY_OPACITY) == 0u, "an unwritten property is not applied");
    gf_kit_status(kit, run.write, GF_STATUS_OK, "write the opacity during the run");
    gf_kit_check(kit, (run.applied_after_write & GF_WINDOW_PROPERTY_OPACITY) != 0u,
                 "a write during the run is applied at once");
    gf_kit_status(kit, run.close, GF_STATUS_OK, "close the property window");
    gf_value read = blank_value();
    char text[8];
    gf_kit_status(kit, read_property(kit, window, "text", &read, text, sizeof(text)), GF_STATUS_DISPOSED,
                  "a window's properties end with its run");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the run's window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the run's root");
}
