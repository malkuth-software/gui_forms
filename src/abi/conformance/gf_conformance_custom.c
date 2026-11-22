/* Kit part: custom controls (GF_TABLE_CUSTOM_CONTROLS), composite controls
 * (since 1.5) and the schema table's listing of them (provisional 1.5),
 * controls.invalidate and, where the library has it, the Windows paint endpoint
 * (GF_TABLE_WINDOWS_PAINT_ENDPOINT). Their paint path is checked by the C++ program
 * tests/abi/custom/gf_abi_custom_paint.cpp, which reaches a window's paint. */

#include "gf_conformance_internal.h"

#include <stddef.h>
#include <string.h>

/* The caller's side of one custom control: the values its properties hold
 * and what the library asked of it. */
typedef struct dial_state {
    double value;
    int64_t mode;
    uint32_t gets;
    uint32_t sets;
    uint32_t paints;
    uint32_t releases;
    gf_object last_control;
} dial_state;

enum { dial_value_index = 0, dial_mode_index = 1, dial_caption_index = 2 };

static void GF_CALL dial_paint(void* context, gf_object control, const gf_paint_target* target) {
    dial_state* state = (dial_state*)context;
    (void)target;
    ++(*state).paints;
    (*state).last_control = control;
}

static gf_status GF_CALL dial_get(void* context, gf_object control, uint64_t index, gf_value* value) {
    dial_state* state = (dial_state*)context;
    ++(*state).gets;
    (*state).last_control = control;
    if (index == dial_value_index) {
        (*value).kind = GF_VALUE_KIND_NUMBER;
        (*value).number = (*state).value;
        return GF_STATUS_OK;
    }
    if (index == dial_mode_index) {
        (*value).kind = GF_VALUE_KIND_ENUMERATION;
        (*value).signed_integer = (*state).mode;
        return GF_STATUS_OK;
    }
    (*value).kind = GF_VALUE_KIND_TEXT;
    (*value).text = gf_kit_text("Dial");
    return GF_STATUS_OK;
}

static gf_status GF_CALL dial_set(void* context, gf_object control, uint64_t index, const gf_value* value) {
    dial_state* state = (dial_state*)context;
    ++(*state).sets;
    (*state).last_control = control;
    if (index == dial_value_index) {
        if ((*value).kind != GF_VALUE_KIND_NUMBER || (*value).number < 0.0) {
            return GF_STATUS_INVALID_ARGUMENT;
        }
        (*state).value = (*value).number;
        return GF_STATUS_OK;
    }
    if (index == dial_mode_index && (*value).kind == GF_VALUE_KIND_ENUMERATION) {
        (*state).mode = (*value).signed_integer;
        return GF_STATUS_OK;
    }
    return GF_STATUS_INVALID_ARGUMENT;
}

static void GF_CALL dial_release(void* context) {
    ++(*(dial_state*)context).releases;
}

static gf_custom_control_spec dial_spec(dial_state* state, gf_custom_property* properties,
                                        gf_create_choice* choices, gf_value* default_value) {
    memset(default_value, 0, sizeof(*default_value));
    (*default_value).struct_size = (uint32_t)sizeof(*default_value);
    (*default_value).kind = GF_VALUE_KIND_NUMBER;
    (*default_value).number = 1.0;
    choices[0].name = gf_kit_text("linear");
    choices[0].value = 0;
    choices[1].name = gf_kit_text("circular");
    choices[1].value = 1;
    memset(properties, 0, 3u * sizeof(*properties));
    properties[0].name = gf_kit_text("DialValue");
    properties[0].category = gf_kit_text("Behavior");
    properties[0].description = gf_kit_text("Where the dial points.");
    properties[0].kind = GF_VALUE_KIND_NUMBER;
    properties[0].flags = GF_CUSTOM_PROPERTY_CHANGE_NOTIFICATIONS;
    properties[0].default_value = default_value;
    properties[1].name = gf_kit_text("dial_mode");
    properties[1].kind = GF_VALUE_KIND_ENUMERATION;
    properties[1].enumeration = gf_kit_text("dial_mode");
    properties[1].choices = choices;
    properties[1].choice_count = 2u;
    properties[2].name = gf_kit_text("caption");
    properties[2].kind = GF_VALUE_KIND_TEXT;
    properties[2].flags = GF_CUSTOM_PROPERTY_READ_ONLY;
    gf_custom_control_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.struct_size = (uint32_t)sizeof(spec);
    spec.base = GF_CUSTOM_BASE_USER_CONTROL;
    spec.flags = GF_CUSTOM_CONTROL_FOCUSABLE;
    spec.property_size = (uint32_t)sizeof(gf_custom_property);
    spec.properties = properties;
    spec.property_count = 3u;
    spec.callbacks.struct_size = (uint32_t)sizeof(spec.callbacks);
    spec.callbacks.context = state;
    spec.callbacks.paint = &dial_paint;
    spec.callbacks.get_property = &dial_get;
    spec.callbacks.set_property = &dial_set;
    spec.callbacks.release = &dial_release;
    return spec;
}

static gf_value number_value(double number) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    value.kind = GF_VALUE_KIND_NUMBER;
    value.number = number;
    return value;
}

static void refusals(gf_kit* kit) {
    dial_state state;
    memset(&state, 0, sizeof(state));
    gf_custom_property properties[3];
    gf_create_choice choices[2];
    gf_value default_value;
    gf_object object = 0u;
    gf_custom_control_spec spec = dial_spec(&state, properties, choices, &default_value);
    gf_kit_status(kit, (*kit).custom_controls.create(NULL, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "create needs a spec");
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text(""), &object), GF_STATUS_INVALID_ARGUMENT,
                  "create needs a stable id");
    spec.base = 2u;
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "an unknown base is refused");
    spec.base = GF_CUSTOM_BASE_SCROLLABLE_CONTROL;
    properties[2].name = gf_kit_text("dial_value");
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "property names are distinct in any spelling");
    properties[2].name = gf_kit_text("bounds");
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "a base's property is not declared again");
    properties[2].name = gf_kit_text("caption");
    properties[2].kind = GF_VALUE_KIND_OBJECT;
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "an OBJECT property is refused");
    properties[2].kind = GF_VALUE_KIND_TEXT;
    properties[1].choice_count = 0u;
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "an ENUMERATION property needs choices");
    properties[1].choice_count = 2u;
    default_value.number = -1.0;
    default_value.kind = GF_VALUE_KIND_TEXT;
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "a default of another kind is refused");
    default_value.kind = GF_VALUE_KIND_NUMBER;
    spec.callbacks.set_property = NULL;
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "writable properties need set_property");
    spec.callbacks.set_property = &dial_set;
    spec.property_size = 8u;
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.refused"), &object),
                  GF_STATUS_INVALID_ARGUMENT, "a short property_size is refused");
    gf_kit_check(kit, object == 0u && state.releases == 0u && state.gets == 0u,
                 "a refused create makes nothing and calls no callback");
}

static void properties_and_changes(gf_kit* kit) {
    dial_state state;
    memset(&state, 0, sizeof(state));
    gf_custom_property properties[3];
    gf_create_choice choices[2];
    gf_value default_value;
    gf_object dial = 0u;
    const gf_custom_control_spec spec = dial_spec(&state, properties, choices, &default_value);
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.dial"), &dial), GF_STATUS_OK,
                  "create a custom control");
    char type_buffer[64];
    uint64_t type_size = 0u;
    gf_kit_status(kit, (*kit).objects.type_id(dial, type_buffer, sizeof(type_buffer), &type_size), GF_STATUS_OK,
                  "read the custom control's type");
    gf_kit_check(kit, type_size == 12u && memcmp(type_buffer, "user_control", 12u) == 0,
                 "a custom control's schema type is its base's");

    gf_value set = number_value(3.5);
    gf_kit_status(kit, (*kit).properties.set(dial, gf_kit_text("dial_value"), &set), GF_STATUS_OK,
                  "set a custom property");
    gf_kit_check(kit, state.value == 3.5 && state.last_control == dial, "set_property receives the value");
    set.number = -2.0;
    gf_kit_status(kit, (*kit).properties.set(dial, gf_kit_text("DialValue"), &set), GF_STATUS_INVALID_ARGUMENT,
                  "a value set_property refuses is refused");
    gf_value got;
    memset(&got, 0, sizeof(got));
    got.struct_size = (uint32_t)sizeof(got);
    char text[32];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).properties.get(dial, gf_kit_text("dial_value"), &got, text, sizeof(text), &required),
                  GF_STATUS_OK, "get a custom property");
    gf_kit_check(kit, got.kind == GF_VALUE_KIND_NUMBER && got.number == 3.5, "get_property gives the value");

    gf_value mode;
    memset(&mode, 0, sizeof(mode));
    mode.struct_size = (uint32_t)sizeof(mode);
    mode.kind = GF_VALUE_KIND_ENUMERATION;
    mode.signed_integer = 1;
    gf_kit_status(kit, (*kit).properties.set(dial, gf_kit_text("dial_mode"), &mode), GF_STATUS_OK,
                  "set a custom enumeration");
    mode.signed_integer = 7;
    gf_kit_status(kit, (*kit).properties.set(dial, gf_kit_text("dial_mode"), &mode), GF_STATUS_INVALID_ARGUMENT,
                  "a value outside the enumeration is refused before set_property");
    memset(&got, 0, sizeof(got));
    got.struct_size = (uint32_t)sizeof(got);
    gf_kit_status(kit, (*kit).properties.get(dial, gf_kit_text("dial_mode"), &got, text, sizeof(text), &required),
                  GF_STATUS_OK, "get a custom enumeration");
    gf_kit_check(kit, got.signed_integer == 1 && gf_kit_view_equals(got.text, "circular"),
                 "the enumeration reads back by value and name");

    memset(&got, 0, sizeof(got));
    got.struct_size = (uint32_t)sizeof(got);
    gf_kit_status(kit, (*kit).properties.get(dial, gf_kit_text("caption"), &got, text, sizeof(text), &required),
                  GF_STATUS_OK, "get a read-only custom property");
    gf_kit_check(kit, got.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(got.text, "Dial"),
                 "the text get_property lends is copied");
    gf_value caption;
    memset(&caption, 0, sizeof(caption));
    caption.struct_size = (uint32_t)sizeof(caption);
    caption.kind = GF_VALUE_KIND_TEXT;
    caption.text = gf_kit_text("Other");
    gf_kit_check(kit, (*kit).properties.set(dial, gf_kit_text("caption"), &caption) != GF_STATUS_OK,
                 "a read-only custom property is not set");
    gf_kit_status(kit, (*kit).properties.reset(dial, gf_kit_text("dial_value")), GF_STATUS_OK,
                  "a custom property with a default resets");
    gf_kit_check(kit, state.value == 1.0, "reset sets the default through set_property");

    gf_kit_recorder recorder;
    memset(&recorder, 0, sizeof(recorder));
    recorder.kit = kit;
    const gf_callback callback = gf_kit_recording_callback(&recorder);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe_property(dial, gf_kit_text("dial_value"), &callback, &subscription),
                  GF_STATUS_OK, "subscribe to a custom property");
    gf_kit_status(kit, (*kit).custom_controls.notify_changed(dial, dial_value_index), GF_STATUS_OK,
                  "notify a change");
    gf_kit_check(kit, recorder.invoked == 1u && recorder.last_sender == dial, "the subscriber hears the change");
    gf_kit_status(kit, (*kit).custom_controls.notify_changed(dial, dial_caption_index), GF_STATUS_INVALID_ARGUMENT,
                  "a property without notifications is refused");
    gf_kit_status(kit, (*kit).custom_controls.notify_changed(dial, 3u), GF_STATUS_INVALID_ARGUMENT,
                  "an index past the properties is refused");
    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_OK, "disconnect");

    const gf_object panel = gf_kit_create(kit, "panel", "kit.custom.panel");
    gf_kit_status(kit, (*kit).custom_controls.notify_changed(panel, 0u), GF_STATUS_WRONG_HANDLE_KIND,
                  "a stock control is not a custom control");
    gf_logical_rect damage;
    damage.x = 1.0;
    damage.y = 2.0;
    damage.width = 10.0;
    damage.height = 4.0;
    gf_kit_status(kit, (*kit).controls.invalidate(dial, &damage), GF_STATUS_OK, "invalidate part of a control");
    gf_kit_status(kit, (*kit).controls.invalidate(panel, NULL), GF_STATUS_OK, "invalidate a whole control");
    damage.width = -1.0;
    gf_kit_status(kit, (*kit).controls.invalidate(dial, &damage), GF_STATUS_INVALID_ARGUMENT,
                  "a negative damage size is refused");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the panel");

    gf_kit_check(kit, state.releases == 0u, "a live custom control keeps its callbacks");
    gf_kit_status(kit, (*kit).objects.dispose(dial), GF_STATUS_OK, "dispose the custom control");
    gf_kit_check(kit, state.releases == 1u, "disposal releases the callbacks");
    const uint32_t gets = state.gets;
    memset(&got, 0, sizeof(got));
    got.struct_size = (uint32_t)sizeof(got);
    gf_kit_check(kit,
                 (*kit).properties.get(dial, gf_kit_text("dial_value"), &got, text, sizeof(text), &required) !=
                     GF_STATUS_OK,
                 "a disposed custom control is not read");
    gf_kit_check(kit, state.gets == gets, "no callback runs after release");
    gf_kit_status(kit, (*kit).objects.release(dial), GF_STATUS_OK, "release the custom control");
    gf_kit_check(kit, state.releases == 1u, "release is called once");

    /* A control never disposed releases its callbacks when it is destroyed. */
    dial_state second;
    memset(&second, 0, sizeof(second));
    gf_custom_control_spec scrollable = dial_spec(&second, properties, choices, &default_value);
    scrollable.base = GF_CUSTOM_BASE_SCROLLABLE_CONTROL;
    gf_object other = 0u;
    gf_kit_status(kit, (*kit).custom_controls.create(&scrollable, gf_kit_text("kit.custom.other"), &other),
                  GF_STATUS_OK, "create a scrollable custom control");
    gf_kit_status(kit, (*kit).objects.release(other), GF_STATUS_OK, "release its only reference");
    gf_kit_check(kit, second.releases == 1u, "destruction releases the callbacks");
}

static void paint_endpoint(gf_kit* kit) {
    gf_host_capabilities capabilities;
    memset(&capabilities, 0, sizeof(capabilities));
    capabilities.struct_size = (uint32_t)sizeof(capabilities);
    gf_kit_status(kit, (*kit).host.capabilities(&capabilities), GF_STATUS_OK, "read the capabilities");
    const int reported = (capabilities.available & GF_HOST_CAPABILITY_WINDOWS_PAINT_ENDPOINT) != 0u;
    const gf_status loaded =
        gf_abi_get_table(GF_TABLE_WINDOWS_PAINT_ENDPOINT, GF_ABI_MAJOR, GF_ABI_MINOR, &(*kit).windows_paint_endpoint,
                         sizeof((*kit).windows_paint_endpoint));
    gf_kit_check(kit, (loaded == GF_STATUS_OK) == reported,
                 "the paint endpoint table exists exactly where the capability is reported");
    if (loaded != GF_STATUS_OK) {
        gf_kit_status(kit, loaded, GF_STATUS_NOT_FOUND, "a library without the endpoint has no table");
        return;
    }
    dial_state state;
    memset(&state, 0, sizeof(state));
    gf_custom_property properties[3];
    gf_create_choice choices[2];
    gf_value default_value;
    gf_object control = 0u;
    const gf_custom_control_spec spec = dial_spec(&state, properties, choices, &default_value);
    gf_kit_status(kit, (*kit).custom_controls.create(&spec, gf_kit_text("kit.custom.endpoint"), &control),
                  GF_STATUS_OK, "create the endpoint's control");
    const gf_object panel = gf_kit_create(kit, "panel", "kit.custom.endpoint.panel");
    gf_paint_endpoint endpoint = 0u;
    uint64_t handle = 0u;
    gf_kit_status(kit, (*kit).windows_paint_endpoint.acquire(panel, 64u, 32u, &endpoint, &handle), GF_STATUS_WRONG_HANDLE_KIND,
                  "an endpoint binds only a custom control");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.acquire(control, 0u, 32u, &endpoint, &handle), GF_STATUS_INVALID_ARGUMENT,
                  "an endpoint has pixels");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.acquire(control, 64u, 32u, &endpoint, &handle), GF_STATUS_OK, "acquire");
    gf_kit_check(kit, endpoint != 0u && handle != 0u, "acquire gives a token and a handle");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.configure(endpoint, 80u, 40u), GF_STATUS_OK, "configure");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.touch(endpoint, 2u), GF_STATUS_INVALID_ARGUMENT, "explicit_boundary is 0 or 1");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.touch(endpoint, 1u), GF_STATUS_OK, "touch");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.drain(endpoint), GF_STATUS_OK, "drain");
    uint64_t required = 0u;
    char snapshot[4096];
    gf_kit_status(kit, (*kit).windows_paint_endpoint.snapshot(endpoint, snapshot, sizeof(snapshot), &required), GF_STATUS_OK,
                  "snapshot");
    gf_kit_check(kit, required <= sizeof(snapshot), "the snapshot fits");
    static uint8_t pixels[80u * 40u * 4u];
    memset(pixels, 0x40, sizeof(pixels));
    gf_kit_status(kit, (*kit).windows_paint_endpoint.submit_bgra(handle, 80u, 40u, 320u, pixels), GF_STATUS_OK, "submit pixels");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.submit_bgra(handle, 80u, 40u, 100u, pixels), GF_STATUS_INVALID_ARGUMENT,
                  "short rows are refused");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.submit_bgra(handle + 1u, 80u, 40u, 320u, pixels), GF_STATUS_STALE_HANDLE,
                  "another handle is not an endpoint's");
    uint64_t device_context = 0u;
    gf_kit_status(kit, (*kit).windows_paint_endpoint.get_dc(handle, &device_context), GF_STATUS_OK, "get the device context");
    gf_kit_check(kit, device_context != 0u, "the endpoint has a device context");
    gf_paint_write_lease lease = 0u;
    gf_kit_status(kit, (*kit).windows_paint_endpoint.begin_write(device_context, &lease), GF_STATUS_OK, "begin a write");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.end_write(lease, 1u), GF_STATUS_OK, "end the write and publish");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.end_write(lease, 1u), GF_STATUS_STALE_HANDLE, "a lease ends once");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.publish_dc(device_context), GF_STATUS_OK, "publish the device context");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.release_dc(handle, device_context), GF_STATUS_OK, "release the device context");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.release(endpoint), GF_STATUS_OK, "release the endpoint");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.release(endpoint), GF_STATUS_STALE_HANDLE, "an endpoint is released once");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.touch(endpoint, 0u), GF_STATUS_STALE_HANDLE, "a released endpoint is stale");
    gf_kit_status(kit, (*kit).windows_paint_endpoint.get_dc(handle, &device_context), GF_STATUS_STALE_HANDLE,
                  "a released endpoint's handle is stale");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the endpoint panel");
    gf_kit_status(kit, (*kit).objects.release(control), GF_STATUS_OK, "release the endpoint's control");
}

/* A composite control's document (since 1.5): a property its title child
 * shows, a boolean a command toggles (reached as the press semantic
 * action, so E912 does not refuse it) and whose change raises an event,
 * and a button child. */
static const char kit_card_document[] =
    "<control xmlns=\"urn:gui-forms:control:1\" name=\"kit-card\" version=\"1.0\"\n"
    "         base=\"user-control\" family=\"card\">\n"
    "  <property name=\"title\" kind=\"text\" default=\"\" category=\"Appearance\"/>\n"
    "  <property name=\"checked\" kind=\"boolean\" default=\"false\" category=\"Behavior\"\n"
    "            changed=\"checked-changed\"/>\n"
    "  <event name=\"checked-changed\" payload=\"boolean\"/>\n"
    "  <command name=\"toggle\" enabled=\"enabled(self)\" run=\"toggle(checked)\"\n"
    "           semantic-action=\"press\"/>\n"
    "  <part name=\"face\" role=\"face\" box=\"inset(region, 0)\"/>\n"
    "  <children>\n"
    "    <flex-panel id=\"@header\" dock=\"top\">\n"
    "      <label id=\"@title\"><bind property=\"text\" to=\"self.title\" mode=\"one-way\"/></label>\n"
    "      <button id=\"@action\" text=\"Go\"/>\n"
    "    </flex-panel>\n"
    "  </children>\n"
    "  <focus target=\"none\"/>\n"
    "  <semantics role=\"group\" name=\"property(title)\" children=\"exposed\"/>\n"
    "</control>\n";

static gf_composite_spec kit_card_spec(const gf_utf8_view* types, uint64_t count) {
    gf_composite_spec spec;
    memset(&spec, 0, sizeof(spec));
    spec.struct_size = (uint32_t)sizeof(spec);
    spec.file = gf_kit_text("kit-card.control");
    spec.document = gf_kit_text(kit_card_document);
    spec.child_types = types;
    spec.child_type_count = count;
    return spec;
}

/* Whether the schema table has the composite listings (provisional 1.5). */
static int lists_composites(const gf_kit* kit) {
    const int lists = (*kit).schema.struct_size >= offsetof(gf_schema_table, composite_command_at) + sizeof(void*);
    return lists;
}

/* The schema table's listing of the held kit-card definition: every
 * composite_* slot, with its refusals; listing receives the load's
 * identity (0 when it is not listed). */
static void composite_listing(gf_kit* kit, uint64_t* listing) {
    *listing = 0u;
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).schema.composite_count(NULL), GF_STATUS_INVALID_ARGUMENT,
                  "composite_count needs an output");
    gf_kit_status(kit, (*kit).schema.composite_count(&count), GF_STATUS_OK, "count the listed composites");
    gf_schema_composite record;
    memset(&record, 0, sizeof(record));
    record.struct_size = (uint32_t)sizeof(record);
    gf_kit_status(kit, (*kit).schema.composite_at(count, &record), GF_STATUS_INVALID_ARGUMENT,
                  "composite_at past the end is refused");
    record.struct_size = 8u;
    gf_kit_status(kit, (*kit).schema.composite_at(0u, &record), GF_STATUS_INVALID_ARGUMENT,
                  "composite_at refuses a short record");
    for (uint64_t index = 0u; index < count; ++index) {
        gf_schema_composite listed;
        memset(&listed, 0, sizeof(listed));
        listed.struct_size = (uint32_t)sizeof(listed);
        gf_kit_status(kit, (*kit).schema.composite_at(index, &listed), GF_STATUS_OK, "read a listed composite");
        if (gf_kit_view_equals(listed.name, "kit-card")) {
            record = listed;
        }
    }
    gf_kit_check(kit, gf_kit_view_equals(record.name, "kit-card"), "the schema lists the held definition");
    if (!gf_kit_view_equals(record.name, "kit-card")) {
        return;
    }
    const uint64_t composite = record.composite;
    *listing = composite;
    gf_kit_check(kit, composite != 0u && gf_kit_view_equals(record.id, "kit_card") &&
                          gf_kit_view_equals(record.version, "1.0") && record.slot.size == 0u,
                 "the listing names the load, its registered name and version, and no slot");
    gf_kit_check(kit, record.part_count == 3u && record.custom_state_count == 0u && record.property_count == 2u &&
                          record.event_count == 1u && record.command_count == 1u,
                 "the listing counts the region, focus and face parts, two properties, one event and one command");

    gf_schema_part part;
    memset(&part, 0, sizeof(part));
    part.struct_size = (uint32_t)sizeof(part);
    gf_kit_status(kit, (*kit).schema.composite_part_at(composite, 2u, &part), GF_STATUS_OK, "read the face part");
    gf_kit_check(kit, gf_kit_view_equals(part.name, "face") && gf_kit_view_equals(part.role, "face") &&
                          part.composed_from.size == 0u,
                 "the declared part follows region and focus");
    gf_kit_status(kit, (*kit).schema.composite_part_at(composite, record.part_count, &part),
                  GF_STATUS_INVALID_ARGUMENT, "composite_part_at past the end is refused");
    gf_utf8_view name;
    memset(&name, 0, sizeof(name));
    if (record.state_count != 0u) {
        gf_kit_status(kit, (*kit).schema.composite_state_at(composite, GF_SCHEMA_NONE, 0u, &name), GF_STATUS_OK,
                      "read the composite's first state");
        gf_kit_check(kit, name.size != 0u, "a state has a name");
    }
    gf_kit_status(kit, (*kit).schema.composite_state_at(composite, 2u, part.state_count, &name),
                  GF_STATUS_INVALID_ARGUMENT, "a part's state past the end is refused");
    gf_kit_status(kit, (*kit).schema.composite_state_at(composite, record.part_count, 0u, &name),
                  GF_STATUS_INVALID_ARGUMENT, "composite_state_at past the parts is refused");
    gf_kit_status(kit, (*kit).schema.composite_state_at(composite, GF_SCHEMA_NONE, 0u, NULL),
                  GF_STATUS_INVALID_ARGUMENT, "composite_state_at needs an output");
    gf_kit_status(kit, (*kit).schema.composite_custom_state_at(composite, 0u, &name), GF_STATUS_INVALID_ARGUMENT,
                  "the card declares no custom state");

    gf_schema_property property;
    memset(&property, 0, sizeof(property));
    property.struct_size = (uint32_t)sizeof(property);
    gf_kit_status(kit, (*kit).schema.composite_property_at(composite, 1u, &property), GF_STATUS_OK,
                  "read the checked property");
    gf_kit_check(kit, gf_kit_view_equals(property.name, "checked") && property.kind == GF_VALUE_KIND_BOOLEAN &&
                          gf_kit_view_equals(property.category, "Behavior") && property.choice_count == 0u &&
                          (property.flags & GF_SCHEMA_PROPERTY_FLAGS_READABLE) != 0u,
                 "a declared property is listed as property_at lists a type's");
    gf_kit_status(kit, (*kit).schema.composite_property_at(composite, 2u, &property), GF_STATUS_INVALID_ARGUMENT,
                  "composite_property_at past the end is refused");
    gf_schema_choice choice;
    memset(&choice, 0, sizeof(choice));
    choice.struct_size = (uint32_t)sizeof(choice);
    gf_kit_status(kit, (*kit).schema.composite_choice_at(composite, 1u, 0u, &choice), GF_STATUS_INVALID_ARGUMENT,
                  "a boolean property has no choice");

    gf_schema_interaction event;
    memset(&event, 0, sizeof(event));
    event.struct_size = (uint32_t)sizeof(event);
    gf_kit_status(kit, (*kit).schema.composite_event_at(composite, 0u, &event), GF_STATUS_OK,
                  "read the declared event");
    gf_kit_check(kit, gf_kit_view_equals(event.name, "checked-changed") &&
                          gf_kit_view_equals(event.payload, "value: boolean") && event.mutable_payload == 0u,
                 "the event is named as written, its payload in the ABI's words");
    gf_kit_status(kit, (*kit).schema.composite_event_at(composite, 1u, &event), GF_STATUS_INVALID_ARGUMENT,
                  "composite_event_at past the end is refused");
    gf_kit_status(kit, (*kit).schema.composite_command_at(composite, 0u, &name), GF_STATUS_OK,
                  "read the declared command");
    gf_kit_check(kit, gf_kit_view_equals(name, "toggle"), "the command is named as written");
    gf_kit_status(kit, (*kit).schema.composite_command_at(composite, 1u, &name), GF_STATUS_INVALID_ARGUMENT,
                  "composite_command_at past the end is refused");
    gf_kit_status(kit, (*kit).schema.composite_command_at(0u, 0u, &name), GF_STATUS_INVALID_ARGUMENT,
                  "an identity no load had is refused");
}

/* After the load is released: every composite_* slot refuses its identity
 * as removed, and the listing no longer holds it. */
static void composite_listing_released(gf_kit* kit, uint64_t composite) {
    gf_schema_part part;
    memset(&part, 0, sizeof(part));
    part.struct_size = (uint32_t)sizeof(part);
    gf_utf8_view name;
    memset(&name, 0, sizeof(name));
    gf_schema_property property;
    memset(&property, 0, sizeof(property));
    property.struct_size = (uint32_t)sizeof(property);
    gf_schema_choice choice;
    memset(&choice, 0, sizeof(choice));
    choice.struct_size = (uint32_t)sizeof(choice);
    gf_schema_interaction event;
    memset(&event, 0, sizeof(event));
    event.struct_size = (uint32_t)sizeof(event);
    gf_kit_status(kit, (*kit).schema.composite_part_at(composite, 0u, &part), GF_STATUS_REMOVED,
                  "a released load's part is refused");
    gf_kit_status(kit, (*kit).schema.composite_state_at(composite, GF_SCHEMA_NONE, 0u, &name), GF_STATUS_REMOVED,
                  "a released load's state is refused");
    gf_kit_status(kit, (*kit).schema.composite_custom_state_at(composite, 0u, &name), GF_STATUS_REMOVED,
                  "a released load's custom state is refused");
    gf_kit_status(kit, (*kit).schema.composite_property_at(composite, 0u, &property), GF_STATUS_REMOVED,
                  "a released load's property is refused");
    gf_kit_status(kit, (*kit).schema.composite_choice_at(composite, 0u, 0u, &choice), GF_STATUS_REMOVED,
                  "a released load's choice is refused");
    gf_kit_status(kit, (*kit).schema.composite_event_at(composite, 0u, &event), GF_STATUS_REMOVED,
                  "a released load's event is refused");
    gf_kit_status(kit, (*kit).schema.composite_command_at(composite, 0u, &name), GF_STATUS_REMOVED,
                  "a released load's command is refused");
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).schema.composite_count(&count), GF_STATUS_OK, "count the listed composites again");
    int listed = 0;
    for (uint64_t index = 0u; index < count; ++index) {
        gf_schema_composite record;
        memset(&record, 0, sizeof(record));
        record.struct_size = (uint32_t)sizeof(record);
        gf_kit_status(kit, (*kit).schema.composite_at(index, &record), GF_STATUS_OK, "read a listed composite");
        if (record.composite == composite) listed = 1;
    }
    gf_kit_check(kit, listed == 0, "a released load is no longer listed");
}

/* load_composite's refusals, then a composite made, listed by the schema
 * table while its definition is held (refused as removed after), its title
 * shown by its child, its command performed with its event heard, and its
 * child found; the definition released while the control lives. */
static void composites(gf_kit* kit) {
    if ((*kit).custom_controls.struct_size <
        offsetof(gf_custom_controls_table, composite_child) + sizeof(void*)) {
        return;
    }
    gf_utf8_view types[3];
    types[0] = gf_kit_text("flex_panel");
    types[1] = gf_kit_text("Label");
    types[2] = gf_kit_text("button");
    gf_object definition = 0u;
    gf_composite_spec spec = kit_card_spec(types, 3u);
    gf_kit_status(kit, (*kit).custom_controls.load_composite(NULL, &definition), GF_STATUS_INVALID_ARGUMENT,
                  "load_composite needs a spec");
    gf_kit_status(kit, (*kit).custom_controls.load_composite(&spec, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "load_composite needs a definition output");
    spec.reserved = 1u;
    gf_kit_status(kit, (*kit).custom_controls.load_composite(&spec, &definition), GF_STATUS_INVALID_ARGUMENT,
                  "a non-zero reserved field is refused");
    spec = kit_card_spec(types, 2u);
    gf_kit_status(kit, (*kit).custom_controls.load_composite(&spec, &definition), GF_STATUS_INVALID_ARGUMENT,
                  "a document naming a type not listed is refused");
    gf_utf8_view unknown[1];
    unknown[0] = gf_kit_text("no_such_type");
    spec = kit_card_spec(unknown, 1u);
    gf_kit_status(kit, (*kit).custom_controls.load_composite(&spec, &definition), GF_STATUS_INVALID_ARGUMENT,
                  "a child type the schema does not list is refused");
    gf_utf8_view component[1];
    component[0] = gf_kit_text("timer");
    spec = kit_card_spec(component, 1u);
    gf_kit_status(kit, (*kit).custom_controls.load_composite(&spec, &definition), GF_STATUS_INVALID_ARGUMENT,
                  "a child type that is not a control is refused");
    gf_kit_check(kit, definition == 0u, "a refusal gives no definition");

    spec = kit_card_spec(types, 3u);
    gf_kit_status(kit, (*kit).custom_controls.load_composite(&spec, &definition), GF_STATUS_OK,
                  "load a composite definition");
    gf_object card = 0u;
    gf_kit_status(kit, (*kit).custom_controls.create_composite(definition, gf_kit_text("Bad Id"), &card),
                  GF_STATUS_INVALID_ARGUMENT, "a malformed stable id is refused");
    const gf_object panel = gf_kit_create(kit, "panel", "kit.composite.panel");
    gf_kit_status(kit, (*kit).custom_controls.create_composite(panel, gf_kit_text("kit.composite.card"), &card),
                  GF_STATUS_WRONG_HANDLE_KIND, "create_composite takes a definition");
    gf_kit_status(kit, (*kit).custom_controls.create_composite(definition, gf_kit_text("kit.composite.card"), &card),
                  GF_STATUS_OK, "create a composite control");
    uint64_t listing = 0u;
    if (lists_composites(kit)) {
        composite_listing(kit, &listing);
    }
    gf_kit_status(kit, (*kit).objects.release(definition), GF_STATUS_OK,
                  "release the definition while its control lives");
    if (lists_composites(kit) && listing != 0u) {
        composite_listing_released(kit, listing);
    }

    char type_buffer[64];
    uint64_t type_size = 0u;
    gf_kit_status(kit, (*kit).objects.type_id(card, type_buffer, sizeof(type_buffer), &type_size), GF_STATUS_OK,
                  "read the composite's type");
    gf_kit_check(kit, type_size == 8u && memcmp(type_buffer, "kit_card", 8u) == 0,
                 "a composite's type id is its document's name");

    gf_kit_status(kit, gf_kit_set_text(kit, card, "title", "Hello"), GF_STATUS_OK, "set the composite's title");
    gf_object title = 0u;
    gf_kit_status(kit, (*kit).custom_controls.composite_child(card, gf_kit_text("title"), &title), GF_STATUS_OK,
                  "find the title child");
    char text[64];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).objects.stable_id(title, text, sizeof(text), &required), GF_STATUS_OK,
                  "read the child's stable id");
    gf_kit_check(kit, required == 24u && memcmp(text, "kit.composite.card.title", 24u) == 0,
                 "a child's stable id is the composite's with its local id");
    gf_value got;
    memset(&got, 0, sizeof(got));
    got.struct_size = (uint32_t)sizeof(got);
    gf_kit_status(kit, (*kit).properties.get(title, gf_kit_text("text"), &got, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the title child's text");
    gf_kit_check(kit, got.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(got.text, "Hello"),
                 "the child shows the bound property");
    gf_kit_status(kit, (*kit).objects.release(title), GF_STATUS_OK, "release the child's reference");
    gf_object missing = 0u;
    gf_kit_status(kit, (*kit).custom_controls.composite_child(card, gf_kit_text("nothing"), &missing),
                  GF_STATUS_NOT_FOUND, "an unknown local id is not found");
    gf_kit_status(kit, (*kit).custom_controls.composite_child(panel, gf_kit_text("title"), &missing),
                  GF_STATUS_WRONG_HANDLE_KIND, "a stock control is not a composite");

    gf_kit_recorder recorder;
    memset(&recorder, 0, sizeof(recorder));
    recorder.kit = kit;
    const gf_callback callback = gf_kit_recording_callback(&recorder);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(card, gf_kit_text("checked_changed"), &callback, &subscription),
                  GF_STATUS_OK, "subscribe to a declared event");
    uint32_t performed = 7u;
    gf_kit_status(kit, (*kit).custom_controls.perform_composite(card, gf_kit_text("toggle"), &performed),
                  GF_STATUS_OK, "perform a command");
    gf_kit_check(kit, performed == 1u, "the command ran");
    gf_kit_check(kit, recorder.invoked == 1u && recorder.last_sender == card &&
                          recorder.last_payload_kind == GF_VALUE_KIND_BOOLEAN,
                 "the declared event is heard with its payload");
    memset(&got, 0, sizeof(got));
    got.struct_size = (uint32_t)sizeof(got);
    gf_kit_status(kit, (*kit).properties.get(card, gf_kit_text("checked"), &got, text, sizeof(text), &required),
                  GF_STATUS_OK, "read the toggled property");
    gf_kit_check(kit, got.kind == GF_VALUE_KIND_BOOLEAN && got.boolean == 1u, "the command toggled the property");
    gf_kit_status(kit, (*kit).custom_controls.perform_composite(card, gf_kit_text("explode"), NULL),
                  GF_STATUS_NOT_FOUND, "an undeclared command is not found");
    gf_kit_status(kit, (*kit).custom_controls.perform_composite(panel, gf_kit_text("toggle"), NULL),
                  GF_STATUS_WRONG_HANDLE_KIND, "perform_composite takes a composite");
    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_OK, "disconnect");

    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the panel");
    gf_kit_status(kit, (*kit).objects.release(card), GF_STATUS_OK, "release the composite");
}

void gf_kit_custom(gf_kit* kit) {
    refusals(kit);
    properties_and_changes(kit);
    composites(kit);
    paint_endpoint(kit);
}
