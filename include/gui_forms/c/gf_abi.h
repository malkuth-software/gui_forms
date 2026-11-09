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
#define GF_ABI_MINOR 1u

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
#define GF_TABLE_DIAGNOSTICS 10u
#define GF_TABLE_CONTROLS 11u
#define GF_TABLE_TEXT_EDITING 12u
#define GF_TABLE_OVERLAYS 13u
#define GF_TABLE_PROPERTY_GRID 14u
#define GF_TABLE_CUSTOM_CONTROLS 15u
#define GF_TABLE_BINDINGS 16u
#define GF_TABLE_SUBSURFACES 17u
#define GF_TABLE_APPLICATION 18u
#define GF_TABLE_RULES 19u
/* Platform extension tables have ids from 0x100. A library has one only on
 * its platform, and reports it as a host capability; elsewhere
 * gf_abi_get_table gives GF_STATUS_NOT_FOUND. */
#define GF_TABLE_WINDOWS_PAINT_ENDPOINT 0x100u

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
 * The splashscreen (since 1.1; C++ Splashscreen, APP_WINDOW_RULES_DESIGN
 * Part S): a borderless window centred on the primary monitor, taking no
 * input, shown while the program loads, before any object or window and
 * before GF_TABLE_APPLICATION's create. It is the program's artwork plus one
 * status line and an optional progress bar in fixed built-in styling, with
 * no colours or geometry of the program's (owner ruling Q-S1 (a)). It
 * closes when the library's first window is ready (the handover), or at
 * splashscreen_close. One shows at a time.
 */
typedef uint64_t gf_splashscreen;

/* The most bytes of one splashscreen status line. */
#define GF_SPLASHSCREEN_MAXIMUM_STATUS_BYTES 512u
/* The artwork's least and greatest extent, in pixels. */
#define GF_SPLASHSCREEN_MINIMUM_EXTENT 160u
#define GF_SPLASHSCREEN_MAXIMUM_EXTENT 1600u

/*
 * What a splashscreen shows. An input record: struct_size at least the 1.1
 * size. artwork holds artwork_size bytes of a PNG file whose pixel size,
 * one logical unit per pixel, is the window's size (each extent from
 * GF_SPLASHSCREEN_MINIMUM_EXTENT to _MAXIMUM_EXTENT); the status and the
 * bar are drawn over its bottom edge. status is UTF-8, at most
 * GF_SPLASHSCREEN_MAXIMUM_STATUS_BYTES. With show_progress non-zero a bar
 * shows progress, in [0, 1]. Everything is copied during the call.
 */
typedef struct gf_splashscreen_options {
    uint32_t struct_size;
    uint32_t show_progress;
    const uint8_t* artwork;
    uint64_t artwork_size;
    gf_utf8_view status;
    double progress;
} gf_splashscreen_options;

/*
 * GF_TABLE_RUNTIME: version and error detail. Its functions may be called
 * from any thread, and they never record a failure of their own, so reading
 * the detail does not replace it. The splashscreen functions (since 1.1)
 * are the exception to the threads rule: they run on the thread that shows
 * the splashscreen, which should be the thread that will be the UI thread,
 * and may be called before the library has a UI thread; another thread is
 * refused with GF_STATUS_WRONG_THREAD.
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
    /*
     * Since 1.1. Shows the splashscreen of options and presents its first
     * frame before it returns; splashscreen receives its handle, valid until
     * splashscreen_close. GF_STATUS_INVALID_ARGUMENT for a record smaller
     * than the 1.1 size, a null splashscreen, artwork that is not a PNG file
     * or has an extent out of range, a status that is not UTF-8 or too long,
     * or a progress outside [0, 1] with show_progress; GF_STATUS_INVALID_STATE
     * while another splashscreen's handle is open (shown, or handed over and
     * not yet closed); GF_STATUS_UNSUPPORTED_CAPABILITY when the host has no
     * splashscreen (no display, a host without
     * GF_TRANSIENT_SURFACE_SPLASHSCREEN) or could not present it. Nothing
     * shows after a failure, and the program goes on without one.
     */
    gf_status (GF_CALL* splashscreen_show)(const gf_splashscreen_options* options, gf_splashscreen* splashscreen);
    /*
     * Since 1.1. Replaces the status line and presents the frame at once.
     * After the handover it accepts and presents nothing.
     * GF_STATUS_STALE_HANDLE for a handle that is not the current
     * splashscreen's; GF_STATUS_INVALID_ARGUMENT, changing nothing, for text
     * that is not UTF-8 or too long; GF_STATUS_UNSUPPORTED_CAPABILITY when
     * the frame could not be presented.
     */
    gf_status (GF_CALL* splashscreen_set_status)(gf_splashscreen splashscreen, gf_utf8_view status);
    /*
     * Since 1.1. Shows the bar at progress, in [0, 1], or hides it when
     * show_progress is zero (progress is then ignored), and presents the
     * frame at once; as set_status otherwise.
     */
    gf_status (GF_CALL* splashscreen_set_progress)(gf_splashscreen splashscreen, uint32_t show_progress,
                                                   double progress);
    /*
     * Since 1.1. Closes the splashscreen's window if it still shows and ends
     * its handle; another may be shown after. GF_STATUS_STALE_HANDLE for a
     * handle that is not the current splashscreen's.
     */
    gf_status (GF_CALL* splashscreen_close)(gf_splashscreen splashscreen);
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
 * into the library, or by a running window's event loop. Since 1.1 a
 * threaded sub-surface's thread is a second kind of owning thread: the
 * objects it creates are its own, as GF_TABLE_SUBSURFACES describes, and
 * "the UI thread" below reads "the object's thread" for them.
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

/* gf_font_role: the face family a font draws from. */
typedef uint32_t gf_font_role;
#define GF_FONT_ROLE_CONTROL 0u
#define GF_FONT_ROLE_CONTENT 1u
#define GF_FONT_ROLE_MONOSPACE 2u

/*
 * A font: its role, weight (1 to 1000; 400 regular, 700 bold), italic (0 or
 * 1), size in logical pixels (finite, above zero) and the logical pixels
 * added between grapheme clusters (finite, from -size/4 to size).
 */
typedef struct gf_font {
    gf_font_role role;
    uint32_t weight;
    uint32_t italic;
    uint32_t reserved;
    double size;
    double letter_spacing;
} gf_font;

/*
 * A property or event value. kind selects the field that holds it: boolean
 * (0 or 1), signed_integer, unsigned_integer, number, text, point, size,
 * rectangle, insets, or color (0xRRGGBBAA). An ENUMERATION holds its value
 * in signed_integer; a value the library reports also carries the choice's
 * name in text ("top, left" for a combination of flags), and a value the
 * caller passes is read from signed_integer alone. A FONT is in font. An
 * IMAGE is the image's id in unsigned_integer (themes.load_image; zero is
 * no image). A COLLECTION the library reports holds its element count in
 * unsigned_integer, and each element is read on its own
 * (properties.element_at, events.field_element); a COLLECTION the caller
 * passes lists its elements in elements and element_count, each a gf_value
 * of the collection's item kind (not itself a COLLECTION), read during the
 * call only. An OBJECT is an object's identity in unsigned_integer (event
 * fields); a property of an object value has no gf_value form yet
 * (GF_STATUS_UNSUPPORTED_CAPABILITY), and the schema lists none.
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
    gf_font font;
    const struct gf_value* elements;
    uint64_t element_count;
} gf_value;

/* gf_event_field_flags: what a callback may do with an event field. */
typedef uint32_t gf_event_field_flags;
/* events.write_field accepts a value of the field's kind. */
#define GF_EVENT_FIELD_WRITABLE 1u
/* The field may be absent: its value is then NULL, and a writable one also
 * accepts NULL. */
#define GF_EVENT_FIELD_OPTIONAL 2u
/* A writable field that accepts a value of any kind with a gf_value form
 * (a binding value, such as BindingConvertEvent's). */
#define GF_EVENT_FIELD_ANY_KIND 4u

/*
 * Event fields. An interaction's payload reaches the callback as named
 * fields, in the order of the C++ payload's members:
 * - a scalar payload is one field named "value" (and is also the event's
 *   payload); a struct payload is its members ("cancel", "row_id"), nested
 *   members dotted ("font.size"); several arguments are "value_1",
 *   "value_2", ..., a struct argument's members below them
 *   ("value_1.cancel");
 * - an enumeration is ENUMERATION: signed_integer and its choice's name in
 *   text ("a, b" for a combination of flags; empty for an unknown value);
 * - an object is OBJECT, its identity in unsigned_integer (zero when it has
 *   none to lend); the identity is lent for the invoke, so a callback that
 *   keeps the object retains it;
 * - a collection is COLLECTION, its element count in unsigned_integer; its
 *   elements are read with events.field_element;
 * - a reference to the sender adds no field; a member no field carries yet
 *   is left out (the C ABI track lists them).
 * kind is the field's declared kind; the value's own kind is NULL when an
 * OPTIONAL field is absent, and FONT, IMAGE, OBJECT or COLLECTION with no
 * other content for a binding value that has no gf_value form yet. Names,
 * values and the text they reference are lent for the invoke. The record's
 * layout is fixed: an array of them has the stride sizeof(gf_event_field).
 */
typedef struct gf_event_field {
    gf_utf8_view name;
    gf_value_kind kind;
    gf_event_field_flags flags;
    const gf_value* value;
} gf_event_field;

/*
 * One event, lent to a callback for the duration of its invoke. sender is the
 * source object while it has an identity, else zero; it is borrowed, so a
 * callback that keeps it retains it. name is the interaction, the property
 * (for a property subscription), "invoke" (begin_invoke), or "ready",
 * "closing" or "closed" (a run). payload is null when the event carries no
 * single value. A "closing" callback sets cancel to 1 to keep the window
 * open; other events ignore cancel. fields are the interaction's payload as
 * named fields (Event fields above); field_count is zero for the other
 * events. A callback writes a writable field with events.write_field; the
 * library applies what was written once the invoke returns.
 */
typedef struct gf_event {
    uint32_t struct_size;
    uint32_t cancel;
    gf_object sender;
    gf_utf8_view name;
    const gf_value* payload;
    uint64_t field_count;
    const gf_event_field* fields;
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
/* gf_create_flags: how objects.create_with reads its arguments. */
typedef uint32_t gf_create_flags;
/* The choices combine as flags (a flags editor's enumeration). */
#define GF_CREATE_FLAGS_COMBINABLE 1u

/* The most choices a caller's enumeration has (create_with), and the most
 * bytes of its name and of each choice's name. */
#define GF_CREATE_MAXIMUM_CHOICES 256u
#define GF_CREATE_MAXIMUM_NAME_BYTES 256u

/* One choice of an enumeration the caller defines. */
typedef struct gf_create_choice {
    gf_utf8_view name;
    int64_t value;
} gf_create_choice;

/*
 * What objects.create_with makes a type with. The caller sets struct_size;
 * the library reads the record and the choices during the call only.
 * owner is the object the new one needs:
 * - anchored_overlay_layer: its anchor control;
 * - dock_site: its dock_workspace;
 * - page_strip: the page_view or navigation_pane whose pages it shows;
 * - color_command, font_command, gallery_command: the command it extends;
 * - background_worker, binding_context, binding_source, error_provider,
 *   help_provider, image_list, notify_icon, ribbon_tool_tip,
 *   semantic_feedback, timer, tool_tip: the window (windows.create) it
 *   binds to.
 * A flags_value_editor takes no owner but an enumeration: its name, its
 * choices (1 to GF_CREATE_MAXIMUM_CHOICES, distinct non-empty names) and
 * value, its current value; objects.create also makes one, empty until its
 * enumeration_name, flag_names, none_name and checked_flags properties are
 * written. The new object keeps what it needs of its owner
 * alive for as long as it lives, except a window. For a component bound to a
 * window, owner means only the window it binds to, as in C++: the caller's
 * references own the component, the component observes the window without
 * keeping it, and when the window is destroyed the component stays alive,
 * unbound (GF_TABLE_WINDOWS). Fields a type does not read are zero.
 */
typedef struct gf_create_arguments {
    uint32_t struct_size;
    gf_create_flags flags;
    gf_object owner;
    gf_utf8_view enumeration;
    const gf_create_choice* choices;
    uint64_t choice_count;
    int64_t value;
} gf_create_arguments;

typedef struct gf_objects_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Creates an object of the schema type type_name, written in any
     * spelling of its id ("check_box" or "CheckBox"), with a stable_id: one
     * or more '.'-separated segments of lowercase ASCII letters and digits
     * joined by single '-' ("settings.ok-button"; a Windows Forms name such
     * as "textBox1" is spelled "text-box1"), GF_STATUS_INVALID_ARGUMENT with
     * a message stating the rule otherwise. A control is made from its
     * stable id, or a component made
     * from nothing (a command, a file dialog) or from its id (a context
     * menu, a dock workspace). A type that needs another object (a dock site
     * its workspace or a timer its window, say: create_with), and the types
     * neither slot makes (a binding, which GF_TABLE_BINDINGS makes, and
     * live_surface), give GF_STATUS_UNSUPPORTED_CAPABILITY; an unknown one GF_STATUS_NOT_FOUND.
     * The objects table takes components wherever it takes an object,
     * except the tree slots, which take controls
     * (GF_STATUS_WRONG_HANDLE_KIND), and the properties table, which takes
     * controls. A component's stable_id is empty unless its type has one
     * (a context menu, a dock workspace). */
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
    /* Creates an object of a type that needs another object, as
     * gf_create_arguments describes. An owner of the wrong type gives
     * GF_STATUS_WRONG_HANDLE_KIND, a stale one GF_STATUS_STALE_HANDLE, a
     * malformed enumeration GF_STATUS_INVALID_ARGUMENT; a type create makes
     * is made as create makes it when arguments asks nothing of it. */
    gf_status (GF_CALL* create_with)(gf_utf8_view type_name, gf_utf8_view stable_id,
                                     const gf_create_arguments* arguments, gf_object* object);
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
    /* The element at index of the COLLECTION property name, as get reports
     * a value (text copied into text_buffer). GF_STATUS_INVALID_ARGUMENT
     * when the property is not a collection, GF_STATUS_NOT_FOUND when index
     * is not below its count. */
    gf_status (GF_CALL* element_at)(gf_object object, gf_utf8_view name, uint64_t index, gf_value* value,
                                    char* text_buffer, uint64_t text_capacity, uint64_t* text_required);
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
    /* Subscribes to an interaction of source by its schema name: any
     * interaction the schema lists for source's type (schema.interaction_at).
     * The event's fields carry the payload (Event fields above). A name the
     * type does not raise gives GF_STATUS_NOT_FOUND. */
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
    /* Writes field (an index below event's field_count) of the event a
     * callback is running for, from within that callback. The field must be
     * WRITABLE and value of its kind (or NULL when it is OPTIONAL, or any
     * kind when it is ANY_KIND), well formed and within the C++ member's
     * range or choices; text is copied. The record the callback sees changes
     * at once; the library applies the written fields when the invoke
     * returns. GF_STATUS_INVALID_STATE when event is not running,
     * GF_STATUS_NOT_FOUND for an index past the fields,
     * GF_STATUS_INVALID_ARGUMENT for a field that is not writable or a value
     * it does not admit; nothing is written then. */
    gf_status (GF_CALL* write_field)(gf_event* event, uint64_t field, const gf_value* value);
    /* Reads element index of a COLLECTION field of the event a callback is
     * running for, from within that callback, into element (the caller sets
     * struct_size); its text is lent for the invoke.
     * GF_STATUS_UNSUPPORTED_CAPABILITY when the elements are structs. */
    gf_status (GF_CALL* field_element)(const gf_event* event, uint64_t field, uint64_t index,
                                       gf_value* element);
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

/* An interaction: payload spells the handler's argument types ("ButtonBase",
 * "std::size_t, CheckState"; empty for none); mutable_payload is 1 when a
 * handler may change an argument, as a cancellable validation's. The
 * schema table lists a type's in it, and the windows table a window's. */
typedef struct gf_schema_interaction {
    uint32_t struct_size;
    uint32_t mutable_payload;
    gf_utf8_view name;
    gf_utf8_view payload;
} gf_schema_interaction;

/*
 * How to run a window. width and height are the client size in logical
 * units; zero keeps the default. ready, closing and closed may each be null;
 * the library calls them with sender the root and releases each once before
 * run returns.
 *
 * Since 1.1 (read when struct_size covers them): minimum_width and
 * minimum_height, the smallest client size the user may resize the window
 * to, in logical units, from 0 to 32768; a side of 0 sets no minimum there
 * beyond the host's own, as Windows Forms' empty MinimumSize. A record of the
 * 1.0 size has no minimum. The client size may not be below the minimum.
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
    double minimum_width;
    double minimum_height;
} gf_run_options;

/* gf_owned_window_flags: how show_modal and show_owned open a window. */
typedef uint32_t gf_owned_window_flags;
#define GF_OWNED_WINDOW_FLAGS_NONE 0u
/* The host's tool window style: a smaller title bar and no taskbar button. */
#define GF_OWNED_WINDOW_TOOL 1u

/*
 * How show_modal and show_owned open a window over the running one (since
 * 1.1). owner is the root of the running window or of an owned window that
 * is open; zero means the running window. width and height are the client
 * size in logical units; zero keeps the default. ready, closing and closed
 * may each be null; the library calls them with sender the window's root
 * and releases each once, on the UI thread, after the window has closed
 * (for show_owned, possibly after show_owned has returned).
 */
typedef struct gf_owned_window_options {
    uint32_t struct_size;
    gf_owned_window_flags flags;
    gf_utf8_view title;
    double width;
    double height;
    gf_object owner;
    const gf_callback* ready;
    const gf_callback* closing;
    const gf_callback* closed;
} gf_owned_window_options;

/* gf_dialog_result_code: a modal window's result, Form.DialogResult's
 * values (a button's dialog_result property takes the same names). */
typedef uint32_t gf_dialog_result_code;
#define GF_DIALOG_RESULT_NONE 0u
#define GF_DIALOG_RESULT_OK 1u
#define GF_DIALOG_RESULT_CANCEL 2u
#define GF_DIALOG_RESULT_ABORT 3u
#define GF_DIALOG_RESULT_RETRY 4u
#define GF_DIALOG_RESULT_IGNORE 5u
#define GF_DIALOG_RESULT_YES 6u
#define GF_DIALOG_RESULT_NO 7u
#define GF_DIALOG_RESULT_TRY_AGAIN 10u
#define GF_DIALOG_RESULT_CONTINUE 11u

/* gf_window_property_flags: the window properties (schema type "window",
 * since 1.1), one bit each, as window_properties reports them. */
typedef uint32_t gf_window_property_flags;
#define GF_WINDOW_PROPERTY_TEXT 1u
#define GF_WINDOW_PROPERTY_ICON 2u
#define GF_WINDOW_PROPERTY_FORM_BORDER_STYLE 4u
#define GF_WINDOW_PROPERTY_MINIMIZE_BOX 8u
#define GF_WINDOW_PROPERTY_MAXIMIZE_BOX 16u
#define GF_WINDOW_PROPERTY_CONTROL_BOX 32u
#define GF_WINDOW_PROPERTY_WINDOW_STATE 64u
#define GF_WINDOW_PROPERTY_TOP_MOST 128u
#define GF_WINDOW_PROPERTY_OPACITY 256u
#define GF_WINDOW_PROPERTY_START_POSITION 512u
#define GF_WINDOW_PROPERTY_SHOW_IN_TASKBAR 1024u
#define GF_WINDOW_PROPERTY_MINIMUM_SIZE 2048u

/*
 * GF_TABLE_WINDOWS: one window at a time, on the platform's host.
 *
 * A window is an object (type id "window", stable id its root's) that create
 * makes over a root and run then shows. The components that need a window
 * (timer, tool_tip and the others objects.create_with lists) are made with a
 * window as owner before or during its run. They bind to the window as the
 * C++ components constructed over a Window do: they observe it and do not
 * keep it, and it does not keep them. The caller's references own both.
 * When the run ends the window is destroyed and disposed; its components
 * stay alive but unbound, and refuse what needs the window. Disposing a
 * window that has not run destroys it; during its run, disposing it only
 * stops new components binding to it.
 *
 * A window raises interactions as a component does (since 1.1):
 * events.subscribe takes their names on the window object, and
 * interaction_count and interaction_at list them (active_changed,
 * backdrop_changed, dialog_result_changed, focused_control_changed,
 * help_requested, key_down, key_press, key_state_changed, key_up,
 * presentation_changed, theme_changed).
 *
 * Key input (since 1.1). A window's key_down and key_up reach a subscriber
 * before the focused control (Windows Forms' KeyPreview), and every
 * control raises preview_key_down, key_down, key_press and key_up; their
 * fields are key_code (the Windows Forms Keys code, the same on every
 * host), physical_key (the USB HID usage), scan_code (the host's own
 * hardware code, zero when none), modifiers, repeat (an auto-repeated
 * down), released_by_deactivation, and the writable handled and
 * suppress_key_press (preview_key_down: is_input_key; key_press: text,
 * key_char and handled). key_state_changed is the raw press and release
 * of each physical key, once each and never for an auto-repeat, before
 * anything can consume it, with the releases the window makes when it
 * loses activation. key_state asks whether a key is down now. Its subscriptions
 * end when the window is destroyed, each callback released once on the UI
 * thread; a disposed window refuses new ones with GF_STATUS_DISPOSED.
 *
 * During the run, show_modal and show_owned open further windows over a
 * root control (since 1.1), owned by the running window or by another of
 * them: a modal window, as Form.ShowDialog shows one, or a non-modal owned
 * window, as Form.Show(owner). Their stable id is their root's, unique
 * among open windows ("main" is the running window's). request_close
 * closes them too. They share the library's theme, images and published
 * values as they stand when the window opens.
 *
 * A window has properties (since 1.1), Form's for the native window: text
 * (the title; until written, the run's options.title), icon (an image from
 * themes.load_image, or none), form_border_style, minimize_box,
 * maximize_box, control_box, window_state (the program's request, not a
 * report of the native state), top_most, opacity (0 to 1), start_position
 * (applies before the window is first shown), show_in_taskbar and
 * minimum_size (the client area's, zero on a side for the window's own).
 * The schema's "window" type lists them with their kinds and choices;
 * properties.get, set and reset take their names on a window from create,
 * before and during its run. A refused value is GF_STATUS_INVALID_ARGUMENT
 * and changes nothing. A written property reaches the host when the run
 * starts and at once during it, where the host applies that property;
 * window_properties reports which it does (a host without a taskbar or a
 * window icon reports those unsupported).
 */
typedef struct gf_windows_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Runs window, a window from create that has not run, or root, a control
     * with no parent that the run makes a window for, and runs the event
     * loop until the window closes. The callbacks' sender is the object
     * given. options.width and height, when not zero, resize the window.
     * GF_STATUS_INVALID_STATE while another run is under way.
     * GF_STATUS_INVALID_ARGUMENT for a window the run refuses, with a
     * message naming the reason (for a client size below the minimum, both
     * sizes). */
    gf_status (GF_CALL* run)(gf_object window_or_root, const gf_run_options* options);
    /* Asks the running window, given as the object passed to run, to close;
     * closing may cancel it. GF_STATUS_INVALID_STATE when it is not
     * running. */
    gf_status (GF_CALL* request_close)(gf_object window_or_root);
    /* Makes a window over root, a control with no parent and not in a
     * window, at the host's default size. The window holds root until it is
     * destroyed. GF_STATUS_INVALID_STATE when root has a parent or a
     * window. */
    gf_status (GF_CALL* create)(gf_object root, gf_object* window);
    /* The number of interactions a window raises (since 1.1). Needs no
     * window or engine, since every window raises the same ones; any thread. */
    gf_status (GF_CALL* interaction_count)(uint64_t* count);
    /* Interaction index (below interaction_count) as a schema record, the
     * form schema.interaction_at gives a type's (since 1.1); any thread.
     * The views stay valid while the library is loaded.
     * GF_STATUS_INVALID_ARGUMENT past the end. */
    gf_status (GF_CALL* interaction_at)(uint64_t interaction, gf_schema_interaction* record);
    /* Raises the window's help request as a message box's Help button does
     * (Window::request_help, not keyboard-initiated): its help_requested handlers run, then its
     * help providers are offered it until one handles it. handled receives
     * 1 when a handler or provider handled it, else 0 (since 1.1). UI thread.
     * GF_STATUS_DISPOSED once the window is gone. */
    gf_status (GF_CALL* request_help)(gf_object window, uint32_t* handled);
    /* down receives 1 when physical_key (a USB HID usage) is down in window
     * now, as the window has seen it (pressed, not released, not released by
     * the window losing activation), else 0 (since 1.1). UI thread.
     * GF_STATUS_INVALID_ARGUMENT without down; GF_STATUS_DISPOSED once the
     * window is gone. */
    gf_status (GF_CALL* key_state)(gf_object window, uint32_t physical_key, uint32_t* down);
    /* Opens a window over root, a control with no parent and not in a
     * window, owned by options.owner, and runs a nested event loop until it
     * closes (since 1.1): the run's other windows refuse the user's input
     * meanwhile, while posted work, events of other sources, timers and
     * repaint continue in every window. A dialog result other than none
     * set on the window (a button's dialog_result, the window's own) asks
     * it to close; its closing callback may cancel. Returns once the
     * window has closed and its closed callback has run, with result its
     * dialog result (GF_DIALOG_RESULT_CANCEL when it closed without one).
     * When the run ends during the loop (the running window closed), the
     * loop ends with GF_STATUS_CANCELLED and the window closes with the
     * run. A callback inside the loop may call show_modal again, up to 8
     * deep (GF_STATUS_LIMIT_EXCEEDED beyond). UI thread, during a run
     * (GF_STATUS_INVALID_STATE otherwise); GF_STATUS_UNSUPPORTED_CAPABILITY
     * where the host has no nested loop; GF_STATUS_INVALID_STATE when root
     * has a parent or a window or its stable id is taken; result may be
     * null. Nothing is opened on a refusal. */
    gf_status (GF_CALL* show_modal)(gf_object root, const gf_owned_window_options* options,
                                    gf_dialog_result_code* result);
    /* Opens a window over root owned by options.owner, as show_modal does,
     * and returns once it is ready, without a nested loop (since 1.1). The
     * window closes through request_close, its close box, its owner
     * closing or the run ending. The same refusals as show_modal, without
     * the depth limit. */
    gf_status (GF_CALL* show_owned)(gf_object root, const gf_owned_window_options* options);
    /* The window properties window's host applies (since 1.1): supported
     * receives the GF_WINDOW_PROPERTY_* bits of those the host honours, 0
     * before the window runs; applied the bits of the written properties
     * the host applied when each was last sent. Either output may be null.
     * UI thread. GF_STATUS_WRONG_HANDLE_KIND for an object that is not a
     * window; GF_STATUS_DISPOSED once the window is gone. */
    gf_status (GF_CALL* window_properties)(gf_object window, gf_window_property_flags* supported,
                                           gf_window_property_flags* applied);
    /* The focused control of a window (since 1.1): window_or_root is a
     * window from create, or a control in a window (a root passed to run,
     * a root of a show_modal or show_owned window, or any control under
     * one). control receives a new reference to the window's focused
     * control, which the caller releases, or zero when none has focus; a
     * composite field whose edit part has the caret is reported, not the
     * edit part (Windows Forms' ActiveControl).
     * When a modal window closes, or a disabled window's root is enabled
     * again, the window's last focused control takes focus back. UI
     * thread. GF_STATUS_INVALID_STATE for a control not in a window;
     * GF_STATUS_DISPOSED for a window that has been disposed or whose run
     * has ended. */
    gf_status (GF_CALL* focused_control)(gf_object window_or_root, gf_object* control);
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

/* gf_schema_effect_time_model: what drives an effect program. A static
 * pose runs on a geometry or state change only; a transition effect follows
 * its part's transition; an ambient effect is ticked on the motion clock. */
typedef uint32_t gf_schema_effect_time_model;
#define GF_SCHEMA_EFFECT_TIME_MODEL_STATIC_POSE 0u
#define GF_SCHEMA_EFFECT_TIME_MODEL_TRANSITION 1u
#define GF_SCHEMA_EFFECT_TIME_MODEL_AMBIENT 2u

/* gf_schema_effect_input: the most input an effect program may take, when
 * its host grants it. */
typedef uint32_t gf_schema_effect_input;
#define GF_SCHEMA_EFFECT_INPUT_NONE 0u
#define GF_SCHEMA_EFFECT_INPUT_POINTER 1u
#define GF_SCHEMA_EFFECT_INPUT_POINTER_AND_KEYS 2u

/* gf_schema_effect_flags: an effect program's facts, as bits. VERIFIED: the
 * gui_forms Clang plugin's effect pass checked its bounds when this library
 * was built (an unverified effect still runs, metered). SETUP_ON_GEOMETRY:
 * its setup runs again when its part's geometry changes. */
typedef uint32_t gf_schema_effect_flags;
#define GF_SCHEMA_EFFECT_FLAGS_VERIFIED 1u
#define GF_SCHEMA_EFFECT_FLAGS_SETUP_ON_GEOMETRY 2u

/* An effect program the library registers (since 1.1): its name, as a
 * control's effect property takes it; parent, the schema id of the type
 * that hosts it; the engine version that introduced it; its declared costs
 * in operations (cost_fixed and cost_per_device_pixel a frame, setup_cost
 * once); the most bytes its state may take; its ambient frame rate; and the
 * device-independent outsets its drawing may reach beyond its part. */
typedef struct gf_schema_effect {
    uint32_t struct_size;
    gf_schema_effect_time_model time_model;
    gf_utf8_view name;
    gf_utf8_view parent;
    uint32_t engine_version;
    gf_schema_effect_input input;
    uint32_t cost_fixed;
    uint32_t cost_per_device_pixel;
    uint32_t setup_cost;
    gf_schema_effect_flags flags;
    uint64_t state_budget;
    double frames_per_second;
    gf_logical_insets outsets;
} gf_schema_effect;

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
    /* The number of effect programs the library registers (since 1.1). */
    gf_status (GF_CALL* effect_count)(uint64_t* count);
    /* Effect index (below effect_count), in registration order (since
     * 1.1). GF_STATUS_INVALID_ARGUMENT past the end. */
    gf_status (GF_CALL* effect_at)(uint64_t effect, gf_schema_effect* record);
    /* The properties a type provides to each of its children, such as a
     * grid panel's cell (since 1.1): how many, one of them (below the
     * count), and one choice of it, as property_at and choice_at give the
     * type's own. GF_STATUS_INVALID_ARGUMENT past the end. */
    gf_status (GF_CALL* child_property_count)(uint64_t type, uint64_t* count);
    gf_status (GF_CALL* child_property_at)(uint64_t type, uint64_t property, gf_schema_property* record);
    gf_status (GF_CALL* child_choice_at)(uint64_t type, uint64_t property, uint64_t choice,
                                         gf_schema_choice* record);
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
    /* Admits an image file (PNG is the only format) of size bytes into the
     * image registry and writes its id, which IMAGE values name. The bytes
     * are read during the call only. A file that is not a valid PNG gives
     * GF_STATUS_INVALID_ARGUMENT, one past the registry's limits
     * GF_STATUS_LIMIT_EXCEEDED; nothing is admitted then. */
    gf_status (GF_CALL* load_image)(const uint8_t* bytes, uint64_t size, uint64_t* image);
    /* Removes an image; controls showing it show none and repaint.
     * GF_STATUS_NOT_FOUND for an id the registry does not hold. */
    gf_status (GF_CALL* remove_image)(uint64_t image);
    /*
     * Copies width x height pixels of 32-bit premultiplied BGRA (row_bytes
     * apart, at least width * 4) into the image registry, as load_image does
     * a PNG; image receives the new id. pixels may be null only for an empty
     * image. Fails with GF_STATUS_LIMIT_EXCEEDED past the registry's limits,
     * else GF_STATUS_INVALID_ARGUMENT, leaving image unwritten.
     */
    gf_status (GF_CALL* load_pixels)(const uint8_t* pixels, uint32_t width, uint32_t height, uint64_t row_bytes,
                                     uint64_t* image);
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
/* The library has GF_TABLE_WINDOWS_PAINT_ENDPOINT (Windows only). */
#define GF_HOST_CAPABILITY_WINDOWS_PAINT_ENDPOINT 4294967296ull
/* Since 1.1: transient surfaces, the host's own windows for tooltips and
 * the splashscreen (Splash: no input) and for menus, drop-downs and pickers
 * (Popup: input through the owner window). Which kinds, and whether they
 * are translucent, is gf_host_capabilities.transient_surfaces. Without it
 * each kind is shown inside its owner window instead. */
#define GF_HOST_CAPABILITY_TRANSIENT_SURFACES 8589934592ull
/* Since 1.1: the host shapes a window's corners and reports them; the
 * window outer band follows the report (a rounded band where the host
 * rounds). Which shapes it may give is
 * gf_host_capabilities.window_corner_shapes. Without it a window's corners
 * are square to the band. Windows 11 (DWM) today. */
#define GF_HOST_CAPABILITY_WINDOW_CORNERS 17179869184ull
/* Since 1.1: the host reports the native window's state: full screen, and
 * the maximize, snap and restore changes that can reshape its corners. A
 * window outer band declared to hide in full screen hides only where the
 * host reports it. Windows, macOS and X11. */
#define GF_HOST_CAPABILITY_WINDOW_STATE_REPORTS 34359738368ull

/* gf_window_corner_shape (since 1.1): the corner shapes a host may give a
 * window, one bit each. A window's corners at a given moment follow its
 * state: Windows squares a maximized, snapped or full-screen window's. */
typedef uint32_t gf_window_corner_shape;
#define GF_WINDOW_CORNER_SHAPE_SQUARE 1u
#define GF_WINDOW_CORNER_SHAPE_ROUNDED 2u

/* gf_transient_surface_kind (since 1.1): the transient surfaces a host
 * opens, one bit each, and how they are drawn. */
typedef uint32_t gf_transient_surface_kind;
#define GF_TRANSIENT_SURFACE_TOOLTIP 1u
#define GF_TRANSIENT_SURFACE_SPLASHSCREEN 2u
#define GF_TRANSIENT_SURFACE_POPUP 4u
/* Drawn with per-pixel alpha: the library's shadow and fade around them. */
#define GF_TRANSIENT_SURFACE_TRANSLUCENT 8u
/* May be drawn opaque, with no shadow or fade, as the desktop decides while
 * the program runs (X11 without a compositing manager). Reported with
 * TRANSLUCENT when the host draws either way. */
#define GF_TRANSIENT_SURFACE_OPAQUE 16u

/* The host's report. An output record (struct_size at least the 1.0 size,
 * 16; a smaller record receives the fields that fit it). protocol_version
 * is the host protocol's revision; available is a set of
 * gf_host_capability bits. Since 1.1, transient_surfaces is a set of
 * gf_transient_surface_kind bits, zero without
 * GF_HOST_CAPABILITY_TRANSIENT_SURFACES; reserved is zero;
 * window_corner_shapes is a set of gf_window_corner_shape bits, zero
 * without GF_HOST_CAPABILITY_WINDOW_CORNERS; window_band_maximum_width is
 * the widest window outer band, in logical units, a window may declare (a
 * limit of the library, the same on every host). */
typedef struct gf_host_capabilities {
    uint32_t struct_size;
    uint32_t protocol_version;
    gf_host_capability available;
    gf_transient_surface_kind transient_surfaces;
    uint32_t reserved;
    gf_window_corner_shape window_corner_shapes;
    uint32_t window_band_maximum_width;
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
 * GF_TABLE_DIAGNOSTICS: what a thread is doing. Its functions may be called
 * from any thread and read only the calling thread's state; they open no
 * shadow frame of their own.
 *
 * Shadow frames. Each thread keeps a stack of logical frames, which a
 * debugger or a crash report reads where the native frames say nothing a
 * program's author would recognise. The library opens a frame on entry to
 * every function of the tables above except the runtime table, around each
 * event and posted callback it calls, and around a window's input dispatch,
 * layout and paint, and closes it on every way out. A callback never unwinds
 * into the library, so a binding that catches its language's exceptions
 * before returning keeps the stack balanced. A frame holds numbers and
 * copied text, never an address. A thread records its outermost
 * GF_SHADOW_STACK_CAPACITY frames; deeper frames count in depth and dropped
 * but are not recorded.
 */

/* gf_shadow_frame_kind: what a shadow frame records. */
typedef uint32_t gf_shadow_frame_kind;
/* A call of a function of these tables: table_id and slot_index name it. */
#define GF_SHADOW_FRAME_KIND_LIBRARY_CALL 1u
/* A window's dispatch of one input: subject is the window root's stable id,
 * name the input's kind (pointer, key, text or drag). */
#define GF_SHADOW_FRAME_KIND_INPUT_DISPATCH 2u
/* An event delivered to a callback: subject is the sender's stable id when
 * the sender is a control (else empty), name the event's name. */
#define GF_SHADOW_FRAME_KIND_EVENT_CALLBACK 3u
/* A window's layout: subject is the window root's stable id, sequence the
 * number of its first layout pass. */
#define GF_SHADOW_FRAME_KIND_LAYOUT_PASS 4u
/* A window's paint: subject is the window root's stable id, sequence the
 * number of the paint lease it starts. */
#define GF_SHADOW_FRAME_KIND_PAINT_PASS 5u
/* A callback posted with dispatch.begin_invoke: subject is the target's
 * stable id when the target is a control (else empty). */
#define GF_SHADOW_FRAME_KIND_POSTED_CALLBACK 6u

/* gf_shadow_frame_flags: set on a frame whose text was cut to its field, at
 * the last whole UTF-8 sequence that fits. */
typedef uint32_t gf_shadow_frame_flags;
#define GF_SHADOW_FRAME_SUBJECT_TRUNCATED 1u
#define GF_SHADOW_FRAME_NAME_TRUNCATED 2u

/* The bytes a frame keeps of its subject and of its name. */
#define GF_SHADOW_FRAME_SUBJECT_CAPACITY 48u
#define GF_SHADOW_FRAME_NAME_CAPACITY 32u
/* The frames a thread records. */
#define GF_SHADOW_STACK_CAPACITY 64u

/*
 * One shadow frame, an element of a caller's array (a fixed 112-byte
 * stride; it grows only with a new major). Fields its kind does not use are
 * zero. subject and name hold subject_size and name_size bytes of UTF-8,
 * not terminated; the bytes after them are zero.
 */
typedef struct gf_shadow_frame {
    gf_shadow_frame_kind kind;
    gf_shadow_frame_flags flags;
    gf_table_id table_id;
    uint32_t slot_index;
    uint64_t sequence;
    uint32_t subject_size;
    uint32_t name_size;
    /* GF_SHADOW_FRAME_SUBJECT_CAPACITY bytes. */
    uint8_t subject[48];
    /* GF_SHADOW_FRAME_NAME_CAPACITY bytes. */
    uint8_t name[32];
} gf_shadow_frame;

/*
 * The calling thread's shadow stack beside its frames. An output record, as
 * gf_abi_version_info. depth counts the frames open now, recorded or not;
 * pushed and dropped count every frame opened, and every one opened past
 * the capacity, since the thread began.
 */
typedef struct gf_shadow_stack_info {
    uint32_t struct_size;
    uint32_t depth;
    uint64_t pushed;
    uint64_t dropped;
} gf_shadow_stack_info;

typedef struct gf_diagnostics_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /*
     * Copies the calling thread's recorded frames, outermost first, into
     * frames and its state into info. required receives the number of
     * recorded frames (at most GF_SHADOW_STACK_CAPACITY). When capacity is
     * smaller, nothing else is written and the result is
     * GF_STATUS_BUFFER_TOO_SMALL. frames may be null when capacity is zero.
     * The frames are copies; nothing is lent.
     */
    gf_status (GF_CALL* shadow_frames)(gf_shadow_frame* frames, uint64_t capacity, uint64_t* required,
                                       gf_shadow_stack_info* info);
} gf_diagnostics_table;

/*
 * A control's layout transaction (Control::layout_transaction_state): how
 * deeply layout is suspended, whether a layout waits for the resume, and the
 * revisions requested and committed. An output record, as
 * gf_abi_version_info.
 */
typedef struct gf_layout_transaction {
    uint32_t struct_size;
    uint32_t suspend_depth;
    uint32_t deferred;
    uint32_t reserved;
    uint64_t requested_revision;
    uint64_t committed_revision;
} gf_layout_transaction;

/* gf_scroll_orientation: a scroll axis. */
typedef uint32_t gf_scroll_orientation;
#define GF_SCROLL_ORIENTATION_HORIZONTAL 0u
#define GF_SCROLL_ORIENTATION_VERTICAL 1u

/*
 * One scroll axis (ScrollProperties): enabled and visible (0 or 1), its
 * range, the changes a page and a line make, and its value. Minimum and the
 * changes are finite and not negative; value lies in the range.
 */
typedef struct gf_scroll_axis {
    uint32_t enabled;
    uint32_t visible;
    double minimum;
    double maximum;
    double large_change;
    double small_change;
    double value;
} gf_scroll_axis;

/*
 * A scrollable control's scroll state (ScrollableControl::scroll_snapshot),
 * after layout, and its last scroll: event_revision counts scrolls,
 * event_type and event_orientation are the scroll interaction's type and
 * orientation choices (zero before the first), with its old and new values.
 * An output record, as gf_abi_version_info.
 */
typedef struct gf_scroll_snapshot {
    uint32_t struct_size;
    uint32_t auto_scroll;
    gf_logical_point position;
    gf_logical_size margin;
    gf_logical_size minimum_content_size;
    gf_logical_rect display_rectangle;
    gf_logical_rect viewport_rectangle;
    gf_scroll_axis horizontal;
    gf_scroll_axis vertical;
    uint64_t event_revision;
    uint32_t event_type;
    gf_scroll_orientation event_orientation;
    double event_old_value;
    double event_new_value;
} gf_scroll_snapshot;

/*
 * GF_TABLE_CONTROLS: operations on a control that are not state: layout
 * transactions, window-relative bounds, pointer capture and scrolling. The
 * state the 0.x table set (a range's bounds, the cursor, the auto-scroll
 * settings, ...) is schema properties (GF_TABLE_PROPERTIES). UI thread
 * only. A scrolling function on a control that does not scroll gives
 * GF_STATUS_WRONG_HANDLE_KIND.
 */
typedef struct gf_controls_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Defers layout until the matching resume_layout; transactions nest. */
    gf_status (GF_CALL* suspend_layout)(gf_object object);
    /* Ends one suspension; perform_layout (0 or 1) lays out a deferred change
     * when the outermost one ends. Without a suspension it does nothing, as
     * Windows Forms tolerates an unmatched ResumeLayout. */
    gf_status (GF_CALL* resume_layout)(gf_object object, uint32_t perform_layout);
    /* Lays the control's children out now. */
    gf_status (GF_CALL* perform_layout)(gf_object object);
    gf_status (GF_CALL* layout_state)(gf_object object, gf_layout_transaction* state);
    /* The control's bounds in its window's client space, after layout. */
    gf_status (GF_CALL* window_bounds)(gf_object object, gf_logical_rect* bounds);
    /* Takes (1) or releases (0) the pointer for the control. */
    gf_status (GF_CALL* set_pointer_capture)(gf_object object, uint32_t captured);
    gf_status (GF_CALL* pointer_capture)(gf_object object, uint32_t* captured);
    gf_status (GF_CALL* scroll_state)(gf_object object, gf_scroll_snapshot* state);
    /* Sets one axis whole; an invalid axis gives INVALID_ARGUMENT. */
    gf_status (GF_CALL* set_scroll_axis)(gf_object object, gf_scroll_orientation orientation,
                                         const gf_scroll_axis* axis);
    /* Scrolls object, after layout, so that child, its descendant, shows. */
    gf_status (GF_CALL* scroll_into_view)(gf_object object, gf_object child);
    /* Asks for a repaint of damage, in the control's logical units, or of
     * the whole control when damage is null; a custom control's paint
     * callback then runs (GF_TABLE_CUSTOM_CONTROLS). A damage rectangle
     * that is not finite, or has a negative size, gives INVALID_ARGUMENT. */
    gf_status (GF_CALL* invalidate)(gf_object object, const gf_logical_rect* damage);
    /* Gives the control keyboard focus, as Control.Focus and Select do
     * (since 1.1): it becomes its window's focused control, the one its
     * window gives the keyboard to while active. A composite field (a
     * combo box with an edit field, a numeric or domain up-down, a masked
     * text box, a search field, a token box) gives the caret to its edit
     * part and still counts as the focused control. focused receives 1 when
     * the window is active, so the control has the keyboard now, and 0
     * when it is not (focus is taken when the window is next activated);
     * it may be null. GF_STATUS_INVALID_STATE, changing nothing, for a
     * control not in a window, disabled or hidden (or under a disabled or
     * hidden ancestor), not focusable (its focusable property, or its edit
     * part's for a composite), outside the
     * window's open focus scope (a dialog or overlay), or whose window's
     * validation kept focus where it was. UI thread. */
    gf_status (GF_CALL* focus)(gf_object object, uint32_t* focused);
    /* Whether the control is its window's focused control (Focused; a
     * composite field is while its edit part has the caret), and whether it
     * or a descendant holds focus (ContainsFocus), as 0 or 1 (since 1.1).
     * A control not in a window has neither. Either output may be null. UI
     * thread. */
    gf_status (GF_CALL* focus_state)(gf_object object, uint32_t* focused, uint32_t* contains_focus);
} gf_controls_table;

/* gf_text_ink_role: a theme ink for decorated text (C++ TextInkRole): the
 * editor's content or secondary ink, or the link, visited link or notice
 * family's text colour. */
typedef uint32_t gf_text_ink_role;
#define GF_TEXT_INK_ROLE_TEXT 0u
#define GF_TEXT_INK_ROLE_MUTED 1u
#define GF_TEXT_INK_ROLE_LINK 2u
#define GF_TEXT_INK_ROLE_VISITED_LINK 3u
#define GF_TEXT_INK_ROLE_NOTICE 4u

/* gf_text_underline: how decorated text is underlined (C++ TextUnderline). */
typedef uint32_t gf_text_underline;
#define GF_TEXT_UNDERLINE_NONE 0u
#define GF_TEXT_UNDERLINE_SINGLE 1u
#define GF_TEXT_UNDERLINE_SQUIGGLE 2u

/* gf_text_decoration_fields: which fields of a gf_text_decoration it sets;
 * a field without its bit leaves that aspect to the layers under it. */
typedef uint32_t gf_text_decoration_fields;
#define GF_TEXT_DECORATION_INK 0x1u
#define GF_TEXT_DECORATION_FOREGROUND 0x2u
#define GF_TEXT_DECORATION_WEIGHT 0x4u
#define GF_TEXT_DECORATION_ITALIC 0x8u
#define GF_TEXT_DECORATION_UNDERLINE 0x10u
#define GF_TEXT_DECORATION_UNDERLINE_COLOR 0x20u
#define GF_TEXT_DECORATION_BACKGROUND 0x40u

/*
 * One styled text run (since 1.1; C++ TextDecoration): UTF-8 byte offsets
 * [start, end) into the text and the look it gives that text. Colours are
 * 0xRRGGBBAA; weight is 1 to 1000; italic is 0 or 1. tag belongs to the
 * caller and comes back unchanged. reserved is 0.
 */
typedef struct gf_text_decoration {
    uint64_t start;
    uint64_t end;
    gf_text_decoration_fields fields;
    gf_text_ink_role ink;
    uint32_t foreground;
    uint32_t weight;
    uint32_t italic;
    gf_text_underline underline;
    uint32_t underline_color;
    uint32_t background;
    uint32_t tag;
    uint32_t reserved;
} gf_text_decoration;

/*
 * GF_TABLE_TEXT_EDITING: a text box's selection, grapheme navigation, edits
 * and history (TextBox), and its styled text runs (since 1.1). Positions are UTF-8 byte offsets
 * into the text property; a position inside a grapheme cluster or past the
 * text gives GF_STATUS_INVALID_ARGUMENT. Another control gives
 * GF_STATUS_WRONG_HANDLE_KIND. UI thread only.
 */
typedef struct gf_text_editing_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Selects from anchor to caret; the caret is where typing goes. */
    gf_status (GF_CALL* select)(gf_object object, uint64_t anchor, uint64_t caret);
    gf_status (GF_CALL* selection)(gf_object object, uint64_t* anchor, uint64_t* caret);
    /* The grapheme boundary after (direction 1) or before (-1) position;
     * position itself at either end of the text. */
    gf_status (GF_CALL* grapheme_boundary)(gf_object object, uint64_t position, int32_t direction,
                                           uint64_t* boundary);
    /* Replaces length bytes at start with replacement as one history step,
     * as typing does (filters apply); changed receives 1 when the text
     * changed, 0 when the box refused it (read-only, a filter, the limit). */
    gf_status (GF_CALL* replace_range)(gf_object object, uint64_t start, uint64_t length,
                                       gf_utf8_view replacement, uint32_t* changed);
    /* Undoes or redoes one history step; changed 0 when there is none. */
    gf_status (GF_CALL* undo)(gf_object object, uint32_t* changed);
    gf_status (GF_CALL* redo)(gf_object object, uint32_t* changed);
    /* Starts the history afresh without touching the text. */
    gf_status (GF_CALL* clear_undo_history)(gf_object object);
    /* Styled text runs (since 1.1; TextBox::set_decorations): decoration
     * layers over the text, composed in ascending layer order, which follow
     * every edit and never change the text. A multiline box draws them. A
     * range past the text, a range ending before it starts, a weight
     * outside 1 to 1000, an underline or ink out of range, an unknown field
     * bit or a nonzero reserved gives GF_STATUS_INVALID_ARGUMENT and changes
     * nothing. decorations may be null when count is 0.
     * Replaces layer's decorations; count 0 clears the layer. */
    gf_status (GF_CALL* set_decorations)(gf_object object, uint32_t layer,
                                         const gf_text_decoration* decorations, uint64_t count);
    /* Replaces layer's decorations within [start, end) only, as an
     * incremental highlighter does after an edit: decorations inside go,
     * those crossing the bounds keep their parts outside, and the given
     * ones (each inside the range) come in. Repaints only that range's
     * rows unless a weight or italics changed. */
    gf_status (GF_CALL* replace_decorations)(gf_object object, uint32_t layer, uint64_t start, uint64_t end,
                                             const gf_text_decoration* decorations, uint64_t count);
    /* Clears every layer. */
    gf_status (GF_CALL* clear_decorations)(gf_object object);
    /* The number of decorations in layer, in start order (0 for a layer
     * without any). */
    gf_status (GF_CALL* decoration_count)(gf_object object, uint32_t layer, uint64_t* count);
    /* Decoration index (below decoration_count) of layer, as it stands
     * after the edits since it was set. GF_STATUS_INVALID_ARGUMENT past the
     * end. */
    gf_status (GF_CALL* decoration_at)(gf_object object, uint32_t layer, uint64_t index,
                                       gf_text_decoration* decoration);
} gf_text_editing_table;

/*
 * GF_TABLE_OVERLAYS: overlay roots and tooltips. An overlay is a root control,
 * without a parent, shown over its owner's window until close_overlay, until
 * its owner leaves the window or until the window closes. Tooltips belong to
 * a tool_tip component (create_with). UI thread only.
 *
 * open_anchored (since 1.1) shows a content control beside an anchor, as a
 * menu, drop-down or context menu shows: an anchored overlay in the
 * anchor's window, placed below or above the anchor and kept inside the
 * window, with click-away and Escape dismissal. Menus, drop-downs and
 * context menus are overlays, not native windows.
 */

/* gf_anchored_overlay_flags: how open_anchored shows its content. */
typedef uint32_t gf_anchored_overlay_flags;
#define GF_ANCHORED_OVERLAY_FLAGS_NONE 0u
/* Prefer the space above the anchor (else below). */
#define GF_ANCHORED_OVERLAY_ABOVE 1u
/* Align the content's right edge with the anchor's (else the left edges). */
#define GF_ANCHORED_OVERLAY_ALIGN_FAR 2u
/* Never flip to the other side when the preferred one is too small. */
#define GF_ANCHORED_OVERLAY_NO_FLIP 4u
/* A click outside the content does not dismiss it. */
#define GF_ANCHORED_OVERLAY_KEEP_ON_CLICK_AWAY 8u
/* Escape does not dismiss it. */
#define GF_ANCHORED_OVERLAY_KEEP_ON_ESCAPE 16u
/* The content takes the keyboard focus, contained in it while open and
 * given back to where it was when it closes (a menu, a drop-down list).
 * Without it the focus stays where it is (a passive pop-up). */
#define GF_ANCHORED_OVERLAY_TAKE_FOCUS 32u

/*
 * How open_anchored shows content (since 1.1). width and height are the
 * content's preferred size in logical units (zero: its measured size); gap
 * is the distance from the anchor. events, which may be null, receives the
 * overlay's events with sender the content: "click_away" and "escape_key"
 * when the user asks to dismiss it (setting the event's cancel keeps it
 * open, for staged dismissal; otherwise it closes), then "closed" once it
 * has closed for any reason (close_anchored, dismissal, the anchor leaving
 * the window, the window closing), after which the callback is released.
 */
typedef struct gf_anchored_overlay_options {
    uint32_t struct_size;
    gf_anchored_overlay_flags flags;
    double width;
    double height;
    double gap;
    const gf_callback* events;
} gf_anchored_overlay_options;

typedef struct gf_overlays_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Shows overlay over owner's window. owner must be in a running window
     * (else GF_STATUS_INVALID_STATE); an overlay already open gives
     * GF_STATUS_INVALID_STATE. */
    gf_status (GF_CALL* open_overlay)(gf_object owner, gf_object overlay);
    /* Closes overlay; one not open gives GF_STATUS_NOT_FOUND. */
    gf_status (GF_CALL* close_overlay)(gf_object overlay);
    /* The text tool_tip shows for target; empty text removes it. */
    gf_status (GF_CALL* set_tool_tip)(gf_object tool_tip, gf_object target, gf_utf8_view text);
    /* Shows target's text now, for duration_ms milliseconds (0: until the
     * pointer leaves or hide_tool_tip). */
    gf_status (GF_CALL* show_tool_tip)(gf_object tool_tip, gf_object target, uint64_t duration_ms);
    gf_status (GF_CALL* hide_tool_tip)(gf_object tool_tip);
    /* Shows content, a control with no parent and not in a window, beside
     * anchor, a control in a running window (since 1.1). The library holds
     * content while it is open. GF_STATUS_INVALID_STATE when anchor is not
     * in a window or is disabled, or content is already open or has a
     * parent; GF_STATUS_INVALID_ARGUMENT for options smaller than their 1.1
     * size, unknown flags or a size or gap that is negative or not finite.
     * Nothing is shown on a refusal. */
    gf_status (GF_CALL* open_anchored)(gf_object anchor, gf_object content,
                                       const gf_anchored_overlay_options* options);
    /* Closes content's anchored overlay: its "closed" event follows before
     * this returns. GF_STATUS_NOT_FOUND when it is not open (since 1.1). */
    gf_status (GF_CALL* close_anchored)(gf_object content);
} gf_overlays_table;

/*
 * GF_TABLE_PROPERTY_GRID: a property grid's selection and its editing
 * operations (PropertyGrid). Its sort is the property_sort property. A name
 * is a property of the selected objects. Another control gives
 * GF_STATUS_WRONG_HANDLE_KIND. UI thread only.
 */
typedef struct gf_property_grid_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Shows the properties the count controls share; count 0 clears. The
     * grid keeps them as it keeps a child, without a reference. */
    gf_status (GF_CALL* select_objects)(gf_object grid, const gf_object* objects, uint64_t count);
    /* Reads the selected objects' properties afresh. */
    gf_status (GF_CALL* refresh)(gf_object grid);
    /* Parses text into the named property as its editor would; accepted
     * receives 0 when the text does not convert or the property refuses it. */
    gf_status (GF_CALL* try_set_text)(gf_object grid, gf_utf8_view name, gf_utf8_view text, uint32_t* accepted);
    /* Resets the named property; reset receives 0 when it cannot reset. */
    gf_status (GF_CALL* reset_property)(gf_object grid, gf_utf8_view name, uint32_t* reset);
    /* Opens the named property's editor; activated 0 when it has none. */
    gf_status (GF_CALL* activate_editor)(gf_object grid, gf_utf8_view name, uint32_t* activated);
} gf_property_grid_table;

/*
 * GF_TABLE_CUSTOM_CONTROLS: a control type the caller defines, the C form of
 * a C++ subclass. As a C++ custom control subclasses a stock control,
 * declares its properties (define_bindable_property) and overrides on_paint,
 * a custom control takes a stock base, declares properties the caller's
 * callbacks get and set, and paints through a callback into pixels the
 * library presents. Its schema type (objects.type_id) is its base's, as a
 * C++ subclass reports its nearest schema type; its properties are the
 * base's and its own, through GF_TABLE_PROPERTIES and the property grid like
 * any other. UI thread only.
 */

/* gf_custom_base: the stock control a custom control extends. */
typedef uint32_t gf_custom_base;
#define GF_CUSTOM_BASE_SCROLLABLE_CONTROL 0u
#define GF_CUSTOM_BASE_USER_CONTROL 1u

/* gf_custom_control_flags. */
typedef uint32_t gf_custom_control_flags;
/* The control takes keyboard focus. */
#define GF_CUSTOM_CONTROL_FOCUSABLE 1u

/* gf_custom_property_flags. */
typedef uint32_t gf_custom_property_flags;
/* No set_property: the property is read only. */
#define GF_CUSTOM_PROPERTY_READ_ONLY 1u
/* The enumeration's choices are combinable flags. */
#define GF_CUSTOM_PROPERTY_COMBINABLE 2u
/* The caller reports changes with notify_changed, so the property's change
 * can be subscribed to and bound. */
#define GF_CUSTOM_PROPERTY_CHANGE_NOTIFICATIONS 4u

#define GF_CUSTOM_MAXIMUM_PROPERTIES 256u

/*
 * One property a custom control declares. name is a registered name in any
 * spelling (snake_case, camelCase or PascalCase; GF_TABLE_SCHEMA), distinct
 * from its siblings' and from its base's properties. kind is a gf_value_kind
 * other than NULL, OBJECT and COLLECTION. default_value, which may be null,
 * makes a writable property resettable to it. An ENUMERATION names its type
 * in enumeration and lists 1 to GF_CREATE_MAXIMUM_CHOICES choices, as
 * create_with does; other kinds leave both empty. The library copies the
 * record during create; nothing in it need outlive the call.
 */
typedef struct gf_custom_property {
    gf_utf8_view name;
    gf_utf8_view category;
    gf_utf8_view description;
    gf_value_kind kind;
    gf_custom_property_flags flags;
    const gf_value* default_value;
    gf_utf8_view enumeration;
    const gf_create_choice* choices;
    uint64_t choice_count;
} gf_custom_property;

/*
 * The pixels a paint callback fills, lent for the call: width by height
 * premultiplied BGRA (byte order blue, green, red, alpha), row_bytes apart,
 * covering the control's client area at scale device pixels per logical
 * unit. damage, in the control's logical units, is what must be painted;
 * every pixel outside it already holds what the previous paint left. After
 * the control's size or scale changes, damage is the whole control and the
 * pixels' contents are unspecified.
 */
typedef struct gf_paint_target {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
    uint64_t row_bytes;
    uint8_t* pixels;
    double scale;
    gf_logical_rect damage;
} gf_paint_target;

/*
 * The callbacks of a custom control, called on the UI thread with context
 * and the control's identity (zero while the caller holds no reference to
 * it). paint draws over the base's own painting (its background). index is
 * the property's position in the spec's properties. get_property fills
 * value, whose struct_size the library sets, with a value of the property's
 * kind; text it lends stays valid until the library's next callback on the
 * control. set_property receives a value of the property's kind, already
 * converted and checked against its enumeration; it returns GF_STATUS_OK to
 * accept it, any other status to refuse it (the setting call then gives
 * GF_STATUS_INVALID_ARGUMENT). A get_property that fails, or gives another
 * kind, makes the reading call give GF_STATUS_INVALID_STATE. paint and
 * get_property must not change controls. release, which may be null, is
 * called once, when the control is disposed or destroyed; no callback runs
 * after it. paint and get_property are required, set_property is required
 * unless every property is read only; reserved is zero.
 */
typedef struct gf_custom_control_callbacks {
    uint32_t struct_size;
    uint32_t reserved;
    void* context;
    void (GF_CALL* paint)(void* context, gf_object control, const gf_paint_target* target);
    gf_status (GF_CALL* get_property)(void* context, gf_object control, uint64_t index, gf_value* value);
    gf_status (GF_CALL* set_property)(void* context, gf_object control, uint64_t index, const gf_value* value);
    void (GF_CALL* release)(void* context);
} gf_custom_control_callbacks;

/*
 * A custom control's definition. property_size is the size of one
 * gf_custom_property as the caller declares it (at least the 1.0 size); the
 * library steps through properties by it. properties may be null when
 * property_count is 0; at most GF_CUSTOM_MAXIMUM_PROPERTIES.
 */
typedef struct gf_custom_control_spec {
    uint32_t struct_size;
    gf_custom_base base;
    gf_custom_control_flags flags;
    uint32_t property_size;
    const gf_custom_property* properties;
    uint64_t property_count;
    gf_custom_control_callbacks callbacks;
} gf_custom_control_spec;

typedef struct gf_custom_controls_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Creates a custom control with stable_id (the objects table's rule:
     * lowercase '.'-separated, '-'-joined segments) and gives the
     * caller one reference to it. A malformed spec gives INVALID_ARGUMENT
     * and calls no callback, release among them. */
    gf_status (GF_CALL* create)(const gf_custom_control_spec* spec, gf_utf8_view stable_id, gf_object* object);
    /* Reports that property index changed, as a C++ control raises its
     * changed event: subscribers and bindings read it afresh. A property
     * declared without GF_CUSTOM_PROPERTY_CHANGE_NOTIFICATIONS gives
     * INVALID_ARGUMENT; another control gives WRONG_HANDLE_KIND. */
    gf_status (GF_CALL* notify_changed)(gf_object object, uint64_t index);
} gf_custom_controls_table;

/*
 * GF_TABLE_BINDINGS: data binding, as C++ binds: a binding_source (made
 * with create_with over a window) holds records of named fields and a
 * current position; a binding ties one property of a control to one field
 * of a source's current record (Control::data_bindings().add). A source is
 * filled and moved here; a binding's options are fixed when it is made.
 * Field names are registered names in any spelling. Another object where a
 * source or binding is expected gives GF_STATUS_WRONG_HANDLE_KIND. UI
 * thread only.
 */

/* gf_source_update_mode: when a control's change reaches the source. */
typedef uint32_t gf_source_update_mode;
#define GF_SOURCE_UPDATE_ON_VALIDATION 0u
#define GF_SOURCE_UPDATE_ON_PROPERTY_CHANGED 1u
#define GF_SOURCE_UPDATE_NEVER 2u

/* gf_control_update_mode: when a source's change reaches the control. */
typedef uint32_t gf_control_update_mode;
#define GF_CONTROL_UPDATE_ON_PROPERTY_CHANGED 0u
#define GF_CONTROL_UPDATE_NEVER 1u

/*
 * A binding's definition: target's property bound to source's field
 * data_member. formatting_enabled (0 or 1) applies format_string and the
 * format and parse events; null_value, which may be null, is what the
 * control shows for a null field, source_null_value what the source
 * receives for a null control value. The library copies what it needs
 * during bind.
 */
typedef struct gf_binding_arguments {
    uint32_t struct_size;
    gf_source_update_mode source_update_mode;
    gf_control_update_mode control_update_mode;
    uint32_t formatting_enabled;
    gf_object target;
    gf_object source;
    gf_utf8_view property;
    gf_utf8_view data_member;
    gf_utf8_view format_string;
    const gf_value* null_value;
    const gf_value* source_null_value;
} gf_binding_arguments;

/* One field of a record: its name and value (a NULL value for none). */
typedef struct gf_binding_field {
    gf_utf8_view name;
    const gf_value* value;
} gf_binding_field;

/* A record: its stable id (may be empty), whether it may be edited (0 or
 * 1) and its fields, with distinct names. */
typedef struct gf_binding_record {
    uint32_t struct_size;
    uint32_t editable;
    gf_utf8_view stable_id;
    const gf_binding_field* fields;
    uint64_t field_count;
} gf_binding_record;

#define GF_BINDING_MAXIMUM_FIELDS 1024u

typedef struct gf_bindings_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Binds as arguments says and gives the caller one reference to the
     * binding. The control keeps the binding until unbind or its disposal.
     * A property that is not bindable gives GF_STATUS_INVALID_ARGUMENT. */
    gf_status (GF_CALL* bind)(const gf_binding_arguments* arguments, gf_object* binding);
    /* Removes the binding from its control, which disposes it, as
     * ControlBindingsCollection::remove does; a later call on it gives
     * GF_STATUS_DISPOSED. */
    gf_status (GF_CALL* unbind)(gf_object binding);
    /* Sets the control's property from the source (read_value) or the
     * source's field from the control (write_value), as the binding's modes
     * would; updated receives 0 when conversion or validation refused it. */
    gf_status (GF_CALL* read_value)(gf_object binding, uint32_t* updated);
    gf_status (GF_CALL* write_value)(gf_object binding, uint32_t* updated);
    /* Appends a record; index receives its position. */
    gf_status (GF_CALL* add_record)(gf_object source, const gf_binding_record* record, uint64_t* index);
    /* Inserts a record at index (at most the count). */
    gf_status (GF_CALL* insert_record)(gf_object source, uint64_t index, const gf_binding_record* record);
    /* Removes the record at index; removed 0 for an index past the
     * records. A source that allows no removal gives
     * GF_STATUS_INVALID_STATE. */
    gf_status (GF_CALL* remove_record)(gf_object source, uint64_t index, uint32_t* removed);
    gf_status (GF_CALL* clear_records)(gf_object source);
    /* Moves to the record at position (-1 for none); moved 0 when it is
     * already there. A position past the records gives
     * GF_STATUS_INVALID_ARGUMENT. */
    gf_status (GF_CALL* set_position)(gf_object source, int64_t position, uint32_t* moved);
    /* The current position (-1 for none) and the count of records. */
    gf_status (GF_CALL* position)(gf_object source, int64_t* position, uint64_t* count);
    /* The current record's field, as properties.get gives a value; no
     * current record or no such field gives GF_STATUS_NOT_FOUND. */
    gf_status (GF_CALL* current_field)(gf_object source, gf_utf8_view field, gf_value* value, char* text_buffer,
                                       uint64_t text_capacity, uint64_t* text_required);
    /* Sets the current record's field; accepted 0 when there is no current
     * record or it, or the source, allows no edit. */
    gf_status (GF_CALL* set_current_field)(gf_object source, gf_utf8_view field, const gf_value* value,
                                           uint32_t* accepted);
} gf_bindings_table;

/*
 * GF_TABLE_SUBSURFACES (since 1.1): threaded sub-surfaces. A
 * sub_surface_host control (objects.create "SubSurfaceHost") occupies a
 * region of its window; its content is a tree of controls built and run on
 * a thread of its own and composed into the region. set_content gives a
 * host its content's build callback. The host starts the thread the first
 * time it is in a window with a non-empty size, and stops it (joining the
 * thread) when it leaves its window, when it is given other content, and
 * when set_content gives it none.
 *
 * Threads. The sub-surface's thread calls the build callback once. Objects
 * created on that thread (in the build callback, or in callbacks raised by
 * the sub-surface's controls) belong to the sub-surface: only its thread
 * may use them, and the UI thread gets GF_STATUS_WRONG_THREAD; the UI
 * thread's objects are refused on the sub-surface's thread the same way.
 * Any thread may retain, release and begin_invoke them: a posted callback
 * runs on the sub-surface's thread, and a last release made elsewhere is
 * completed there. Callbacks registered on the sub-surface's objects run on
 * its thread, and so do their releases. On that thread the objects,
 * properties, events, dispatch, schema, controls, text editing and
 * subsurfaces tables may be called; every other function gives
 * GF_STATUS_WRONG_THREAD. When the sub-surface stops, its thread destroys
 * its objects: their identities stay until released and give
 * GF_STATUS_STALE_HANDLE, and callbacks still posted to them are released
 * without being invoked.
 */

/* gf_subsurface_state: where a host's sub-surface is. */
typedef uint32_t gf_subsurface_state;
/* No thread: never started, or stopped after its host left its window or
 * lost its content. */
#define GF_SUBSURFACE_STATE_STOPPED 0u
/* The thread is running the build callback. */
#define GF_SUBSURFACE_STATE_STARTING 1u
/* The tree is built and the sub-surface runs. */
#define GF_SUBSURFACE_STATE_RUNNING 2u
/* The build handed over no root; the thread ended (subsurfaces.failure). */
#define GF_SUBSURFACE_STATE_FAILED 3u

/*
 * A sub-surface's content. The caller sets struct_size to
 * sizeof(gf_subsurface_content). build is invoked once, on the
 * sub-surface's thread, with sender zero and the name "build"; it creates
 * the content's root control there and hands it over with
 * subsurfaces.set_root before it returns. Its release runs on the
 * sub-surface's thread after that invoke, or on the UI thread when the
 * content never started (replaced first, or its host destroyed first).
 * opaque is 1 when the content covers its whole region with opaque pixels
 * (the window then copies its frames instead of blending them), else 0.
 */
typedef struct gf_subsurface_content {
    uint32_t struct_size;
    uint32_t opaque;
    gf_callback build;
} gf_subsurface_content;

typedef struct gf_subsurfaces_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Gives host content to show; a null content stops the sub-surface and
     * leaves the region empty. A running sub-surface is stopped and its
     * thread joined first. The library takes content->build only when this
     * returns GF_STATUS_OK. A host that is not a sub_surface_host gives
     * GF_STATUS_WRONG_HANDLE_KIND. UI thread. */
    gf_status (GF_CALL* set_content)(gf_object host, const gf_subsurface_content* content);
    /* Hands root, a control created on the calling thread, to the
     * sub-surface whose build callback is running there; the sub-surface
     * holds it from then on, and the caller keeps its own reference. Only
     * inside a build callback, elsewhere GF_STATUS_INVALID_STATE; a second
     * call replaces the first. */
    gf_status (GF_CALL* set_root)(gf_object root);
    /* The host's state; GF_SUBSURFACE_STATE_STOPPED while it has no
     * thread. UI thread. */
    gf_status (GF_CALL* state)(gf_object host, gf_subsurface_state* state);
    /* The content epoch of the newest configuration the host sent its
     * sub-surface (each frame the sub-surface shows was produced at one);
     * zero before the first. UI thread. */
    gf_status (GF_CALL* content_epoch)(gf_object host, uint64_t* epoch);
    /* Why the sub-surface failed (GF_SUBSURFACE_STATE_FAILED), as text;
     * empty in every other state. UI thread. */
    gf_status (GF_CALL* failure)(gf_object host, char* buffer, uint64_t capacity, uint64_t* required);
} gf_subsurfaces_table;

/*
 * GF_TABLE_APPLICATION (since 1.1): the application the library's windows
 * share (C++ Application::create; APP_WINDOW_RULES_DESIGN Part A). A
 * program may make one at a time, before the library's first object: create
 * compiles its theme, the base sheet and then the packages in order, once,
 * and every window takes that theme and the presentation defaults. A
 * program that never calls create gets the process default, as before:
 * the built-in theme and GUI.Forms' presentation defaults. GF_TABLE_THEMES
 * still holds the one theme: a later load or reset replaces the
 * application's.
 *
 * create runs on the calling thread, which becomes the UI thread (as the
 * first create of an object does); close and state run on the UI thread.
 */

/* gf_theme_base: the sheet an application's theme starts from, before its
 * packages. */
typedef uint32_t gf_theme_base;
/* The process default's base, whichever named base the library makes the
 * default (GUI_FORMS in 1.1): zero, so a zero-initialised config takes it. */
#define GF_THEME_BASE_DEFAULT 0u
/* The Windows Professional sheet, a Windows-familiar theme's. */
#define GF_THEME_BASE_WINDOWS_PROFESSIONAL 1u
/* The library's own GUI.Forms sheet. */
#define GF_THEME_BASE_GUI_FORMS 2u

/* gf_presentation_source: how an application's windows take their
 * presentation (text scale, high contrast, reduced motion and transparency,
 * sound, ambient motion, double-click interval). With either source a
 * window follows the presentation its host reports once the host reports
 * it; the sources differ in what a window starts from. */
typedef uint32_t gf_presentation_source;
/* Start from GUI.Forms' defaults and follow the host's reports. A later
 * minor may start from the host's settings as read when the application is
 * made, where the host can report them before a window exists. */
#define GF_PRESENTATION_SOURCE_HOST 0u
/* Start from explicit defaults, GUI.Forms' presentation defaults as this
 * library defines them, and follow the host's reports. */
#define GF_PRESENTATION_SOURCE_DEFAULTS 1u

/* The most theme packages one application compiles; more are refused by
 * the compile (finding E601), as a repeated package name is. */
#define GF_APPLICATION_MAXIMUM_THEME_PACKAGES 16u

/*
 * What an application is made with. An input record: struct_size at least
 * the 1.1 size, reserved zero. packages lists package_count packages
 * (packages may be null when the count is zero), compiled after the base
 * in that order, each with its
 * @import closure among the installed packages (themes.install). The
 * packages and their bytes are read during create only. With the default
 * base (DEFAULT, or the named base it resolves to) and no packages the
 * theme is the built-in one, not compiled again.
 */
typedef struct gf_application_config {
    uint32_t struct_size;
    gf_theme_base theme_base;
    const gf_theme_package* packages;
    uint64_t package_count;
    gf_presentation_source presentation_source;
    uint32_t reserved;
} gf_application_config;

/* gf_application_state: whether the library has an application. */
typedef uint32_t gf_application_state;
/* No application: create has not been called, or the application was
 * closed. Windows use the process default. */
#define GF_APPLICATION_STATE_NONE 0u
/* The application is current. */
#define GF_APPLICATION_STATE_CURRENT 1u

typedef struct gf_application_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Makes the application from config and makes it current. report (an
     * output record) receives the theme compile's outcome: APPLIED, with
     * any warnings among the findings, or REFUSED, with nothing made; the
     * findings replace the held ones either way (themes.diagnostic_at,
     * diagnostic_text). GF_STATUS_INVALID_STATE, making nothing and
     * leaving the findings, while an application is current or while the
     * library holds objects made without one; GF_STATUS_INVALID_ARGUMENT
     * for an unknown base or source, a non-zero reserved field, or
     * packages null with a non-zero count. More packages than the maximum
     * or a repeated package name is a refused compile (E601), not a failed
     * call. */
    gf_status (GF_CALL* create)(const gf_application_config* config, gf_theme_report* report);
    /* Ends the application's time as current. windows_alive receives the
     * number of its windows still alive. GF_STATUS_INVALID_STATE, changing
     * nothing, while any is; once closed, the theme is the built-in one
     * again (as themes.reset), windows made afterwards use the process
     * default and create may make another application. Closing
     * with no application accepts and does nothing. */
    gf_status (GF_CALL* close)(uint64_t* windows_alive);
    /* Whether an application is current. */
    gf_status (GF_CALL* state)(gf_application_state* state);
} gf_application_table;

/*
 * GF_TABLE_RULES (since 1.1): a window's rules (notes:planning
 * APP_WINDOW_RULES_DESIGN Part C), as C++ RuleSet keeps them. A rule has one
 * trigger, zero or more conditions that must all hold, and one or more
 * actions run in order. The vocabulary is closed: the trigger, condition
 * and action kinds below are the whole of it.
 *
 * Evaluation. For one occurrence of a trigger a window's rules on it are
 * evaluated in the order they were added; each whose conditions all hold
 * fires. A firing rule with GF_RULE_FLAGS_CONSUME stops the rules added
 * after it and the occurrence's default route: a key then reaches no
 * control and no key command (key rules see a key before the focused
 * control), and a context-menu request reaches none of the control's
 * ancestors. Control-event rules run when the control raises the event.
 * A rule added while an occurrence is evaluated is first evaluated by the
 * next one.
 *
 * Lifetime. A window's rules end with it. A rule naming an object (its
 * trigger's source, a condition's object, an action's target) is disarmed
 * when that object is disposed or destroyed; its token then reads
 * GF_RULE_STATE_STALE. Each token add gives is freed by remove, which also
 * ends the rule when it is still armed; a freed token gives
 * GF_STATUS_STALE_HANDLE. An action's callback is released once the library
 * will not call it again: after its rule ends, at the latest when its
 * window is destroyed, on the UI thread. Every function is UI thread only.
 */

/* gf_rule_trigger_kind: what a rule fires on. */
typedef uint32_t gf_rule_trigger_kind;
/* A key's press (not the platform's auto-repeat), its release, its
 * auto-repeat (the platform's own); physical_key names it. */
#define GF_RULE_TRIGGER_KEY_PRESSED 1u
#define GF_RULE_TRIGGER_KEY_RELEASED 2u
#define GF_RULE_TRIGGER_KEY_REPEAT 3u
/* The WinForms mouse events of the control source. */
#define GF_RULE_TRIGGER_MOUSE_DOWN 4u
#define GF_RULE_TRIGGER_MOUSE_UP 5u
#define GF_RULE_TRIGGER_CLICK 6u
#define GF_RULE_TRIGGER_MOUSE_CLICK 7u
#define GF_RULE_TRIGGER_DOUBLE_CLICK 8u
#define GF_RULE_TRIGGER_MOUSE_DOUBLE_CLICK 9u
#define GF_RULE_TRIGGER_CONTEXT_MENU_REQUESTED 10u
#define GF_RULE_TRIGGER_MOUSE_ENTER 11u
#define GF_RULE_TRIGGER_MOUSE_HOVER 12u
#define GF_RULE_TRIGGER_MOUSE_LEAVE 13u
#define GF_RULE_TRIGGER_MOUSE_CAPTURE_CHANGED 14u
/* The control source gained or lost the keyboard focus. */
#define GF_RULE_TRIGGER_FOCUS_GAINED 15u
#define GF_RULE_TRIGGER_FOCUS_LOST 16u
/* Any interaction source's type raises, by its schema name
 * (schema.interaction_at), in interaction. */
#define GF_RULE_TRIGGER_INTERACTION 17u
/* The command source was executed. */
#define GF_RULE_TRIGGER_COMMAND_EXECUTED 18u

/* gf_rule_condition_kind: a level read when the trigger occurs. */
typedef uint32_t gf_rule_condition_kind;
/* The key physical_key is held. */
#define GF_RULE_CONDITION_KEY_DOWN 1u
/* The held modifiers are exactly modifiers (GF_RULE_MODIFIER_*). */
#define GF_RULE_CONDITION_MODIFIERS 2u
/* The pointer is over the control object or one of its descendants. */
#define GF_RULE_CONDITION_HOVERING 3u
/* The control object is the window's pressed control. */
#define GF_RULE_CONDITION_PRESSED 4u
/* The control object has the keyboard focus. */
#define GF_RULE_CONDITION_FOCUSED 5u
/* The control object or one of its descendants has the keyboard focus. */
#define GF_RULE_CONDITION_FOCUS_WITHIN 6u
/* The control object is effectively enabled; effectively visible. */
#define GF_RULE_CONDITION_ENABLED 7u
#define GF_RULE_CONDITION_VISIBLE 8u
/* The window is the active window. */
#define GF_RULE_CONDITION_WINDOW_ACTIVE 9u

/* The modifiers GF_RULE_CONDITION_MODIFIERS names, combined as flags. */
#define GF_RULE_MODIFIER_SHIFT 1u
#define GF_RULE_MODIFIER_CONTROL 2u
#define GF_RULE_MODIFIER_ALT 4u
#define GF_RULE_MODIFIER_META 8u

/* gf_rule_action_kind: what a firing rule does. */
typedef uint32_t gf_rule_action_kind;
/* Calls callback with sender the trigger's source (zero for a key
 * trigger), the trigger kind's name ("key_pressed", "click", ...) as name,
 * and no payload. */
#define GF_RULE_ACTION_CALLBACK 1u
/* Executes the command target through the window's command route. */
#define GF_RULE_ACTION_INVOKE 2u
/* Writes value to target's property, as properties.set does. */
#define GF_RULE_ACTION_SET 3u
/* Writes the opposite of target's boolean property. */
#define GF_RULE_ACTION_TOGGLE 4u
/* Gives the control target the keyboard focus. */
#define GF_RULE_ACTION_FOCUS 5u

/* gf_rule_flags: how a rule fires. */
typedef uint32_t gf_rule_flags;
/* A firing ends the occurrence: later rules and the default route. */
#define GF_RULE_FLAGS_CONSUME 1u

/* gf_rule_state: where a rule is. */
typedef uint32_t gf_rule_state;
/* It fires. */
#define GF_RULE_STATE_ARMED 1u
/* Ended by remove (not reported: remove frees the token). */
#define GF_RULE_STATE_REVOKED 2u
/* An object it names was disposed or destroyed, or its window has gone. */
#define GF_RULE_STATE_STALE 3u

/* A rule's token: the same encoding as gf_object, in its own space. */
typedef uint64_t gf_rule_token;

/*
 * A condition. The caller sets struct_size to sizeof(gf_rule_condition);
 * object is the control the control conditions read, physical_key
 * KEY_DOWN's key (a USB HID usage), modifiers MODIFIERS' set. negated is 1
 * for a condition that holds when its level does not, else 0. Fields a kind
 * does not read are zero; reserved is zero.
 */
typedef struct gf_rule_condition {
    uint32_t struct_size;
    gf_rule_condition_kind kind;
    gf_object object;
    uint32_t physical_key;
    uint32_t modifiers;
    uint32_t negated;
    uint32_t reserved;
} gf_rule_condition;

/*
 * An action. The caller sets struct_size to sizeof(gf_rule_action). target
 * is INVOKE's command, SET's and TOGGLE's object and FOCUS's control;
 * property names SET's and TOGGLE's property; value is SET's value (any
 * kind but a collection; its text is copied); callback is CALLBACK's (the
 * library takes it only when add returns GF_STATUS_OK). Fields a kind does
 * not read are zero.
 */
typedef struct gf_rule_action {
    uint32_t struct_size;
    gf_rule_action_kind kind;
    gf_object target;
    gf_utf8_view property;
    const gf_value* value;
    const gf_callback* callback;
} gf_rule_action;

/*
 * A rule. The caller sets struct_size to sizeof(gf_rule). trigger is its
 * kind; physical_key names a key trigger's key, source the control of a
 * control trigger or an interaction trigger (any object, for INTERACTION)
 * or the command of COMMAND_EXECUTED, and interaction INTERACTION's schema
 * name. conditions holds condition_count conditions (at most 8) and actions
 * action_count actions (1 to 8); add copies them. Fields a kind does not
 * read are zero.
 */
typedef struct gf_rule {
    uint32_t struct_size;
    gf_rule_flags flags;
    gf_rule_trigger_kind trigger;
    uint32_t physical_key;
    gf_object source;
    gf_utf8_view interaction;
    uint64_t condition_count;
    const gf_rule_condition* conditions;
    uint64_t action_count;
    const gf_rule_action* actions;
} gf_rule;

typedef struct gf_rules_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Arms rule on window (from windows.create; created or running) and
     * writes its token. GF_STATUS_INVALID_ARGUMENT for a malformed record
     * (an unknown kind or flag, a missing key, object, property or
     * callback, a collection value, nonzero reserved fields),
     * GF_STATUS_WRONG_HANDLE_KIND when window is not a window or an object
     * is not of the kind its field names, GF_STATUS_DISPOSED for a disposed
     * window or object, GF_STATUS_NOT_FOUND for an interaction the
     * source's type does not raise or a property the target lacks,
     * GF_STATUS_INVALID_STATE when an armed rule has the same trigger and
     * set of conditions, GF_STATUS_LIMIT_EXCEEDED past 8 conditions, 8
     * actions, 1024 rules on the window or 4096 tokens not freed. A refused
     * add changes nothing and leaves *token as it was. */
    gf_status (GF_CALL* add)(gf_object window, const gf_rule* rule, gf_rule_token* token);
    /* Ends token's rule when it is armed and frees the token. */
    gf_status (GF_CALL* remove)(gf_rule_token token);
    /* token's rule: GF_RULE_STATE_ARMED or GF_RULE_STATE_STALE. */
    gf_status (GF_CALL* state)(gf_rule_token token, gf_rule_state* state);
} gf_rules_table;

/*
 * GF_TABLE_WINDOWS_PAINT_ENDPOINT (Windows only; host capability
 * GF_HOST_CAPABILITY_WINDOWS_PAINT_ENDPOINT): a virtual Windows paint target
 * for code that paints through GDI, as a Windows Forms owner-drawn control
 * does. An endpoint is bound to a custom control (GF_TABLE_CUSTOM_CONTROLS):
 * while it lives, the control shows the frames the endpoint publishes
 * instead of calling its paint callback. Its compatibility handle is a
 * virtual window handle the compatibility shim understands, never an HWND;
 * its device context is a memory DC over the endpoint's pixels.
 *
 * acquire, configure, drain run on the UI thread. touch, snapshot,
 * submit_bgra and the device-context functions may run on any thread; a
 * write lease ends on the thread that began it. release may run on any
 * thread (a finalizer's): on the UI thread it unbinds the control at once,
 * elsewhere the control keeps the endpoint's last frame until it is
 * disposed. An endpoint or lease no longer active, and a handle or device
 * context that is not an endpoint's, give GF_STATUS_STALE_HANDLE, so a shim
 * falls through to the system. A call on the wrong thread gives
 * GF_STATUS_WRONG_THREAD.
 */

/* An endpoint and a write lease: nonzero tokens, never reused. A
 * compatibility handle and a device context are Windows handle values
 * widened to 64 bits. */
typedef uint64_t gf_paint_endpoint;
typedef uint64_t gf_paint_write_lease;

#define GF_PAINT_ENDPOINT_MAXIMUM_SIDE 32768u

typedef struct gf_windows_paint_endpoint_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Makes an endpoint of width by height pixels (1 to
     * GF_PAINT_ENDPOINT_MAXIMUM_SIDE each, at most 2^28 pixels) bound to
     * control, a custom control (else GF_STATUS_WRONG_HANDLE_KIND), and
     * gives its token and compatibility handle. */
    gf_status (GF_CALL* acquire)(gf_object control, uint32_t width, uint32_t height, gf_paint_endpoint* endpoint,
                                 uint64_t* compatibility_handle);
    /* Resizes the pixels; the token and handle stay. */
    gf_status (GF_CALL* configure)(gf_paint_endpoint endpoint, uint32_t width, uint32_t height);
    /* Marks the pixels changed; explicit_boundary (0 or 1) ends a frame now
     * rather than at the endpoint's next quiet point. */
    gf_status (GF_CALL* touch)(gf_paint_endpoint endpoint, uint32_t explicit_boundary);
    /* Publishes what the endpoint holds now. */
    gf_status (GF_CALL* drain)(gf_paint_endpoint endpoint);
    /* The endpoint's state as text, for diagnostics; required receives its
     * size, a short buffer gives GF_STATUS_BUFFER_TOO_SMALL. */
    gf_status (GF_CALL* snapshot)(gf_paint_endpoint endpoint, char* buffer, uint64_t capacity, uint64_t* required);
    /* Publishes width by height premultiplied BGRA pixels, row_bytes apart
     * (at least width * 4), to the endpoint behind compatibility_handle. */
    gf_status (GF_CALL* submit_bgra)(uint64_t compatibility_handle, uint32_t width, uint32_t height,
                                     uint64_t row_bytes, const uint8_t* pixels);
    /* Ends the endpoint; its token and handle stop working. */
    gf_status (GF_CALL* release)(gf_paint_endpoint endpoint);
    /* GetDC and ReleaseDC for the shim: the endpoint's memory DC. */
    gf_status (GF_CALL* get_dc)(uint64_t compatibility_handle, uint64_t* device_context);
    gf_status (GF_CALL* release_dc)(uint64_t compatibility_handle, uint64_t device_context);
    /* Publishes what was drawn into device_context. */
    gf_status (GF_CALL* publish_dc)(uint64_t device_context);
    /* A short lease that serializes one write into device_context against
     * resizing and release; end_write publishes (1) or not (0). */
    gf_status (GF_CALL* begin_write)(uint64_t device_context, gf_paint_write_lease* lease);
    gf_status (GF_CALL* end_write)(gf_paint_write_lease lease, uint32_t publish);
} gf_windows_paint_endpoint_table;

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
