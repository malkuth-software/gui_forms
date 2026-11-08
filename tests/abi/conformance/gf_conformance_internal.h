#ifndef GUI_FORMS_TESTS_ABI_GF_CONFORMANCE_INTERNAL_H
#define GUI_FORMS_TESTS_ABI_GF_CONFORMANCE_INTERNAL_H

/* Shared by the kit's parts. Not installed for consumers. */

#include "gf_conformance.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

/* The tables, loaded once, and the running tally. */
typedef struct gf_kit {
    FILE* out;
    uint32_t checks;
    uint32_t failures;
    pthread_t ui_thread;
    gf_runtime_table runtime;
    gf_objects_table objects;
    gf_properties_table properties;
    gf_events_table events;
    gf_dispatch_table dispatch;
    gf_windows_table windows;
    gf_schema_table schema;
    gf_themes_table themes;
    gf_host_table host;
    gf_diagnostics_table diagnostics;
    gf_controls_table controls;
    gf_text_editing_table text_editing;
    gf_overlays_table overlays;
    gf_property_grid_table property_grid;
    gf_custom_controls_table custom_controls;
    gf_bindings_table bindings;
    gf_subsurfaces_table subsurfaces;
    gf_application_table application;
    /* Loaded by the custom part where the library has it (Windows). */
    gf_windows_paint_endpoint_table windows_paint_endpoint;
} gf_kit;

/* Counts a check and reports it when condition is zero. */
void gf_kit_check(gf_kit* kit, int condition, const char* name);
/* Counts a check that a call returned expected, reporting both statuses. */
void gf_kit_status(gf_kit* kit, gf_status actual, gf_status expected, const char* name);

/* A view of a NUL-terminated literal. */
gf_utf8_view gf_kit_text(const char* text);
/* A callback record for invoke, release and context. */
gf_callback gf_kit_callback(void (GF_CALL* invoke)(void* context, gf_event* event),
                            void (GF_CALL* release)(void* context), void* context);
/* Whether a view holds exactly the bytes of text. */
int gf_kit_view_equals(gf_utf8_view view, const char* text);
/* Creates an object of type with stable id; zero on failure (reported). */
gf_object gf_kit_create(gf_kit* kit, const char* type, const char* stable_id);
/* The object's state, or GF_OBJECT_STATE_DISPOSED + 1 when the call fails. */
gf_object_state gf_kit_state(gf_kit* kit, gf_object object);
/* Whether create refuses the schema type id (it needs another object or a
 * window, or is a service). */
int gf_kit_create_refuses(gf_utf8_view id);
/* Makes the schema type id through create, or through create_with with the
 * owner or enumeration it needs (owner receives a reference the caller
 * releases, zero when none); zero when neither slot makes it. */
gf_object gf_kit_make_any(gf_kit* kit, gf_utf8_view id, const char* stable_id, gf_object* owner);
/* Sets a text property; returns the call's status. */
gf_status gf_kit_set_text(gf_kit* kit, gf_object object, const char* property, const char* text);

/* A recorder for callbacks: invocations, releases and the thread of each. */
typedef struct gf_kit_recorder {
    gf_kit* kit;
    uint32_t invoked;
    uint32_t released;
    uint32_t off_thread_calls;
    gf_object last_sender;
    gf_value_kind last_payload_kind;
    char last_name[64];
    char last_text[64];
} gf_kit_recorder;

void GF_CALL gf_kit_record_invoke(void* context, gf_event* event);
void GF_CALL gf_kit_record_release(void* context);
gf_callback gf_kit_recording_callback(gf_kit_recorder* recorder);

void gf_kit_ownership(gf_kit* kit);
void gf_kit_values(gf_kit* kit);
void gf_kit_schema(gf_kit* kit);
void gf_kit_callbacks(gf_kit* kit);
void gf_kit_events(gf_kit* kit);
void gf_kit_create_with(gf_kit* kit);
void gf_kit_diagnostics(gf_kit* kit);
void gf_kit_threads(gf_kit* kit);
void gf_kit_window(gf_kit* kit);
void gf_kit_window_handle(gf_kit* kit);
void gf_kit_window_events(gf_kit* kit);
void gf_kit_themes(gf_kit* kit);
void gf_kit_proxies(gf_kit* kit);
void gf_kit_families(gf_kit* kit);
void gf_kit_custom(gf_kit* kit);
void gf_kit_bindings(gf_kit* kit);
/* Runs a window with a threaded sub-surface. */
void gf_kit_subsurfaces(gf_kit* kit);
void gf_kit_application(gf_kit* kit);
void gf_kit_host(gf_kit* kit);
/* The host checks that need a running window; from its ready callback. */
void gf_kit_host_running(gf_kit* kit);

#endif
