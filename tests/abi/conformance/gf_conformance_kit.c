/* The kit's entry and shared helpers. See gf_conformance.h. */

#include "gf_conformance_internal.h"

#include <string.h>

void gf_kit_check(gf_kit* kit, int condition, const char* name) {
    ++(*kit).checks;
    if (!condition) {
        ++(*kit).failures;
        if ((*kit).out != NULL) {
            fprintf((*kit).out, "gf_conformance: FAIL %s\n", name);
        }
    }
}

void gf_kit_status(gf_kit* kit, gf_status actual, gf_status expected, const char* name) {
    ++(*kit).checks;
    if (actual == expected) {
        return;
    }
    ++(*kit).failures;
    if ((*kit).out == NULL) {
        return;
    }
    /* The calling thread's failure detail, once the runtime table is loaded. */
    gf_error_detail detail;
    char message[513];
    uint64_t required = 0u;
    memset(&detail, 0, sizeof(detail));
    detail.struct_size = (uint32_t)sizeof(detail);
    message[0] = '\0';
    if ((*kit).runtime.error_detail != NULL &&
        (*kit).runtime.error_detail(&detail, message, sizeof(message) - 1u, &required) == GF_STATUS_OK) {
        message[required] = '\0';
    }
    fprintf((*kit).out, "gf_conformance: FAIL %s: status %d, expected %d (last failure: table %u, slot %u: %s)\n",
            name, (int)actual, (int)expected, detail.table_id, detail.slot_index, message);
}

gf_utf8_view gf_kit_text(const char* text) {
    gf_utf8_view view;
    view.data = text;
    view.size = (uint64_t)strlen(text);
    return view;
}

gf_callback gf_kit_callback(void (GF_CALL* invoke)(void* context, gf_event* event),
                            void (GF_CALL* release)(void* context), void* context) {
    gf_callback callback;
    memset(&callback, 0, sizeof(callback));
    callback.struct_size = (uint32_t)sizeof(gf_callback);
    callback.invoke = invoke;
    callback.release = release;
    callback.context = context;
    return callback;
}

int gf_kit_view_equals(gf_utf8_view view, const char* text) {
    const size_t length = strlen(text);
    const int equal = view.size == (uint64_t)length && (length == 0u || memcmp(view.data, text, length) == 0);
    return equal;
}

gf_object gf_kit_create(gf_kit* kit, const char* type, const char* stable_id) {
    gf_object object = 0u;
    const gf_status created = (*kit).objects.create(gf_kit_text(type), gf_kit_text(stable_id), &object);
    gf_kit_status(kit, created, GF_STATUS_OK, "create a control");
    return object;
}

gf_object_state gf_kit_state(gf_kit* kit, gf_object object) {
    gf_object_state state = GF_OBJECT_STATE_DISPOSED + 1u;
    if ((*kit).objects.state(object, &state) != GF_STATUS_OK) {
        return GF_OBJECT_STATE_DISPOSED + 1u;
    }
    return state;
}

gf_status gf_kit_set_text(gf_kit* kit, gf_object object, const char* property, const char* text) {
    gf_value value;
    memset(&value, 0, sizeof(value));
    value.struct_size = (uint32_t)sizeof(gf_value);
    value.kind = GF_VALUE_KIND_TEXT;
    value.text = gf_kit_text(text);
    const gf_status set = (*kit).properties.set(object, gf_kit_text(property), &value);
    return set;
}

/* Copies at most capacity - 1 bytes of a view and terminates them. */
static void copy_view(gf_utf8_view view, char* buffer, size_t capacity) {
    size_t length = (size_t)view.size;
    if (length > capacity - 1u) {
        length = capacity - 1u;
    }
    if (length != 0u) {
        memcpy(buffer, view.data, length);
    }
    buffer[length] = '\0';
}

void GF_CALL gf_kit_record_invoke(void* context, gf_event* event) {
    gf_kit_recorder* recorder = (gf_kit_recorder*)context;
    ++(*recorder).invoked;
    if (!pthread_equal(pthread_self(), (*(*recorder).kit).ui_thread)) {
        ++(*recorder).off_thread_calls;
    }
    (*recorder).last_sender = (*event).sender;
    copy_view((*event).name, (*recorder).last_name, sizeof((*recorder).last_name));
    (*recorder).last_payload_kind = GF_VALUE_KIND_NULL;
    (*recorder).last_text[0] = '\0';
    if ((*event).payload != NULL) {
        (*recorder).last_payload_kind = (*(*event).payload).kind;
        if ((*(*event).payload).kind == GF_VALUE_KIND_TEXT) {
            copy_view((*(*event).payload).text, (*recorder).last_text, sizeof((*recorder).last_text));
        }
    }
}

void GF_CALL gf_kit_record_release(void* context) {
    gf_kit_recorder* recorder = (gf_kit_recorder*)context;
    ++(*recorder).released;
    if (!pthread_equal(pthread_self(), (*(*recorder).kit).ui_thread)) {
        ++(*recorder).off_thread_calls;
    }
}

gf_callback gf_kit_recording_callback(gf_kit_recorder* recorder) {
    const gf_callback callback = gf_kit_callback(&gf_kit_record_invoke, &gf_kit_record_release, recorder);
    return callback;
}

/* Loads one table at the header's version; reports a failure. */
static void load_table(gf_kit* kit, gf_table_id table_id, void* table, uint64_t size, const char* name) {
    const gf_status loaded = gf_abi_get_table(table_id, GF_ABI_MAJOR, GF_ABI_MINOR, table, size);
    gf_kit_status(kit, loaded, GF_STATUS_OK, name);
}

uint32_t gf_conformance_run(FILE* out, gf_conformance_flags flags) {
    gf_kit kit;
    memset(&kit, 0, sizeof(kit));
    kit.out = out;
    kit.ui_thread = pthread_self();
    load_table(&kit, GF_TABLE_RUNTIME, &kit.runtime, sizeof(kit.runtime), "load the runtime table");
    load_table(&kit, GF_TABLE_OBJECTS, &kit.objects, sizeof(kit.objects), "load the objects table");
    load_table(&kit, GF_TABLE_PROPERTIES, &kit.properties, sizeof(kit.properties), "load the properties table");
    load_table(&kit, GF_TABLE_EVENTS, &kit.events, sizeof(kit.events), "load the events table");
    load_table(&kit, GF_TABLE_DISPATCH, &kit.dispatch, sizeof(kit.dispatch), "load the dispatch table");
    load_table(&kit, GF_TABLE_WINDOWS, &kit.windows, sizeof(kit.windows), "load the windows table");
    load_table(&kit, GF_TABLE_SCHEMA, &kit.schema, sizeof(kit.schema), "load the schema table");
    load_table(&kit, GF_TABLE_THEMES, &kit.themes, sizeof(kit.themes), "load the themes table");
    load_table(&kit, GF_TABLE_HOST, &kit.host, sizeof(kit.host), "load the host table");
    load_table(&kit, GF_TABLE_DIAGNOSTICS, &kit.diagnostics, sizeof(kit.diagnostics), "load the diagnostics table");
    load_table(&kit, GF_TABLE_CONTROLS, &kit.controls, sizeof(kit.controls), "load the controls table");
    load_table(&kit, GF_TABLE_TEXT_EDITING, &kit.text_editing, sizeof(kit.text_editing),
               "load the text editing table");
    load_table(&kit, GF_TABLE_POPUPS, &kit.popups, sizeof(kit.popups), "load the popups table");
    load_table(&kit, GF_TABLE_PROPERTY_GRID, &kit.property_grid, sizeof(kit.property_grid),
               "load the property grid table");
    load_table(&kit, GF_TABLE_CUSTOM_CONTROLS, &kit.custom_controls, sizeof(kit.custom_controls),
               "load the custom controls table");
    load_table(&kit, GF_TABLE_BINDINGS, &kit.bindings, sizeof(kit.bindings), "load the bindings table");
    if (kit.failures == 0u) {
        gf_kit_ownership(&kit);
        gf_kit_values(&kit);
        gf_kit_schema(&kit);
        gf_kit_callbacks(&kit);
        /* The thread handover needs an idle library, so it runs before the
         * first window: the engine a window uses binds the UI thread for
         * good. */
        gf_kit_threads(&kit);
        gf_kit_create_with(&kit);
        gf_kit_events(&kit);
        gf_kit_themes(&kit);
        gf_kit_proxies(&kit);
        gf_kit_families(&kit);
        gf_kit_custom(&kit);
        gf_kit_bindings(&kit);
        gf_kit_host(&kit);
        gf_kit_diagnostics(&kit);
        if ((flags & GF_CONFORMANCE_FLAGS_WINDOW) != 0u) {
            gf_kit_window(&kit);
            gf_kit_window_handle(&kit);
        }
    }
    if (out != NULL) {
        fprintf(out, "gf_conformance: %u checks, %u failed\n", kit.checks, kit.failures);
    }
    return kit.failures;
}
