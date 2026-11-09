/* Kit part: data binding (GF_TABLE_BINDINGS): a binding source's records
 * and position, and a text box's text bound to one of their fields. */

#include "gf_conformance_internal.h"

#include <string.h>

static gf_value text_value(const char* text) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    value.kind = GF_VALUE_KIND_TEXT;
    value.text = gf_kit_text(text);
    return value;
}

/* Adds a record whose one field, title, holds title. */
static gf_status add_titled(gf_kit* kit, gf_object source, const char* stable_id, const char* title,
                            uint64_t* index) {
    const gf_value value = text_value(title);
    gf_binding_field field;
    field.name = gf_kit_text("title");
    field.value = &value;
    gf_binding_record record;
    memset(&record, 0, sizeof(record));
    record.struct_size = (uint32_t)sizeof(record);
    record.editable = 1u;
    record.stable_id = gf_kit_text(stable_id);
    record.fields = &field;
    record.field_count = 1u;
    return (*kit).bindings.add_record(source, &record, index);
}

/* Whether the source's current title is expected. */
static int current_title_is(gf_kit* kit, gf_object source, const char* expected) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[64];
    uint64_t required = 0u;
    if ((*kit).bindings.current_field(source, gf_kit_text("title"), &value, text, sizeof(text), &required) !=
        GF_STATUS_OK) {
        return 0;
    }
    return value.kind == GF_VALUE_KIND_TEXT && gf_kit_view_equals(value.text, expected);
}

/* Whether the control's text is expected. */
static int text_is(gf_kit* kit, gf_object control, const char* expected) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(value);
    char text[64];
    uint64_t required = 0u;
    if ((*kit).properties.get(control, gf_kit_text("text"), &value, text, sizeof(text), &required) !=
        GF_STATUS_OK) {
        return 0;
    }
    return gf_kit_view_equals(value.text, expected);
}

void gf_kit_bindings(gf_kit* kit) {
    const gf_object root = gf_kit_create(kit, "panel", "kit.bindings.root");
    gf_object window = 0u;
    gf_kit_status(kit, (*kit).windows.create(root, &window), GF_STATUS_OK, "make the bindings window");
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = window;
    gf_object source = 0u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("binding_source"), gf_kit_text("kit.bindings.source"),
                                                  &arguments, &source),
                  GF_STATUS_OK, "make a binding source");
    const gf_object box = gf_kit_create(kit, "text_box", "kit.bindings.box");

    uint64_t index = 9u;
    gf_kit_status(kit, add_titled(kit, source, "first", "First", &index), GF_STATUS_OK, "add a record");
    gf_kit_check(kit, index == 0u, "the first record is at 0");
    gf_kit_status(kit, add_titled(kit, source, "second", "Second", &index), GF_STATUS_OK, "add another");
    gf_kit_check(kit, index == 1u, "the second record is at 1");
    gf_kit_status(kit, add_titled(kit, source, "first", "Again", &index), GF_STATUS_INVALID_ARGUMENT,
                  "a stable id is not repeated");
    int64_t position = -2;
    uint64_t count = 0u;
    gf_kit_status(kit, (*kit).bindings.position(source, &position, &count), GF_STATUS_OK, "read the position");
    gf_kit_check(kit, position == 0 && count == 2u, "the source is at its first record");

    gf_binding_arguments binding_arguments;
    memset(&binding_arguments, 0, sizeof(binding_arguments));
    binding_arguments.struct_size = (uint32_t)sizeof(binding_arguments);
    binding_arguments.source_update_mode = GF_SOURCE_UPDATE_ON_VALIDATION;
    binding_arguments.control_update_mode = GF_CONTROL_UPDATE_ON_PROPERTY_CHANGED;
    binding_arguments.target = box;
    binding_arguments.source = box;
    binding_arguments.property = gf_kit_text("Text");
    binding_arguments.data_member = gf_kit_text("title");
    gf_object binding = 0u;
    gf_kit_status(kit, (*kit).bindings.bind(&binding_arguments, &binding), GF_STATUS_WRONG_HANDLE_KIND,
                  "a binding's source is a binding source");
    binding_arguments.source = source;
    binding_arguments.property = gf_kit_text("no_such_property");
    gf_kit_status(kit, (*kit).bindings.bind(&binding_arguments, &binding), GF_STATUS_INVALID_ARGUMENT,
                  "a property that is not bindable is refused");
    binding_arguments.property = gf_kit_text("Text");
    binding_arguments.source_update_mode = 3u;
    gf_kit_status(kit, (*kit).bindings.bind(&binding_arguments, &binding), GF_STATUS_INVALID_ARGUMENT,
                  "an unknown update mode is refused");
    binding_arguments.source_update_mode = GF_SOURCE_UPDATE_ON_VALIDATION;
    gf_kit_check(kit, binding == 0u, "a refused bind leaves its output");
    gf_kit_status(kit, (*kit).bindings.bind(&binding_arguments, &binding), GF_STATUS_OK, "bind the text");

    uint32_t updated = 9u;
    gf_kit_status(kit, (*kit).bindings.read_value(binding, &updated), GF_STATUS_OK, "read the source");
    gf_kit_check(kit, updated == 1u && text_is(kit, box, "First"), "the control shows the current field");
    uint32_t moved = 9u;
    gf_kit_status(kit, (*kit).bindings.set_position(source, 1, &moved), GF_STATUS_OK, "move to the second record");
    gf_kit_check(kit, moved == 1u, "the position moved");
    gf_kit_status(kit, (*kit).bindings.read_value(binding, &updated), GF_STATUS_OK, "read the source again");
    gf_kit_check(kit, text_is(kit, box, "Second"), "the control follows the position");
    gf_kit_status(kit, (*kit).bindings.set_position(source, 7, &moved), GF_STATUS_INVALID_ARGUMENT,
                  "a position past the records is refused");

    gf_kit_status(kit, gf_kit_set_text(kit, box, "text", "Edited"), GF_STATUS_OK, "edit the control");
    gf_kit_status(kit, (*kit).bindings.write_value(binding, &updated), GF_STATUS_OK, "write the source");
    gf_kit_check(kit, updated == 1u && current_title_is(kit, source, "Edited"), "the source holds the edit");
    const gf_value direct = text_value("Direct");
    uint32_t accepted = 9u;
    gf_kit_status(kit, (*kit).bindings.set_current_field(source, gf_kit_text("title"), &direct, &accepted),
                  GF_STATUS_OK, "set the current field");
    gf_kit_check(kit, accepted == 1u && current_title_is(kit, source, "Direct"), "the field holds the value");
    gf_value missing;
    memset(&missing, 0, sizeof(missing));
    missing.struct_size = (uint32_t)sizeof(missing);
    char text[16];
    uint64_t required = 0u;
    gf_kit_status(kit, (*kit).bindings.current_field(source, gf_kit_text("no_field"), &missing, text, sizeof(text),
                                                     &required),
                  GF_STATUS_NOT_FOUND, "a missing field is not found");

    const gf_value third_title = text_value("Third");
    gf_binding_field fields[2];
    fields[0].name = gf_kit_text("title");
    fields[0].value = &third_title;
    fields[1].name = gf_kit_text("Title");
    fields[1].value = NULL;
    gf_binding_record third;
    memset(&third, 0, sizeof(third));
    third.struct_size = (uint32_t)sizeof(third);
    third.editable = 1u;
    third.stable_id = gf_kit_text("third");
    third.fields = fields;
    third.field_count = 2u;
    gf_kit_status(kit, (*kit).bindings.insert_record(source, 0u, &third), GF_STATUS_INVALID_ARGUMENT,
                  "a record's field names are distinct");
    third.field_count = 1u;
    gf_kit_status(kit, (*kit).bindings.insert_record(source, 9u, &third), GF_STATUS_INVALID_ARGUMENT,
                  "an insertion past the records is refused");
    gf_kit_status(kit, (*kit).bindings.insert_record(source, 0u, &third), GF_STATUS_OK, "insert a record");
    gf_kit_status(kit, (*kit).bindings.position(source, &position, &count), GF_STATUS_OK, "read the count");
    gf_kit_check(kit, count == 3u, "three records");
    uint32_t removed = 9u;
    gf_kit_status(kit, (*kit).bindings.remove_record(source, 5u, &removed), GF_STATUS_OK, "remove past the end");
    gf_kit_check(kit, removed == 0u, "nothing is removed past the end");
    gf_kit_status(kit, (*kit).bindings.remove_record(source, 0u, &removed), GF_STATUS_OK, "remove a record");
    gf_kit_check(kit, removed == 1u, "the record is removed");

    gf_kit_status(kit, (*kit).bindings.unbind(binding), GF_STATUS_OK, "unbind");
    gf_kit_status(kit, (*kit).bindings.unbind(binding), GF_STATUS_DISPOSED, "unbinding disposes the binding");
    gf_kit_status(kit, (*kit).bindings.read_value(box, &updated), GF_STATUS_WRONG_HANDLE_KIND,
                  "a control is not a binding");
    gf_kit_status(kit, (*kit).bindings.clear_records(source), GF_STATUS_OK, "clear the records");
    gf_kit_status(kit, (*kit).bindings.position(source, &position, &count), GF_STATUS_OK, "read the empty source");
    gf_kit_check(kit, count == 0u && position == -1, "an empty source has no position");

    gf_kit_status(kit, (*kit).objects.release(binding), GF_STATUS_OK, "release the binding");
    gf_kit_status(kit, (*kit).objects.release(box), GF_STATUS_OK, "release the text box");
    gf_kit_status(kit, (*kit).objects.release(source), GF_STATUS_OK, "release the source");
    gf_kit_status(kit, (*kit).objects.release(window), GF_STATUS_OK, "release the bindings window");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the bindings root");
}

/* What the running root's ready callback saw. */
typedef struct bindings_run {
    gf_kit* kit;
    gf_object child;
    gf_object source;
    gf_object child_source;
    gf_status made_on_root;
    gf_status made_on_child;
    gf_status added;
    int64_t position;
    uint64_t count;
    gf_status close;
} bindings_run;

static void GF_CALL on_bindings_ready(void* context, gf_event* event) {
    bindings_run* run = (bindings_run*)context;
    gf_kit* kit = (*run).kit;
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = (*event).sender;
    (*run).made_on_root = (*kit).objects.create_with(gf_kit_text("binding_source"), gf_kit_text("kit.run.source"),
                                                     &arguments, &(*run).source);
    arguments.owner = (*run).child;
    (*run).made_on_child = (*kit).objects.create_with(gf_kit_text("BindingSource"),
                                                      gf_kit_text("kit.run.child_source"), &arguments,
                                                      &(*run).child_source);
    uint64_t index = 0u;
    (*run).added = add_titled(kit, (*run).source, "only", "Only", &index);
    (void)(*kit).bindings.position((*run).source, &(*run).position, &(*run).count);
    (*run).close = (*kit).windows.request_close((*event).sender);
}

/* A binding source made over windows.run's root (since 1.1): a C program
 * that runs a root, not a created window, binds data from its ready
 * callback. */
void gf_kit_bindings_running(gf_kit* kit) {
    bindings_run run;
    memset(&run, 0, sizeof(run));
    run.kit = kit;
    run.made_on_root = -1;
    run.made_on_child = -1;
    run.added = -1;
    run.position = -2;
    run.close = -1;
    const gf_object root = gf_kit_create(kit, "panel", "kit.run.bindings");
    run.child = gf_kit_create(kit, "label", "kit.run.bindings.label");
    gf_kit_status(kit, (*kit).objects.insert_child(root, run.child, 0u), GF_STATUS_OK, "build the run's tree");
    gf_create_arguments arguments;
    memset(&arguments, 0, sizeof(arguments));
    arguments.struct_size = (uint32_t)sizeof(arguments);
    arguments.owner = root;
    gf_object early = 9u;
    gf_kit_status(kit, (*kit).objects.create_with(gf_kit_text("binding_source"), gf_kit_text("kit.run.early"),
                                                  &arguments, &early),
                  GF_STATUS_WRONG_HANDLE_KIND, "a root in no window is not a binding source's owner");
    gf_kit_check(kit, early == 9u, "the refusal leaves its output");

    const gf_callback ready = gf_kit_callback(&on_bindings_ready, NULL, &run);
    gf_run_options options;
    memset(&options, 0, sizeof(options));
    options.struct_size = (uint32_t)sizeof(options);
    options.title = gf_kit_text("GUI.Forms C ABI bindings on a run");
    options.ready = &ready;
    gf_kit_status(kit, (*kit).windows.run(root, &options), GF_STATUS_OK, "run the root");
    gf_kit_status(kit, run.made_on_root, GF_STATUS_OK, "a binding source over the running root");
    gf_kit_status(kit, run.made_on_child, GF_STATUS_OK, "a binding source over a control of the running tree");
    gf_kit_status(kit, run.added, GF_STATUS_OK, "the source takes a record during the run");
    gf_kit_check(kit, run.position == 0 && run.count == 1u, "the source is at its one record");
    gf_kit_status(kit, run.close, GF_STATUS_OK, "close the run");
    gf_kit_check(kit, gf_kit_state(kit, run.source) == GF_OBJECT_STATE_ALIVE,
                 "the source outlives the run's window, unbound");
    gf_kit_status(kit, (*kit).objects.release(run.child_source), GF_STATUS_OK, "release the child's source");
    gf_kit_status(kit, (*kit).objects.release(run.source), GF_STATUS_OK, "release the root's source");
    gf_kit_status(kit, (*kit).objects.release(run.child), GF_STATUS_OK, "release the run's label");
    gf_kit_status(kit, (*kit).objects.release(root), GF_STATUS_OK, "release the run's root");
}
