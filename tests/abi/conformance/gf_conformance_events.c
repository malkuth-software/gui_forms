/* Kit part: events for every schema interaction (stage C5), and the event
 * fields a property change reaches through the ABI alone. */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_schema_type type_record(void) {
    gf_schema_type record;
    memset(&record, 0, sizeof(record));
    record.struct_size = (uint32_t)sizeof(record);
    return record;
}

static void GF_CALL ignore_invoke(void* context, gf_event* event) {
    (void)context;
    (void)event;
}

/* Subscribes to every interaction of one created object; returns how many
 * were refused. */
static uint64_t subscribe_all(gf_kit* kit, uint64_t type, const gf_schema_type* record, gf_object object,
                              uint64_t* connected) {
    uint64_t refused = 0u;
    uint64_t index = 0u;
    for (index = 0u; index < (*record).interaction_count; ++index) {
        gf_schema_interaction interaction;
        memset(&interaction, 0, sizeof(interaction));
        interaction.struct_size = (uint32_t)sizeof(interaction);
        if ((*kit).schema.interaction_at(type, index, &interaction) != GF_STATUS_OK) {
            ++refused;
            continue;
        }
        const gf_callback callback = gf_kit_callback(&ignore_invoke, NULL, NULL);
        gf_subscription subscription = 0u;
        if ((*kit).events.subscribe(object, interaction.name, &callback, &subscription) != GF_STATUS_OK) {
            if ((*kit).out != NULL) {
                fprintf((*kit).out, "  refused: %.*s.%.*s\n", (int)(*record).type_name.size,
                        (*record).type_name.data, (int)interaction.name.size, interaction.name.data);
            }
            ++refused;
            continue;
        }
        ++*connected;
        if ((*kit).events.disconnect(subscription) != GF_STATUS_OK) {
            ++refused;
        }
    }
    return refused;
}

/* The C5 gate: every interaction the schema lists for a type that create
 * makes is subscribed through the ABI. */
static void every_interaction(gf_kit* kit) {
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).schema.type_count(&count), GF_STATUS_OK, "type_count for events");
    uint64_t connected = 0u;
    uint64_t refused = 0u;
    uint64_t types = 0u;
    uint64_t index = 0u;
    for (index = 0u; index < count; ++index) {
        gf_schema_type record = type_record();
        if ((*kit).schema.type_at(index, &record) != GF_STATUS_OK) {
            ++refused;
            continue;
        }
        gf_object owner = 0u;
        const gf_object object = gf_kit_make_any(kit, record.id, "kit.events.every", &owner);
        if (object == 0u) {
            if (owner != 0u && (*kit).objects.release(owner) != GF_STATUS_OK) {
                ++refused;
            }
            continue;
        }
        ++types;
        refused += subscribe_all(kit, index, &record, object, &connected);
        if ((*kit).objects.release(object) != GF_STATUS_OK) {
            ++refused;
        }
        if (owner != 0u && (*kit).objects.release(owner) != GF_STATUS_OK) {
            ++refused;
        }
    }
    gf_kit_check(kit, refused == 0u, "every interaction of every created type subscribes");
    gf_kit_check(kit, types > 90u && connected > 1500u, "the interactions of most schema types were reached");
    if ((*kit).out != NULL) {
        fprintf((*kit).out, "  events: %llu interactions subscribed on %llu types\n", (unsigned long long)connected,
                (unsigned long long)types);
    }
}

/* What one field-reading callback saw. */
typedef struct field_probe {
    gf_kit* kit;
    uint32_t invoked;
    uint64_t field_count;
    gf_value_kind kind;
    gf_event_field_flags flags;
    int64_t number;
    char name[32];
    char text[32];
    int payload_is_field;
    gf_status write_status;
} field_probe;

static void copy_view(char* buffer, size_t capacity, gf_utf8_view view) {
    const size_t size = view.size < capacity - 1u ? (size_t)view.size : capacity - 1u;
    if (size > 0u) {
        memcpy(buffer, view.data, size);
    }
    buffer[size] = '\0';
}

static void GF_CALL probe_invoke(void* context, gf_event* event) {
    field_probe* probe = (field_probe*)context;
    ++(*probe).invoked;
    (*probe).field_count = (*event).field_count;
    if ((*event).field_count == 0u || (*event).fields == NULL) {
        return;
    }
    const gf_event_field* field = &(*event).fields[0];
    (*probe).kind = (*field).kind;
    (*probe).flags = (*field).flags;
    (*probe).number = (*(*field).value).signed_integer;
    copy_view((*probe).name, sizeof((*probe).name), (*field).name);
    copy_view((*probe).text, sizeof((*probe).text), (*(*field).value).text);
    (*probe).payload_is_field = (*event).payload == (*field).value;
    /* Not a mutable payload: the field is not writable. */
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    value.kind = GF_VALUE_KIND_ENUMERATION;
    (*probe).write_status = (*(*probe).kit).events.write_field(event, 0u, &value);
}

/* A scalar enumeration payload reached by a property change: one field
 * named value, with its choice's name, which is also the payload. */
static void enumeration_field(gf_kit* kit) {
    const gf_object check = gf_kit_create(kit, "check_box", "kit.events.fields");
    field_probe probe;
    memset(&probe, 0, sizeof(probe));
    probe.kit = kit;
    const gf_callback callback = gf_kit_callback(&probe_invoke, NULL, &probe);
    gf_subscription subscription = 0u;
    gf_kit_status(kit, (*kit).events.subscribe(check, gf_kit_text("check_state_changed"), &callback, &subscription),
                  GF_STATUS_OK, "subscribe to check_state_changed");
    gf_value checked;
    memset(&checked, 0, sizeof(checked));
    checked.struct_size = (uint32_t)sizeof(checked);
    checked.kind = GF_VALUE_KIND_BOOLEAN;
    checked.boolean = 1u;
    gf_kit_status(kit, (*kit).properties.set(check, gf_kit_text("checked"), &checked), GF_STATUS_OK,
                  "check the check box");
    gf_kit_check(kit, probe.invoked == 1u && probe.field_count == 1u, "one event with one field");
    gf_kit_check(kit, strcmp(probe.name, "value") == 0 && probe.kind == GF_VALUE_KIND_ENUMERATION &&
                          strcmp(probe.text, "checked") == 0 && probe.payload_is_field,
                 "a scalar enumeration is the field value, named, and the payload");
    gf_kit_check(kit, (probe.flags & GF_EVENT_FIELD_WRITABLE) == 0u && probe.write_status == GF_STATUS_INVALID_ARGUMENT,
                 "a field of an immutable payload is not writable");
    gf_kit_status(kit, (*kit).events.disconnect(subscription), GF_STATUS_OK, "disconnect check_state_changed");

    gf_event outside;
    memset(&outside, 0, sizeof(outside));
    outside.struct_size = (uint32_t)sizeof(outside);
    gf_kit_status(kit, (*kit).events.write_field(&outside, 0u, &checked), GF_STATUS_INVALID_STATE,
                  "write_field outside a callback is refused");
    gf_value element;
    memset(&element, 0, sizeof(element));
    element.struct_size = (uint32_t)sizeof(element);
    gf_kit_status(kit, (*kit).events.field_element(&outside, 0u, 0u, &element), GF_STATUS_INVALID_STATE,
                  "field_element outside a callback is refused");
    gf_kit_status(kit, (*kit).objects.release(check), GF_STATUS_OK, "release the fields check box");
}

void gf_kit_events(gf_kit* kit) {
    every_interaction(kit);
    enumeration_field(kit);
}
