/* Kit part: the schema table, and create by every schema type. */

#include "gf_conformance_internal.h"

#include <pthread.h>
#include <string.h>


static gf_schema_type type_record(void) {
    gf_schema_type record;
    memset(&record, 0, sizeof(record));
    record.struct_size = (uint32_t)sizeof(record);
    return record;
}

/* Copies an object's type id and compares it with a view. */
static int type_id_is(gf_kit* kit, gf_object object, gf_utf8_view expected) {
    char buffer[64];
    uint64_t required = 0u;
    if ((*kit).objects.type_id(object, buffer, sizeof(buffer), &required) != GF_STATUS_OK ||
        required != expected.size) {
        return 0;
    }
    const int equal = memcmp(buffer, expected.data, (size_t)required) == 0;
    return equal;
}

static void lookups(gf_kit* kit) {
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).schema.type_count(&count), GF_STATUS_OK, "type_count");
    gf_kit_check(kit, count > 0u, "the schema has types");

    uint64_t by_id = 0u;
    uint64_t by_display = 1u;
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text("check_box"), &by_id), GF_STATUS_OK, "find a type by id");
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text("CheckBox"), &by_display), GF_STATUS_OK,
                  "find a type by its display spelling");
    gf_kit_check(kit, by_id == by_display, "both spellings name one type");
    gf_schema_type record = type_record();
    gf_kit_status(kit, (*kit).schema.type_at(by_id, &record), GF_STATUS_OK, "type_at");
    gf_kit_check(kit, record.kind == GF_SCHEMA_KIND_CONTROL && gf_kit_view_equals(record.id, "check_box") &&
                          gf_kit_view_equals(record.type_name, "CheckBox") && record.property_count > 0u &&
                          record.interaction_count > 0u,
                 "a check box is a control with properties and interactions");
    gf_schema_type again = type_record();
    gf_kit_status(kit, (*kit).schema.type_at(by_id, &again), GF_STATUS_OK, "type_at again");
    gf_kit_check(kit, again.id.data == record.id.data, "the schema lends the same storage at every call");

    uint64_t untouched = 7u;
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text("no_such_type"), &untouched), GF_STATUS_NOT_FOUND,
                  "an unknown type is not found");
    gf_kit_check(kit, untouched == 7u, "a failed find leaves its output");
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text(""), &untouched), GF_STATUS_INVALID_ARGUMENT,
                  "an empty type name is refused");
    gf_kit_status(kit, (*kit).schema.type_at(count, &record), GF_STATUS_INVALID_ARGUMENT,
                  "an index past the end is refused");
    gf_schema_type small = type_record();
    small.struct_size = 8u;
    gf_kit_status(kit, (*kit).schema.type_at(by_id, &small), GF_STATUS_INVALID_ARGUMENT,
                  "a record below its 1.0 size is refused");
    gf_kit_status(kit, (*kit).schema.vocabulary_count(99u, &count), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown vocabulary is refused");

    uint64_t button = 0u;
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text("button"), &button), GF_STATUS_OK, "find button");
    gf_schema_type button_record = type_record();
    gf_kit_status(kit, (*kit).schema.type_at(button, &button_record), GF_STATUS_OK, "button's record");
    uint64_t dock = button_record.property_count;
    uint64_t index = 0u;
    for (index = 0u; index < button_record.property_count; ++index) {
        gf_schema_property property;
        memset(&property, 0, sizeof(property));
        property.struct_size = (uint32_t)sizeof(property);
        if ((*kit).schema.property_at(button, index, &property) == GF_STATUS_OK &&
            gf_kit_view_equals(property.name, "dock")) {
            dock = index;
            gf_kit_check(kit, property.kind == GF_VALUE_KIND_ENUMERATION &&
                                  gf_kit_view_equals(property.enumeration, "dock_style") && property.choice_count > 0u &&
                                  (property.flags & GF_SCHEMA_PROPERTY_FLAGS_WRITABLE) != 0u,
                         "dock is a writable enumeration");
        }
    }
    gf_kit_check(kit, dock < button_record.property_count, "a button has a dock property");

    /* Properties by schema index (N19): resolve "text" once, then reach it
     * by its position. */
    uint64_t text_index = button_record.property_count;
    for (index = 0u; index < button_record.property_count; ++index) {
        gf_schema_property property;
        memset(&property, 0, sizeof(property));
        property.struct_size = (uint32_t)sizeof(property);
        if ((*kit).schema.property_at(button, index, &property) == GF_STATUS_OK &&
            gf_kit_view_equals(property.name, "text")) {
            text_index = index;
        }
    }
    gf_kit_check(kit, text_index < button_record.property_count, "a button has a text property");
    const gf_object pressed = gf_kit_create(kit, "button", "kit.schema.indexed");
    gf_value written;
    memset(&written, 0, sizeof(written));
    written.struct_size = (uint32_t)sizeof(written);
    written.kind = GF_VALUE_KIND_TEXT;
    written.text = gf_kit_text("Indexed");
    gf_kit_status(kit, (*kit).properties.set_at(pressed, text_index, &written), GF_STATUS_OK,
                  "set a property by its schema index");
    gf_value read;
    memset(&read, 0, sizeof(read));
    read.struct_size = (uint32_t)sizeof(read);
    char text[16];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).properties.get(pressed, gf_kit_text("text"), &read, text, sizeof(text), &required),
                  GF_STATUS_OK, "read it back by name");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(read.text, "Indexed"),
                 "set_at wrote the property the index names");
    memset(&read, 0, sizeof(read));
    read.struct_size = (uint32_t)sizeof(read);
    gf_kit_status(kit, (*kit).properties.get_at(pressed, text_index, &read, text, sizeof(text), &required),
                  GF_STATUS_OK, "read it by index");
    gf_kit_check(kit, read.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(read.text, "Indexed"),
                 "get_at reads the property the index names");
    gf_kit_status(kit, (*kit).properties.get_at(pressed, button_record.property_count, &read, text, sizeof(text),
                                                &required),
                  GF_STATUS_INVALID_ARGUMENT, "an index past the type's properties is refused");
    gf_kit_status(kit, (*kit).properties.set_at(pressed, button_record.property_count, &written),
                  GF_STATUS_INVALID_ARGUMENT, "set_at past the list is refused");
    gf_kit_status(kit, (*kit).objects.release(pressed), GF_STATUS_OK, "release the indexed button");
    gf_schema_choice choice;
    memset(&choice, 0, sizeof(choice));
    choice.struct_size = (uint32_t)sizeof(choice);
    gf_kit_status(kit, (*kit).schema.choice_at(button, dock, 0u, &choice), GF_STATUS_OK, "choice_at");
    gf_kit_check(kit, gf_kit_view_equals(choice.name, "none") && choice.value == 0, "dock's first choice is none");
    gf_schema_part part;
    memset(&part, 0, sizeof(part));
    part.struct_size = (uint32_t)sizeof(part);
    gf_kit_status(kit, (*kit).schema.part_at(button, GF_SCHEMA_NONE, 0u, &part), GF_STATUS_OK, "part_at");
    gf_kit_check(kit, part.name.size > 0u && part.role.size > 0u, "a part has a name and a role");
    gf_kit_status(kit, (*kit).schema.item_system_at(button, 0u, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "item_system_at needs a record");

    gf_schema_engine engine;
    memset(&engine, 0, sizeof(engine));
    engine.struct_size = (uint32_t)sizeof(engine);
    gf_kit_status(kit, (*kit).schema.engine(&engine), GF_STATUS_OK, "engine");
    gf_kit_check(kit, engine.primitive_count > 0u, "the engine registers primitives");
}

/* The effect programs (since 1.1): each names a schema type as its parent,
 * and the refusals leave their outputs. */
static void effects(gf_kit* kit) {
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).schema.effect_count(&count), GF_STATUS_OK, "effect_count");
    gf_kit_check(kit, count > 0u, "the library registers effect programs");
    gf_kit_status(kit, (*kit).schema.effect_count(NULL), GF_STATUS_INVALID_ARGUMENT, "effect_count needs an output");
    uint64_t index = 0u;
    for (index = 0u; index < count; ++index) {
        gf_schema_effect effect;
        memset(&effect, 0, sizeof(effect));
        effect.struct_size = (uint32_t)sizeof(effect);
        gf_kit_status(kit, (*kit).schema.effect_at(index, &effect), GF_STATUS_OK, "effect_at");
        uint64_t parent = 0u;
        gf_kit_status(kit, (*kit).schema.find_type(effect.parent, &parent), GF_STATUS_OK,
                      "an effect's parent is a schema type");
        gf_kit_check(kit, effect.struct_size == (uint32_t)sizeof(effect) && effect.name.size > 0u &&
                              effect.time_model <= GF_SCHEMA_EFFECT_TIME_MODEL_AMBIENT &&
                              effect.input <= GF_SCHEMA_EFFECT_INPUT_POINTER_AND_KEYS && effect.state_budget > 0u &&
                              effect.frames_per_second > 0.0 && effect.outsets.left >= 0.0 &&
                              effect.outsets.top >= 0.0 && effect.outsets.right >= 0.0 && effect.outsets.bottom >= 0.0,
                     "an effect has a name, a known time model and input, a state budget, a rate and outsets");
    }
    gf_schema_effect past;
    memset(&past, 0, sizeof(past));
    past.struct_size = (uint32_t)sizeof(past);
    gf_kit_status(kit, (*kit).schema.effect_at(count, &past), GF_STATUS_INVALID_ARGUMENT,
                  "an effect index past the end is refused");
    gf_kit_check(kit, past.name.size == 0u, "a refused effect_at leaves its record");
    gf_schema_effect small;
    memset(&small, 0, sizeof(small));
    small.struct_size = 8u;
    gf_kit_status(kit, (*kit).schema.effect_at(0u, &small), GF_STATUS_INVALID_ARGUMENT,
                  "an effect record below its size is refused");
}

/* The properties a type provides to its children (since 1.1): a grid
 * panel's area, cell and item_style, none for a button, and the refusals. */
static void child_properties(gf_kit* kit) {
    uint64_t grid = 0u;
    uint64_t button = 0u;
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text("grid_panel"), &grid), GF_STATUS_OK, "find grid_panel");
    gf_kit_status(kit, (*kit).schema.find_type(gf_kit_text("button"), &button), GF_STATUS_OK,
                  "find button for child properties");
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).schema.child_property_count(grid, &count), GF_STATUS_OK, "child_property_count");
    gf_kit_check(kit, count == 3u, "a grid panel provides three properties to its children");
    uint64_t none = 1u;
    gf_kit_status(kit, (*kit).schema.child_property_count(button, &none), GF_STATUS_OK,
                  "child_property_count of a button");
    gf_kit_check(kit, none == 0u, "a button provides no properties to its children");
    gf_kit_status(kit, (*kit).schema.child_property_count(grid, NULL), GF_STATUS_INVALID_ARGUMENT,
                  "child_property_count needs an output");
    gf_schema_property area;
    memset(&area, 0, sizeof(area));
    area.struct_size = (uint32_t)sizeof(area);
    gf_kit_status(kit, (*kit).schema.child_property_at(grid, 0u, &area), GF_STATUS_OK, "child_property_at");
    gf_kit_check(kit, gf_kit_view_equals(area.name, "area") && area.kind == GF_VALUE_KIND_TEXT &&
                          (area.flags & GF_SCHEMA_PROPERTY_FLAGS_WRITABLE) != 0u && area.choice_count == 0u,
                 "a grid panel's first child property is its writable text area");
    /* item_style is found by its name, not its position. */
    uint64_t item_style_found = 0u;
    uint64_t index = 0u;
    for (index = 0u; index < count; ++index) {
        gf_schema_property record;
        memset(&record, 0, sizeof(record));
        record.struct_size = (uint32_t)sizeof(record);
        if ((*kit).schema.child_property_at(grid, index, &record) != GF_STATUS_OK) continue;
        if (!gf_kit_view_equals(record.name, "item_style")) continue;
        if (record.kind == GF_VALUE_KIND_TEXT && (record.flags & GF_SCHEMA_PROPERTY_FLAGS_WRITABLE) != 0u &&
            record.choice_count == 0u) {
            ++item_style_found;
        }
    }
    gf_kit_check(kit, item_style_found == 1u, "a grid panel provides its writable text item_style once");
    gf_schema_property past;
    memset(&past, 0, sizeof(past));
    past.struct_size = (uint32_t)sizeof(past);
    gf_kit_status(kit, (*kit).schema.child_property_at(grid, count, &past), GF_STATUS_INVALID_ARGUMENT,
                  "a child property past the end is refused");
    gf_kit_check(kit, past.name.size == 0u, "a refused child_property_at leaves its record");
    gf_schema_choice choice;
    memset(&choice, 0, sizeof(choice));
    choice.struct_size = (uint32_t)sizeof(choice);
    gf_kit_status(kit, (*kit).schema.child_choice_at(grid, 0u, 0u, &choice), GF_STATUS_INVALID_ARGUMENT,
                  "a text child property has no choices");
    gf_kit_status(kit, (*kit).schema.child_choice_at(grid, count, 0u, &choice), GF_STATUS_INVALID_ARGUMENT,
                  "a choice of a child property past the end is refused");
}

/* Every type the schema lists is created by its id unless it needs another
 * object or a window, or is a service. */
static void create_every_type(gf_kit* kit) {
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).schema.type_count(&count), GF_STATUS_OK, "type_count for create");
    uint64_t created = 0u;
    uint64_t wrong = 0u;
    uint64_t index = 0u;
    for (index = 0u; index < count; ++index) {
        gf_schema_type record = type_record();
        if ((*kit).schema.type_at(index, &record) != GF_STATUS_OK) {
            ++wrong;
            continue;
        }
        gf_object object = 0u;
        const gf_status status = (*kit).objects.create(record.id, gf_kit_text("kit.schema.every"), &object);
        if (gf_kit_create_refuses(record.id)) {
            if (status != GF_STATUS_UNSUPPORTED_CAPABILITY || object != 0u) {
                ++wrong;
            }
            continue;
        }
        if (status != GF_STATUS_OK || !type_id_is(kit, object, record.id)) {
            ++wrong;
        } else {
            ++created;
        }
        if (object != 0u && (*kit).objects.release(object) != GF_STATUS_OK) {
            ++wrong;
        }
    }
    gf_kit_check(kit, wrong == 0u, "create makes every type made from a stable id or from nothing and refuses the rest");
    gf_kit_check(kit, created > 90u, "most schema types are made by create");
    gf_object none = 0u;
    gf_kit_status(kit, (*kit).objects.create(gf_kit_text("no_such_type"), gf_kit_text("kit.none"), &none),
                  GF_STATUS_NOT_FOUND, "create of an unknown type is not found");
}

typedef struct schema_thread_call {
    gf_schema_table* schema;
    gf_status status;
    uint64_t count;
} schema_thread_call;

static void* read_type_count(void* context) {
    schema_thread_call* call = (schema_thread_call*)context;
    (*call).status = (*(*call).schema).type_count(&(*call).count);
    return NULL;
}

/* Reads the type count on a second thread; returns its status. */
static gf_status type_count_elsewhere(gf_kit* kit) {
    schema_thread_call call;
    memset(&call, 0, sizeof(call));
    call.schema = &(*kit).schema;
    call.status = GF_STATUS_INTERNAL;
    pthread_t thread;
    if (pthread_create(&thread, NULL, &read_type_count, &call) != 0) {
        return GF_STATUS_INTERNAL;
    }
    pthread_join(thread, NULL);
    return call.status;
}

/* A schema read binds an unbound library for itself alone; while objects
 * hold the UI thread, another thread is refused. */
static void threads(gf_kit* kit) {
    gf_kit_status(kit, type_count_elsewhere(kit), GF_STATUS_OK,
                  "another thread reads the schema while no object holds the UI thread");
    const gf_object panel = gf_kit_create(kit, "panel", "kit.schema.thread");
    gf_kit_check(kit, panel != 0u, "this thread is the UI thread again after that read");
    gf_kit_status(kit, type_count_elsewhere(kit), GF_STATUS_WRONG_THREAD,
                  "another thread is refused while an object holds the UI thread");
    gf_kit_status(kit, (*kit).objects.release(panel), GF_STATUS_OK, "release the thread panel");
}

void gf_kit_schema(gf_kit* kit) {
    lookups(kit);
    effects(kit);
    child_properties(kit);
    create_every_type(kit);
    threads(kit);
}
