#ifndef GUI_FORMS_C_GF_ABI_H
#define GUI_FORMS_C_GF_ABI_H

/*
 * GUI.Forms C ABI, major 1: the stability target of GUI.Forms 0.5, frozen
 * at 1.0. Until 1.0 the audit records every break against the 0.5 baseline
 * with its reason (manifests/abi/audit_policy.json); from 1.0 it rejects them.
 *
 * This header is the contract. tools/abi_audit.py reads it in a restricted
 * dialect and records it in manifests/abi/gf_abi.json; the audit rejects
 * anything outside that dialect and any change that breaks a sealed baseline.
 * The rules, in short (the stage C plan states them in full):
 *
 * - Types: fixed-width integers, double, pointers, and typedef'd function
 *   pointers. No enum, bool, size_t, long, wchar_t, bit-field or union types.
 *   An enumeration is a uint32_t (or int32_t) typedef plus integer-literal
 *   macros; a consumer treats an unknown value as unsupported.
 * - Records have no implicit padding (explicit reserved fields) and a size
 *   that is a multiple of 8. A record larger than 16 bytes is passed by
 *   pointer, never by value.
 * - Text is UTF-8: gf_utf8_view in, caller-supplied buffers out.
 * - Names: a constant of a typedef'd enumeration carries the typedef's name
 *   as its prefix (gf_status: GF_STATUS_*). No name repeats one of the
 *   experimental 0.x header (gui_forms/c_api.h), so a program migrating from
 *   it can include both.
 * - Calling convention: GF_CALL, the platform's C convention (Microsoft x64
 *   on Windows, System V AMD64 on x86-64 Linux, AAPCS64 and Apple's arm64
 *   variant on macOS). Every function and function pointer is spelled with
 *   GF_CALL. Only 64-bit targets are supported.
 * - Versioning: gf_abi_get_table is the one exported function. A caller
 *   passes the GF_ABI_MAJOR and GF_ABI_MINOR it was compiled with and the
 *   size of the table it knows. Within one major a table only grows at its
 *   tail, a slot is never removed or changed, and a constant never changes
 *   its value.
 * - Errors: every function returns a gf_status. On failure every output is
 *   left unchanged, except that GF_STATUS_BUFFER_TOO_SMALL sets the required
 *   size. The failing thread's detail is copied out through the runtime
 *   table. No library storage is lent to the caller, except the schema's
 *   text (GF_TABLE_SCHEMA), which lives until the process ends.
 */

#include <stdint.h>

#if UINTPTR_MAX != UINT64_MAX
#error "The GUI.Forms C ABI supports 64-bit targets only"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* gf:platform-begin */
#if defined(_WIN32)
#if defined(GF_ABI_BUILD)
#define GF_ABI_EXPORT __declspec(dllexport)
#else
#define GF_ABI_EXPORT __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GF_ABI_EXPORT __attribute__((visibility("default")))
#else
#define GF_ABI_EXPORT
#endif
/* Every supported target has one C calling convention, so GF_CALL is empty;
 * it marks each function so that a port to a target with several can name one. */
#define GF_CALL
/* gf:platform-end */

/* The ABI version this header describes. */
#define GF_ABI_MAJOR 1u
#define GF_ABI_MINOR 0u

/* gf_status: the result of every call. Zero is success; failures are
 * positive and the list only grows. */
typedef int32_t gf_status;
#define GF_STATUS_OK 0
#define GF_STATUS_INVALID_ARGUMENT 1
#define GF_STATUS_STALE_HANDLE 2
#define GF_STATUS_WRONG_HANDLE_KIND 3
#define GF_STATUS_WRONG_THREAD 4
#define GF_STATUS_DISPOSED 5
#define GF_STATUS_BUFFER_TOO_SMALL 6
#define GF_STATUS_UNSUPPORTED_VERSION 7
#define GF_STATUS_INTERNAL 8
#define GF_STATUS_OUT_OF_MEMORY 9
#define GF_STATUS_UNSUPPORTED_CAPABILITY 10
#define GF_STATUS_NOT_FOUND 11
#define GF_STATUS_INVALID_STATE 12
#define GF_STATUS_LIMIT_EXCEEDED 13
#define GF_STATUS_CANCELLED 14
#define GF_STATUS_REMOVED 15

/* gf_table_id: names one function table for gf_abi_get_table. */
typedef uint32_t gf_table_id;
#define GF_TABLE_RUNTIME 1u
#define GF_TABLE_OBJECTS 2u
#define GF_TABLE_PROPERTIES 3u
#define GF_TABLE_EVENTS 4u
#define GF_TABLE_DISPATCH 5u
#define GF_TABLE_WINDOWS 6u
#define GF_TABLE_SCHEMA 7u
#define GF_TABLE_THEMES 8u
#define GF_TABLE_HOST 9u

/* The slot index an error detail reports for gf_abi_get_table itself. */
#define GF_ABI_ENTRY_SLOT 4294967295u

/*
 * A retained object: the slot in the low 32 bits and the generation in the
 * high 32 bits. Zero is no object. A stale value is rejected with
 * GF_STATUS_STALE_HANDLE, never dereferenced.
 */
typedef uint64_t gf_object;

/* Borrowed UTF-8 text: size bytes at data, not terminated. data may be null
 * only when size is zero. The callee copies what it keeps. */
typedef struct gf_utf8_view {
    const char* data;
    uint64_t size;
} gf_utf8_view;

/*
 * The library and ABI versions. An output record: the caller sets
 * struct_size to the size it was compiled with; the library writes only
 * that many bytes and leaves struct_size as it was.
 */
typedef struct gf_abi_version_info {
    uint32_t struct_size;
    uint32_t abi_major;
    uint32_t abi_minor;
    uint32_t library_major;
    uint32_t library_minor;
    uint32_t library_patch;
} gf_abi_version_info;

/*
 * The calling thread's last failure. An output record, as above. status is
 * GF_STATUS_OK when no call on this thread has failed since the last clear_error.
 * table_id and slot_index name the failing function (slot_index counts the
 * table's function pointers from zero; GF_ABI_ENTRY_SLOT is gf_abi_get_table).
 * message_size is the byte length of the UTF-8 message, at most 512.
 */
typedef struct gf_error_detail {
    uint32_t struct_size;
    gf_status status;
    gf_table_id table_id;
    uint32_t slot_index;
    uint64_t message_size;
} gf_error_detail;

/*
 * GF_TABLE_RUNTIME: version and error detail. Its functions may be called
 * from any thread, and they never record a failure of their own, so reading
 * the detail does not replace it.
 */
typedef struct gf_runtime_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Writes the versions into info. */
    gf_status (GF_CALL* version)(gf_abi_version_info* info);
    /*
     * Copies the calling thread's last failure into detail and its message
     * into message_buffer (not terminated). message_required receives the
     * message size. When message_capacity is smaller, nothing else is written
     * and the result is GF_STATUS_BUFFER_TOO_SMALL. message_buffer may be null
     * when message_capacity is zero.
     */
    gf_status (GF_CALL* error_detail)(gf_error_detail* detail,
                                      char* message_buffer,
                                      uint64_t message_capacity,
                                      uint64_t* message_required);
    /* Forgets the calling thread's last failure. */
    gf_status (GF_CALL* clear_error)(void);
} gf_runtime_table;

/*
 * Threads. The first thread that creates an object becomes the library's UI
 * thread. It stays the UI thread while any object, subscription, posted
 * callback or run exists, and stops being it when the last of them ends. A
 * thread that reads the schema while there is no UI thread is the UI thread
 * for that call only.
 * Every function of the tables below runs on the UI thread only and returns
 * GF_STATUS_WRONG_THREAD elsewhere, except objects.retain, objects.release and
 * dispatch.begin_invoke, which any thread may call. A release that ends an
 * object off the UI thread is completed on the UI thread: at its next call
 * into the library, or by a running window's event loop.
 *
 * Callbacks. The library calls a callback on the UI thread only, never two at
 * once, and calls its release exactly once, on the UI thread, after the last
 * invoke has returned. A callback may call any function, including the ones
 * that end it. When a call that would take a callback fails, the library has
 * not taken it and never calls it or its release.
 */

/* gf_object_state: an object's life. A disposed object keeps its identity
 * until its last release, but refuses most calls with GF_STATUS_DISPOSED. */
typedef uint32_t gf_object_state;
#define GF_OBJECT_STATE_ALIVE 0u
#define GF_OBJECT_STATE_DISPOSING 1u
#define GF_OBJECT_STATE_DISPOSED 2u

/* gf_value_kind: what a gf_value holds. The numbering is the library's
 * property kinds; the list only grows. */
typedef uint32_t gf_value_kind;
#define GF_VALUE_KIND_NULL 0u
#define GF_VALUE_KIND_BOOLEAN 1u
#define GF_VALUE_KIND_SIGNED 2u
#define GF_VALUE_KIND_UNSIGNED 3u
#define GF_VALUE_KIND_NUMBER 4u
#define GF_VALUE_KIND_TEXT 5u
#define GF_VALUE_KIND_POINT 6u
#define GF_VALUE_KIND_SIZE 7u
#define GF_VALUE_KIND_RECTANGLE 8u
#define GF_VALUE_KIND_INSETS 9u
#define GF_VALUE_KIND_COLOR 10u
#define GF_VALUE_KIND_FONT 11u
#define GF_VALUE_KIND_IMAGE 12u
#define GF_VALUE_KIND_ENUMERATION 13u
#define GF_VALUE_KIND_OBJECT 14u
#define GF_VALUE_KIND_COLLECTION 15u

/* Geometry in logical units. */
typedef struct gf_logical_point {
    double x;
    double y;
} gf_logical_point;

typedef struct gf_logical_size {
    double width;
    double height;
} gf_logical_size;

typedef struct gf_logical_rect {
    double x;
    double y;
    double width;
    double height;
} gf_logical_rect;

typedef struct gf_logical_insets {
    double left;
    double top;
    double right;
    double bottom;
} gf_logical_insets;

/*
 * A property or event value. kind selects the field that holds it: boolean
 * (0 or 1), signed_integer, unsigned_integer, number, text, point, size,
 * rectangle, insets, or color (0xRRGGBBAA). An ENUMERATION holds its value
 * in signed_integer; a value the library reports also carries the choice's
 * name in text ("top, left" for a combination of flags), and a value the
 * caller passes is read from signed_integer alone. FONT, IMAGE, OBJECT and
 * COLLECTION have no gf_value form yet (GF_STATUS_UNSUPPORTED_CAPABILITY).
 * Other fields are ignored on input and zero on output. The caller sets
 * struct_size; the record may grow at its tail in a later minor.
 */
typedef struct gf_value {
    uint32_t struct_size;
    gf_value_kind kind;
    uint32_t boolean;
    uint32_t color;
    int64_t signed_integer;
    uint64_t unsigned_integer;
    double number;
    gf_utf8_view text;
    gf_logical_point point;
    gf_logical_size size;
    gf_logical_rect rectangle;
    gf_logical_insets insets;
} gf_value;

/*
 * One event, lent to a callback for the duration of its invoke. sender is the
 * source object while it has an identity, else zero; it is borrowed, so a
 * callback that keeps it retains it. name is the interaction, the property
 * (for a property subscription), "invoke" (begin_invoke), or "ready",
 * "closing" or "closed" (a run). payload is null when the event carries no
 * value. A "closing" callback sets cancel to 1 to keep the window open;
 * other events ignore cancel.
 */
typedef struct gf_event {
    uint32_t struct_size;
    uint32_t cancel;
    gf_object sender;
    gf_utf8_view name;
    const gf_value* payload;
} gf_event;

/*
 * A callback the caller lends the library: invoke is called with context and
 * the event; release, which may be null, is called with context once the
 * library will not call invoke again (see Callbacks above). reserved is zero.
 * The library copies the record; the caller's copy may go at once.
 */
typedef struct gf_callback {
    uint32_t struct_size;
    uint32_t reserved;
    void (GF_CALL* invoke)(void* context, gf_event* event);
    void (GF_CALL* release)(void* context);
    void* context;
} gf_callback;

/* A subscription: the same encoding as gf_object, in its own space. */
typedef uint64_t gf_subscription;

/*
 * GF_TABLE_OBJECTS: controls and the tree. The caller holds one reference
 * for every gf_object the library hands out (create, child_at, parent) and
 * gives each back with release. A parent holds its children: a child with a
 * parent outlives its last release, and an object without one ends at its
 * last release. Its identity goes stale then, even when a parent keeps it.
 */
typedef struct gf_objects_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Creates a control of the schema type type_name, written in any
     * spelling of its id ("check_box" or "CheckBox"), with a non-empty
     * stable_id. A type that is not a control, or that needs another
     * object to be made (a dock site its workspace, say), gives
     * GF_STATUS_UNSUPPORTED_CAPABILITY; an unknown one GF_STATUS_NOT_FOUND. */
    gf_status (GF_CALL* create)(gf_utf8_view type_name, gf_utf8_view stable_id,
                                gf_object* object);
    /* Adds a reference. Any thread. */
    gf_status (GF_CALL* retain)(gf_object object);
    /* Gives back a reference. Any thread; see Threads above. */
    gf_status (GF_CALL* release)(gf_object object);
    /* Disposes the object and its subtree. Their subscriptions end (the
     * releases run before dispose returns) and their posted callbacks only
     * release. */
    gf_status (GF_CALL* dispose)(gf_object object);
    gf_status (GF_CALL* state)(gf_object object, gf_object_state* state);
    /* Copy the schema type id ("check_box"; for an application's own
     * subclass, its nearest schema type's) or the stable id into buffer
     * (not terminated); required receives its size, also with
     * GF_STATUS_BUFFER_TOO_SMALL. */
    gf_status (GF_CALL* type_id)(gf_object object, char* buffer, uint64_t capacity,
                                 uint64_t* required);
    gf_status (GF_CALL* stable_id)(gf_object object, char* buffer, uint64_t capacity,
                                   uint64_t* required);
    /* Puts child, which has no parent, under parent at index (at most the
     * child count; the count appends). Children are ordered back to front:
     * the last one paints on top. A child cannot be its own ancestor. */
    gf_status (GF_CALL* insert_child)(gf_object parent, gf_object child, uint64_t index);
    /* Takes child from parent; it then ends at its last release. */
    gf_status (GF_CALL* remove_child)(gf_object parent, gf_object child);
    gf_status (GF_CALL* child_count)(gf_object parent, uint64_t* count);
    /* A new reference to the child at index, in the order above. */
    gf_status (GF_CALL* child_at)(gf_object parent, uint64_t index, gf_object* child);
    /* A new reference to the parent, or zero when there is none. */
    gf_status (GF_CALL* parent)(gf_object object, gf_object* parent);
} gf_objects_table;

/*
 * GF_TABLE_PROPERTIES: properties by name, as the control schema
 * (manifests/control-schema.json) spells them.
 */
typedef struct gf_properties_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Reads a property into value (an output record whose struct_size the
     * caller sets, at least the 1.0 size). Text is copied into text_buffer
     * and value->text points there; text_required receives its size (zero
     * for other kinds), also with GF_STATUS_BUFFER_TOO_SMALL. An unknown or
     * unreadable property gives GF_STATUS_NOT_FOUND; a kind gf_value cannot
     * hold yet gives GF_STATUS_UNSUPPORTED_CAPABILITY. */
    gf_status (GF_CALL* get)(gf_object object, gf_utf8_view name, gf_value* value,
                             char* text_buffer, uint64_t text_capacity,
                             uint64_t* text_required);
    /* Writes a property. A value that does not convert to the property's
     * kind gives GF_STATUS_INVALID_ARGUMENT, a read-only property
     * GF_STATUS_INVALID_STATE, an unknown one GF_STATUS_NOT_FOUND. */
    gf_status (GF_CALL* set)(gf_object object, gf_utf8_view name, const gf_value* value);
    /* Returns a property to its default. GF_STATUS_UNSUPPORTED_CAPABILITY when
     * it has no reset path. */
    gf_status (GF_CALL* reset)(gf_object object, gf_utf8_view name);
} gf_properties_table;

/*
 * GF_TABLE_EVENTS. A subscription made during an emission is first called by
 * the next one; one ended during an emission is not called again, even later
 * in that emission. A subscription ends at disconnect, when its source is
 * disposed, or when its source is destroyed.
 */
typedef struct gf_events_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Subscribes to an interaction of source by its schema name. At 1.0:
     * name_changed, visible_changed and enabled_changed (boolean payload)
     * on every control; clicked on Button and CheckBox; text_changed (text
     * payload) on Button, CheckBox, Label and TextBox; checked_changed on
     * CheckBox. Another schema interaction gives
     * GF_STATUS_UNSUPPORTED_CAPABILITY, an unknown name GF_STATUS_NOT_FOUND. */
    gf_status (GF_CALL* subscribe)(gf_object source, gf_utf8_view interaction,
                                   const gf_callback* callback,
                                   gf_subscription* subscription);
    /* Subscribes to changes of one property; the event has no payload. */
    gf_status (GF_CALL* subscribe_property)(gf_object source, gf_utf8_view property,
                                            const gf_callback* callback,
                                            gf_subscription* subscription);
    /* Ends a subscription. Its release runs before this returns or, when
     * called from its own invoke, once that invoke returns. A subscription
     * that has ended gives GF_STATUS_STALE_HANDLE. */
    gf_status (GF_CALL* disconnect)(gf_subscription subscription);
} gf_events_table;

/*
 * GF_TABLE_DISPATCH: work for the UI thread. Posted callbacks run in the
 * order they were posted, while a window runs or at run_pending.
 */
typedef struct gf_dispatch_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Posts callback to run once on the UI thread with sender target and
     * name "invoke". Any thread. target is retained until then; when it has
     * been disposed by then, only release runs. At most 4096 callbacks wait
     * at once (GF_STATUS_LIMIT_EXCEEDED). */
    gf_status (GF_CALL* begin_invoke)(gf_object target, const gf_callback* callback);
    /* Completes deferred releases and runs the callbacks posted before this
     * call; invoked receives how many ran. */
    gf_status (GF_CALL* run_pending)(uint64_t* invoked);
} gf_dispatch_table;

/* gf_run_flags: how a window runs. None are defined at 1.0; a later minor
 * may add some, and this library refuses any it does not know. */
typedef uint32_t gf_run_flags;
#define GF_RUN_FLAGS_NONE 0u

/*
 * How to run a window. width and height are the client size in logical
 * units; zero keeps the default. ready, closing and closed may each be null;
 * the library calls them with sender the root and releases each once before
 * run returns.
 */
typedef struct gf_run_options {
    uint32_t struct_size;
    gf_run_flags flags;
    gf_utf8_view title;
    double width;
    double height;
    const gf_callback* ready;
    const gf_callback* closing;
    const gf_callback* closed;
} gf_run_options;

/* GF_TABLE_WINDOWS: one window at a time, on the platform's host. */
typedef struct gf_windows_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Shows root, which has no parent, in a window and runs the event loop
     * until the window closes. GF_STATUS_INVALID_STATE while another run is
     * under way. */
    gf_status (GF_CALL* run)(gf_object root, const gf_run_options* options);
    /* Asks the running window of root to close; closing may cancel it.
     * GF_STATUS_INVALID_STATE when root is not running. */
    gf_status (GF_CALL* request_close)(gf_object root);
} gf_windows_table;

/* gf_schema_kind: what a schema type is. A control is created and placed in
 * a tree; a component is owned by a control or a window; a service is
 * reached through another object and lists nothing. */
typedef uint32_t gf_schema_kind;
#define GF_SCHEMA_KIND_CONTROL 0u
#define GF_SCHEMA_KIND_COMPONENT 1u
#define GF_SCHEMA_KIND_SERVICE 2u

/* gf_schema_vocabulary: the schema's lists of names (design language 8.5
 * and 12.7), each in the library's order. MARKS leaves out the absence of a
 * mark. */
typedef uint32_t gf_schema_vocabulary;
#define GF_SCHEMA_VOCABULARY_PART_ROLES 0u
#define GF_SCHEMA_VOCABULARY_STATES 1u
#define GF_SCHEMA_VOCABULARY_CUSTOM_STATES 2u
#define GF_SCHEMA_VOCABULARY_MARKS 3u
#define GF_SCHEMA_VOCABULARY_ENGINE_INPUTS 4u
#define GF_SCHEMA_VOCABULARY_VALUES 5u
#define GF_SCHEMA_VOCABULARY_FAMILIES 6u
#define GF_SCHEMA_VOCABULARY_SEMANTIC_ROLES 7u
#define GF_SCHEMA_VOCABULARY_SEMANTIC_ACTIONS 8u
#define GF_SCHEMA_VOCABULARY_SEMANTIC_STATES 9u
#define GF_SCHEMA_VOCABULARY_SEMANTIC_FIELDS 10u
#define GF_SCHEMA_VOCABULARY_CASCADE_LEVELS 11u
#define GF_SCHEMA_VOCABULARY_EXPRESSION_OPERATIONS 12u
#define GF_SCHEMA_VOCABULARY_ENGINE_PROPERTIES 13u

/* An index that names no item system or part: a type's own parts and
 * states rather than an item system's, or a scope's own states rather than
 * a part's. */
#define GF_SCHEMA_NONE 18446744073709551615ull

/* gf_schema_property_flags: what a property allows, as bits. */
typedef uint32_t gf_schema_property_flags;
#define GF_SCHEMA_PROPERTY_FLAGS_READABLE 1u
#define GF_SCHEMA_PROPERTY_FLAGS_WRITABLE 2u
#define GF_SCHEMA_PROPERTY_FLAGS_BINDABLE 4u
#define GF_SCHEMA_PROPERTY_FLAGS_NULLABLE 8u

/* gf_schema_serialization: whether a form document writes a property, and
 * whether it writes its content rather than the value itself. */
typedef uint32_t gf_schema_serialization;
#define GF_SCHEMA_SERIALIZATION_HIDDEN 0u
#define GF_SCHEMA_SERIALIZATION_VISIBLE 1u
#define GF_SCHEMA_SERIALIZATION_CONTENT 2u

/* gf_schema_parameter_kind: what an engine primitive's parameter holds. */
typedef uint32_t gf_schema_parameter_kind;
#define GF_SCHEMA_PARAMETER_KIND_NUMBER 0u
#define GF_SCHEMA_PARAMETER_KIND_CHOICE 1u
#define GF_SCHEMA_PARAMETER_KIND_COLOR 2u

/* gf_schema_primitive_list: the name lists of an engine primitive. */
typedef uint32_t gf_schema_primitive_list;
#define GF_SCHEMA_PRIMITIVE_LIST_ROLES 0u
#define GF_SCHEMA_PRIMITIVE_LIST_STATES_READ 1u
#define GF_SCHEMA_PRIMITIVE_LIST_INPUTS_READ 2u

/*
 * The schema's records are output records: the caller sets struct_size, at
 * least the 1.0 size, and the library writes the 1.0 fields after it. Every
 * gf_utf8_view the schema table writes lends library storage that stays
 * valid and unchanged until the process ends; it is the one exception to
 * the rule that no library storage is lent. An empty composed_from or
 * enumeration means none.
 */
typedef struct gf_schema_type {
    uint32_t struct_size;
    gf_schema_kind kind;
    gf_utf8_view id;
    gf_utf8_view type_name;
    uint64_t part_count;
    uint64_t state_count;
    uint64_t custom_state_count;
    uint64_t item_system_count;
    uint64_t property_count;
    uint64_t interaction_count;
} gf_schema_type;

typedef struct gf_schema_item_system {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view name;
    uint64_t part_count;
    uint64_t state_count;
    uint64_t custom_state_count;
} gf_schema_item_system;

/* A part: role names one of GF_SCHEMA_VOCABULARY_PART_ROLES, and
 * composed_from the system the part reuses. state_count counts the states
 * the part takes on its own. */
typedef struct gf_schema_part {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view name;
    gf_utf8_view role;
    gf_utf8_view composed_from;
    uint64_t state_count;
} gf_schema_part;

/* A property: kind is a gf_value_kind, and an enumeration names its type
 * and counts its choices. */
typedef struct gf_schema_property {
    uint32_t struct_size;
    gf_value_kind kind;
    gf_utf8_view name;
    gf_utf8_view category;
    gf_utf8_view enumeration;
    uint64_t choice_count;
    gf_schema_property_flags flags;
    gf_schema_serialization serialization;
} gf_schema_property;

/* One value an enumeration property may take. */
typedef struct gf_schema_choice {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view name;
    int64_t value;
} gf_schema_choice;

/* An interaction: payload spells the handler's argument types ("ButtonBase",
 * "std::size_t, CheckState"; empty for none); mutable_payload is 1 when a
 * handler may change an argument, as a cancellable validation's. */
typedef struct gf_schema_interaction {
    uint32_t struct_size;
    uint32_t mutable_payload;
    gf_utf8_view name;
    gf_utf8_view payload;
} gf_schema_interaction;

/* The theme engine this library implements: its version, the budgets a
 * theme is checked against, and how many primitives it registers. */
typedef struct gf_schema_engine {
    uint32_t struct_size;
    uint32_t version_major;
    uint32_t version_minor;
    uint32_t reserved;
    uint64_t operations_per_device_pixel;
    uint64_t animated_operations_per_frame;
    uint64_t expression_maximum_operations;
    uint64_t expression_maximum_stack;
    uint64_t published_values_maximum;
    uint64_t transition_maximum_states;
    uint64_t transition_maximum_per_part;
    double ambient_maximum_frames_per_second;
    double ambient_maximum_window_fraction;
    uint64_t primitive_count;
} gf_schema_engine;

/* An engine primitive: the version that introduced it, its cost in
 * operations, the device-independent outsets its painting may reach beyond
 * its box, and the sizes of its name lists and parameters. */
typedef struct gf_schema_primitive {
    uint32_t struct_size;
    uint32_t introduced_major;
    uint32_t introduced_minor;
    uint32_t cost_per_device_pixel;
    uint32_t cost_fixed;
    uint32_t cost_preparation_per_device_pixel;
    gf_utf8_view name;
    gf_logical_insets outsets;
    uint64_t role_count;
    uint64_t state_read_count;
    uint64_t input_read_count;
    uint64_t parameter_count;
} gf_schema_primitive;

/* A primitive's parameter. A NUMBER has minimum, maximum and
 * default_number; a CHOICE has choice_count choices and the default
 * choice's index in default_number; a COLOR has default_color
 * (0xRRGGBBAA). animated is 1 when a transition may animate it. */
typedef struct gf_schema_parameter {
    uint32_t struct_size;
    gf_schema_parameter_kind kind;
    gf_utf8_view name;
    double minimum;
    double maximum;
    double default_number;
    uint32_t default_color;
    uint32_t animated;
    uint64_t choice_count;
} gf_schema_parameter;

/*
 * GF_TABLE_SCHEMA: the control schema (stage T1) and the engine it themes,
 * as manifests/control-schema.json records them. Types are numbered from
 * zero in the registry's order; every other list from zero in its own. An
 * index past the end gives GF_STATUS_INVALID_ARGUMENT. The first call builds
 * the schema from the library's types; it is the same in every later call.
 */
typedef struct gf_schema_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    gf_status (GF_CALL* vocabulary_count)(gf_schema_vocabulary vocabulary, uint64_t* count);
    gf_status (GF_CALL* vocabulary_at)(gf_schema_vocabulary vocabulary, uint64_t index,
                                       gf_utf8_view* name);
    gf_status (GF_CALL* type_count)(uint64_t* count);
    /* The index of a type named in any spelling of its id. */
    gf_status (GF_CALL* find_type)(gf_utf8_view name, uint64_t* type);
    gf_status (GF_CALL* type_at)(uint64_t type, gf_schema_type* record);
    gf_status (GF_CALL* item_system_at)(uint64_t type, uint64_t system,
                                        gf_schema_item_system* record);
    /* A part of the type (system GF_SCHEMA_NONE) or of one item system. */
    gf_status (GF_CALL* part_at)(uint64_t type, uint64_t system, uint64_t part,
                                 gf_schema_part* record);
    /* A state of the type or item system, or (part not GF_SCHEMA_NONE) of
     * one of its parts. */
    gf_status (GF_CALL* state_at)(uint64_t type, uint64_t system, uint64_t part, uint64_t state,
                                  gf_utf8_view* name);
    gf_status (GF_CALL* custom_state_at)(uint64_t type, uint64_t system, uint64_t state,
                                         gf_utf8_view* name);
    gf_status (GF_CALL* property_at)(uint64_t type, uint64_t property, gf_schema_property* record);
    gf_status (GF_CALL* choice_at)(uint64_t type, uint64_t property, uint64_t choice,
                                   gf_schema_choice* record);
    gf_status (GF_CALL* interaction_at)(uint64_t type, uint64_t interaction,
                                        gf_schema_interaction* record);
    gf_status (GF_CALL* engine)(gf_schema_engine* record);
    gf_status (GF_CALL* primitive_at)(uint64_t primitive, gf_schema_primitive* record);
    gf_status (GF_CALL* primitive_name_at)(uint64_t primitive, gf_schema_primitive_list list,
                                           uint64_t index, gf_utf8_view* name);
    gf_status (GF_CALL* parameter_at)(uint64_t primitive, uint64_t parameter,
                                      gf_schema_parameter* record);
    gf_status (GF_CALL* parameter_choice_at)(uint64_t primitive, uint64_t parameter,
                                             uint64_t choice, gf_utf8_view* name);
} gf_schema_table;

/* Borrowed bytes: size bytes at data. data may be null only when size is
 * zero. The callee copies what it keeps. */
typedef struct gf_bytes_view {
    const uint8_t* data;
    uint64_t size;
} gf_bytes_view;

/* The most files, and the most bytes in all (manifest included), of one
 * theme package. */
#define GF_THEME_PACKAGE_MAXIMUM_FILES 256u
#define GF_THEME_PACKAGE_MAXIMUM_BYTES 16777216u
/* The most names the library publishes values for. */
#define GF_THEME_MAXIMUM_PUBLISHED_VALUES 256u

/* One file of a theme package: its path, as the manifest lists it, and its
 * bytes. */
typedef struct gf_theme_file {
    gf_utf8_view path;
    gf_bytes_view bytes;
} gf_theme_file;

/*
 * A theme package (stage T3): its theme.json manifest and the files the
 * manifest lists, as bytes; the library reads no file system. An input
 * record: struct_size at least the 1.0 size, reserved zero. Paths are
 * UTF-8; the manifest and the files are checked by the compiler, which
 * reports what it refuses as findings rather than as a failed call.
 */
typedef struct gf_theme_package {
    uint32_t struct_size;
    uint32_t reserved;
    gf_bytes_view manifest;
    const gf_theme_file* files;
    uint64_t file_count;
} gf_theme_package;

/* gf_theme_outcome: what a load or an install did. */
typedef uint32_t gf_theme_outcome;
#define GF_THEME_OUTCOME_APPLIED 0u
#define GF_THEME_OUTCOME_REFUSED 1u

/* What a load or an install gave. An output record (as gf_abi_version_info).
 * diagnostic_count counts the findings now held (diagnostic_at). */
typedef struct gf_theme_report {
    uint32_t struct_size;
    gf_theme_outcome outcome;
    uint64_t diagnostic_count;
} gf_theme_report;

/* gf_theme_severity: an error refuses the package; a warning drops one item
 * and the rest applies. */
typedef uint32_t gf_theme_severity;
#define GF_THEME_SEVERITY_ERROR 0u
#define GF_THEME_SEVERITY_WARNING 1u

/* One finding. An output record. line and column count from 1; 0 where the
 * finding has no place in a file. */
typedef struct gf_theme_diagnostic {
    uint32_t struct_size;
    gf_theme_severity severity;
    uint32_t line;
    uint32_t column;
} gf_theme_diagnostic;

/* gf_theme_diagnostic_text: the texts of a finding. The code is as E606,
 * the name as manifest_invalid, the file the one at fault (theme.json, or a
 * sheet as <theme>/<path>); rule and property are empty outside a rule. */
typedef uint32_t gf_theme_diagnostic_text;
#define GF_THEME_DIAGNOSTIC_TEXT_CODE 0u
#define GF_THEME_DIAGNOSTIC_TEXT_NAME 1u
#define GF_THEME_DIAGNOSTIC_TEXT_FILE 2u
#define GF_THEME_DIAGNOSTIC_TEXT_RULE 3u
#define GF_THEME_DIAGNOSTIC_TEXT_PROPERTY 4u
#define GF_THEME_DIAGNOSTIC_TEXT_DETAIL 5u

/*
 * GF_TABLE_THEMES: the theme engine (stage T2) and its language (T3). The
 * library holds one theme, one image registry and one set of published
 * values for every window it shows, so a theme loaded before a run is the
 * window's from its first frame and one loaded during a run replaces the
 * running window's at once (it remeasures and repaints). UI thread; the
 * first call binds an unbound library, as create does, and the library
 * stays bound.
 */
typedef struct gf_themes_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Compiles package with its @import closure among the installed
     * packages and, when it compiles, makes it the theme (an atomic
     * replace). The compilation admits the names published so far
     * (publish_value): a package listing another is refused (E606). A
     * refused package changes nothing; either way its findings replace the
     * held ones. GF_STATUS_INVALID_STATE when a control's inline style does
     * not compile against the new theme: nothing changes then either. */
    gf_status (GF_CALL* load)(const gf_theme_package* package, gf_theme_report* report);
    /* Makes the built-in theme the theme again and clears the findings. */
    gf_status (GF_CALL* reset)(void);
    /* Verifies package whole and installs it for @import, replacing an
     * installed package of its name. A refused package changes nothing;
     * either way its findings replace the held ones. At most 256 packages
     * are installed. */
    gf_status (GF_CALL* install)(const gf_theme_package* package, gf_theme_report* report);
    /* Removes the installed package named name; removed is 1 when there
     * was one, else 0. The theme in use is unchanged. */
    gf_status (GF_CALL* remove)(gf_utf8_view name, uint32_t* removed);
    /* A finding of the last load or install, in the compiler's order. */
    gf_status (GF_CALL* diagnostic_at)(uint64_t index, gf_theme_diagnostic* record);
    gf_status (GF_CALL* diagnostic_text)(uint64_t index, gf_theme_diagnostic_text text, char* buffer,
                                         uint64_t capacity, uint64_t* required);
    /* The id of the theme in use (a package's name). */
    gf_status (GF_CALL* theme_id)(char* buffer, uint64_t capacity, uint64_t* required);
    /* Publishes a named value for themes to read (var(--name)). name is 1
     * to 64 lowercase ASCII letters, digits, hyphens and underscores,
     * starting with a letter, and value is finite, else
     * GF_STATUS_INVALID_ARGUMENT. The running window repaints the parts
     * that read it. A name once published stays admitted to later loads,
     * withdrawn or not; at most GF_THEME_MAXIMUM_PUBLISHED_VALUES names
     * (GF_STATUS_LIMIT_EXCEEDED). */
    gf_status (GF_CALL* publish_value)(gf_utf8_view name, double value);
    /* Withdraws the value of name; it then reads as not published, and the
     * reading expression gives its fallback. Withdrawing a name never
     * published changes nothing. */
    gf_status (GF_CALL* withdraw_value)(gf_utf8_view name);
    /* present is 1 and value the value when name is published; else
     * present is 0 and value is left unchanged. */
    gf_status (GF_CALL* published_value)(gf_utf8_view name, double* value, uint32_t* present);
} gf_themes_table;

/* gf_host_capability: what a host can do, one bit each; a host reports the
 * set it implements. A later minor may add bits. */
typedef uint64_t gf_host_capability;
#define GF_HOST_CAPABILITY_LIFECYCLE 1ull
#define GF_HOST_CAPABILITY_SCALE_NOTIFICATIONS 2ull
#define GF_HOST_CAPABILITY_MONITOR_GEOMETRY 4ull
#define GF_HOST_CAPABILITY_OCCLUSION 8ull
#define GF_HOST_CAPABILITY_SCHEDULED_WAKE 16ull
#define GF_HOST_CAPABILITY_POINTER_INPUT 32ull
#define GF_HOST_CAPABILITY_KEYBOARD_INPUT 64ull
#define GF_HOST_CAPABILITY_TEXT_COMPOSITION 128ull
#define GF_HOST_CAPABILITY_POINTER_CAPTURE 256ull
#define GF_HOST_CAPABILITY_CURSOR 512ull
#define GF_HOST_CAPABILITY_CLIPBOARD 1024ull
#define GF_HOST_CAPABILITY_TYPED_DRAG_DESTINATION 2048ull
#define GF_HOST_CAPABILITY_DIALOGS 4096ull
#define GF_HOST_CAPABILITY_MENUS 8192ull
#define GF_HOST_CAPABILITY_FONT_DISCOVERY 16384ull
#define GF_HOST_CAPABILITY_ACCESSIBILITY 32768ull
#define GF_HOST_CAPABILITY_TYPED_DRAG_SOURCE 65536ull
#define GF_HOST_CAPABILITY_SOUND_CUES 131072ull
#define GF_HOST_CAPABILITY_CLIPBOARD_IMAGES 262144ull
#define GF_HOST_CAPABILITY_FRAME_CAPTURE 524288ull
#define GF_HOST_CAPABILITY_NOTIFICATION_AREA 1048576ull
#define GF_HOST_CAPABILITY_NOTIFICATION_AREA_MESSAGES 2097152ull
#define GF_HOST_CAPABILITY_NOTIFICATION_AREA_MENUS 4194304ull
#define GF_HOST_CAPABILITY_RUNTIME_WINDOWS 8388608ull
#define GF_HOST_CAPABILITY_WINDOW_FRAMES 16777216ull
#define GF_HOST_CAPABILITY_SCREEN_POINTER 33554432ull
#define GF_HOST_CAPABILITY_WINDOW_AT_POINT 67108864ull
#define GF_HOST_CAPABILITY_WINDOW_ORDERING 134217728ull
#define GF_HOST_CAPABILITY_TRANSPARENT_WINDOWS 268435456ull
#define GF_HOST_CAPABILITY_WINDOW_BACKDROPS 536870912ull
#define GF_HOST_CAPABILITY_COMPOSITOR_NOTIFICATIONS 1073741824ull
#define GF_HOST_CAPABILITY_PRESENTATION_PREFERENCES 2147483648ull

/* The host's report. An output record. protocol_version is the host
 * protocol's revision; available is a set of gf_host_capability bits. */
typedef struct gf_host_capabilities {
    uint32_t struct_size;
    uint32_t protocol_version;
    gf_host_capability available;
} gf_host_capabilities;

/* gf_pointer_cursor: the pointer's shape over the running window. */
typedef uint32_t gf_pointer_cursor;
#define GF_POINTER_CURSOR_ARROW 0u
#define GF_POINTER_CURSOR_TEXT 1u
#define GF_POINTER_CURSOR_HAND 2u
#define GF_POINTER_CURSOR_CROSSHAIR 3u
#define GF_POINTER_CURSOR_RESIZE_HORIZONTAL 4u
#define GF_POINTER_CURSOR_RESIZE_VERTICAL 5u
#define GF_POINTER_CURSOR_WAIT 6u
#define GF_POINTER_CURSOR_FORBIDDEN 7u
#define GF_POINTER_CURSOR_RESIZE_DIAGONAL_DOWN 8u
#define GF_POINTER_CURSOR_RESIZE_DIAGONAL_UP 9u

/* gf_dialog_kind: which dialog a gf_dialog_request asks for. */
typedef uint32_t gf_dialog_kind;
#define GF_DIALOG_KIND_MESSAGE 0u
#define GF_DIALOG_KIND_OPEN_FILE 1u
#define GF_DIALOG_KIND_SAVE_FILE 2u
#define GF_DIALOG_KIND_FOLDER 3u
#define GF_DIALOG_KIND_COLOR 4u

typedef uint32_t gf_message_buttons;
#define GF_MESSAGE_BUTTONS_OK 0u
#define GF_MESSAGE_BUTTONS_OK_CANCEL 1u
#define GF_MESSAGE_BUTTONS_YES_NO 2u
#define GF_MESSAGE_BUTTONS_YES_NO_CANCEL 3u
#define GF_MESSAGE_BUTTONS_RETRY_CANCEL 4u

typedef uint32_t gf_message_icon;
#define GF_MESSAGE_ICON_NONE 0u
#define GF_MESSAGE_ICON_INFORMATION 1u
#define GF_MESSAGE_ICON_WARNING 2u
#define GF_MESSAGE_ICON_ERROR 3u
#define GF_MESSAGE_ICON_QUESTION 4u

/* gf_dialog_choice: the button a message dialog closed with. */
typedef uint32_t gf_dialog_choice;
#define GF_DIALOG_CHOICE_NONE 0u
#define GF_DIALOG_CHOICE_OK 1u
#define GF_DIALOG_CHOICE_CANCEL 2u
#define GF_DIALOG_CHOICE_YES 3u
#define GF_DIALOG_CHOICE_NO 4u
#define GF_DIALOG_CHOICE_RETRY 5u

/* gf_dialog_flags: allow_multiple for an open-file dialog,
 * confirm_overwrite for a save-file dialog, allow_alpha for a colour
 * dialog; a flag the kind does not read is refused. */
typedef uint32_t gf_dialog_flags;
#define GF_DIALOG_FLAGS_NONE 0u
#define GF_DIALOG_FLAGS_ALLOW_MULTIPLE 1u
#define GF_DIALOG_FLAGS_CONFIRM_OVERWRITE 2u
#define GF_DIALOG_FLAGS_ALLOW_ALPHA 4u

typedef uint32_t gf_dialog_outcome;
#define GF_DIALOG_OUTCOME_ACCEPTED 0u
#define GF_DIALOG_OUTCOME_CANCELLED 1u

/* chosen_filter when no filter of the request was chosen. */
#define GF_DIALOG_NO_FILTER 18446744073709551615ull

/* A file dialog's filter: a label and its extensions (as png, without the
 * dot). */
typedef struct gf_file_filter {
    gf_utf8_view label;
    const gf_utf8_view* extensions;
    uint64_t extension_count;
} gf_file_filter;

/*
 * A dialog. An input record: struct_size at least the 1.0 size, reserved
 * zero. Each kind reads its own fields, and the others must be empty or
 * zero: message reads title, message, buttons, icon and default_choice;
 * open_file title, initial_directory, suggested_name, filters,
 * initial_filter and allow_multiple; save_file the same but
 * allow_multiple, with default_extension and confirm_overwrite; folder
 * title and initial_directory; color title, initial_rgba (0xRRGGBBAA) and
 * allow_alpha. The host bounds texts, filters and extensions and refuses
 * more with GF_STATUS_LIMIT_EXCEEDED.
 */
typedef struct gf_dialog_request {
    uint32_t struct_size;
    gf_dialog_kind kind;
    gf_utf8_view title;
    gf_utf8_view message;
    gf_utf8_view initial_directory;
    gf_utf8_view suggested_name;
    gf_utf8_view default_extension;
    const gf_file_filter* filters;
    uint64_t filter_count;
    uint64_t initial_filter;
    gf_message_buttons buttons;
    gf_message_icon icon;
    gf_dialog_choice default_choice;
    gf_dialog_flags flags;
    uint32_t initial_rgba;
    uint32_t reserved;
} gf_dialog_request;

/* How a dialog closed. An output record. choice is a message dialog's
 * button; rgba a colour dialog's colour; path_count the paths an accepted
 * file or folder dialog chose (dialog_path_at) and chosen_filter the
 * request's filter in force when it was accepted, or GF_DIALOG_NO_FILTER. */
typedef struct gf_dialog_result {
    uint32_t struct_size;
    gf_dialog_outcome outcome;
    gf_dialog_choice choice;
    uint32_t rgba;
    uint64_t path_count;
    uint64_t chosen_filter;
} gf_dialog_result;

/*
 * GF_TABLE_HOST: the platform host's services. capabilities and
 * platform_name may be called from any thread at any time and describe the
 * host this library runs windows on. The others act on the running window
 * and are UI-thread calls during a run (GF_STATUS_INVALID_STATE otherwise);
 * a capability the host lacks gives GF_STATUS_UNSUPPORTED_CAPABILITY.
 * Tooltips are the tool_tip_text property of the controls that have one
 * (GF_TABLE_PROPERTIES).
 */
typedef struct gf_host_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    gf_status (GF_CALL* capabilities)(gf_host_capabilities* record);
    /* The host's name, as win32-dib, appkit-macos or linux-x11. */
    gf_status (GF_CALL* platform_name)(char* buffer, uint64_t capacity, uint64_t* required);
    /* Shows cursor until the pointer next moves over a control that sets
     * its own. */
    gf_status (GF_CALL* set_cursor)(gf_pointer_cursor cursor);
    /* The clipboard's text, read at each call; GF_STATUS_NOT_FOUND when it
     * holds none. */
    gf_status (GF_CALL* read_clipboard_text)(char* buffer, uint64_t capacity, uint64_t* required);
    /* Replaces the clipboard's contents with text (at most 16 MiB). */
    gf_status (GF_CALL* write_clipboard_text)(gf_utf8_view text);
    /* Shows a modal dialog over the running window and returns when it
     * closes; posted callbacks and the window's events run meanwhile. The
     * paths it chose replace the held ones. */
    gf_status (GF_CALL* show_dialog)(const gf_dialog_request* request, gf_dialog_result* result);
    /* A path the last dialog chose, in the host's order. */
    gf_status (GF_CALL* dialog_path_at)(uint64_t index, char* buffer, uint64_t capacity, uint64_t* required);
} gf_host_table;

/*
 * Fills table (table_capacity bytes, a multiple of 8 and at least 8) with
 * the functions of table_id. abi_major must equal the library's major and
 * abi_minor must not exceed its minor, else GF_STATUS_UNSUPPORTED_VERSION. The
 * library writes min(table_capacity, its table size) bytes and sets the
 * table's struct_size to the bytes written and abi_minor to its own minor.
 * An unknown table_id gives GF_STATUS_NOT_FOUND. On failure the table is not
 * written. May be called from any thread.
 */
GF_ABI_EXPORT gf_status GF_CALL gf_abi_get_table(gf_table_id table_id,
                                                 uint32_t abi_major,
                                                 uint32_t abi_minor,
                                                 void* table,
                                                 uint64_t table_capacity);

#ifdef __cplusplus
}
#endif

#endif
