/* Kit part: custom controls (GF_TABLE_CUSTOM_CONTROLS), controls.invalidate
 * and, where the library has it, the Windows paint endpoint
 * (GF_TABLE_WINDOWS_PAINT_ENDPOINT). Their paint path is checked by the C++ program
 * tests/abi/custom/gf_abi_custom_paint.cpp, which reaches a window's paint. */

#include "gf_conformance_internal.h"

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

void gf_kit_custom(gf_kit* kit) {
    refusals(kit);
    properties_and_changes(kit);
    paint_endpoint(kit);
}
