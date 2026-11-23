#ifndef GUI_FORMS_C_GF_ABI_H
#define GUI_FORMS_C_GF_ABI_H
#include "gui_forms/c/gf_c_shield_begin.h"

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
 *   as its prefix (gf_status: GF_STATUS_*). This header is the library's
 *   one C surface: the experimental 0.x header was retired before 1.0.
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

/* The ABI version this header describes. 1.1 is the seal (checkpoint
 * cp24); 1.2 (owner decision D9, cp28) adds the window, focus, decoration,
 * anchored overlay, data grid, docking and fonts surfaces and the slots
 * marked since 1.2, additions only; 1.3 adds the class tables
 * (gf_abi_classes.h, GF_TABLE_CLASSES), additions only; 1.4 adds the
 * content boundary (GF_TABLE_CONTENT, GF_HOST_CAPABILITY_HOSTED_CONTENT),
 * additions only; 1.5 adds the window outer band and shaping, the
 * window's pointer location, the list view table (GF_TABLE_LIST_VIEW), the
 * failure detail (gf_failure_detail, diagnostics.failure_detail), the
 * gate frame kind, fixed-size array event fields and the composite controls
 * (gf_composite_spec and the custom controls table's load_composite,
 * create_composite, perform_composite and composite_child), the business card
 * and the diagnostic overlay (owner decision 4a, 2026-11-15:
 * gf_program_identity, application.set_program_identity,
 * windows.show_business_card, diagnostics.set_diagnostic_overlay and
 * toggle_diagnostic_overlay), relative pointer mode (the gf_controls_table
 * slots marked since 1.5 and the host capabilities RELATIVE_POINTER to
 * POINTER_WARP), application font families (gf_app_font_face and the
 * fonts table's load_family and unload_family) and the presentation shim's
 * host capabilities (DIRECT_BUFFERS, HOST_SCROLL and
 * REFERENCE_PRESENTATION), additions only; 1.6 (checkpoint cp35) adds the
 * procedural pattern CARBON_FIBRE and the multiple document interface: the
 * docking table's frames presentation
 * (gf_dock_document_presentation, gf_dock_frame_state,
 * gf_dock_frame_arrangement, gf_dock_frame_memory, the edit kinds SET_DOCUMENT_PRESENTATION to
 * SET_FRAME_ICON_SLOT with gf_dock_edit's 1.6 fields, and the docking slots
 * marked since 1.6), the window's MDI slots (gf_mdi_layout and the windows
 * table's slots marked since 1.6) and the window's mdi_child_activate
 * interaction, additions only; 1.7 adds the touchpad gestures (the
 * control's gesture interaction, GestureEventArgs fields by name, and
 * gf_control_hooks.on_gesture, appended), the host capability
 * DESKTOP_NOTIFICATIONS and the docking table's tool strip and ribbon
 * merging (the docking slots marked since 1.7) and the visible-area
 * guarantee (stage VA: gf_overflow_answer, gf_overflow_cause,
 * gf_content_alignment, gf_viewport_fit, gf_overflow_record, the windows
 * table's overflow_records, unanswered_overflow_count and safe_insets and
 * the controls table's set_viewport_fit and viewport_fit), additions
 * only; and a public state query per stateful handle kind that had
 * none (IC study decision 7): windows.lifecycle (gf_window_lifecycle) and
 * runtime.splashscreen_state (gf_splashscreen_state), additions only. The
 * behavioural constraints of every table are declared beside this header
 * (gf_abi_<table>.ic.toml) and generate the // ic: lines above its slots. */
#define GF_ABI_MAJOR 1u
#define GF_ABI_MINOR 7u

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
/* Since 1.2. An exception inside the library reached the function's
 * boundary while exception reflection is on (gf_diagnostics_table
 * set_exception_reflection): the function did nothing more, and the
 * calling thread's failure snapshot holds the failure and the shadow frames
 * it was thrown under. With reflection off the same failure is
 * GF_STATUS_INTERNAL, also with a snapshot. */
#define GF_STATUS_NATIVE_EXCEPTION 16

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
#define GF_TABLE_DATA_GRID 20u
#define GF_TABLE_DOCKING 21u
#define GF_TABLE_FONTS 22u
#define GF_TABLE_HELP 23u
/* Since 1.3: objects made from class tables (gf_abi_classes.h). */
#define GF_TABLE_CLASSES 24u
/* Since 1.4: content surfaces, content editing and hosted content. */
#define GF_TABLE_CONTENT 25u
/* Since 1.5: a list view's columns, items, groups and selection. */
#define GF_TABLE_LIST_VIEW 26u
/* Since 1.6: the marks a program sets for the library's MCP automation. */
#define GF_TABLE_AUTOMATION 27u
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

/* gf_splashscreen_state (since 1.7): where an open splashscreen handle
 * stands. SHOWN: its window shows. HANDED_OVER: the library's first window
 * closed it; the handle stays open, and the setters accept and present
 * nothing, until splashscreen_close. A closed handle is stale
 * (GF_STATUS_STALE_HANDLE), never a state. */
typedef uint32_t gf_splashscreen_state;
#define GF_SPLASHSCREEN_STATE_SHOWN 0u
#define GF_SPLASHSCREEN_STATE_HANDED_OVER 1u

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
// ic: gf_abi::runtime: generated from include/gui_forms/c/gf_abi_runtime.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. States: shown (initial), handed_over. Observed by
// ic: gf_runtime_table.splashscreen_state(): shown GF_SPLASHSCREEN_STATE_SHOWN, handed_over
// ic: GF_SPLASHSCREEN_STATE_HANDED_OVER. Thread: any, unless a call says otherwise. Identity: generational; a stale
// ic: one: returns GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_runtime_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Writes the versions into info. */
    // ic: Requires not_null(info), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument
    // ic: (struct_size is below the 1.0 record size), unchanged.
    gf_status (GF_CALL* version)(gf_abi_version_info* info);
    /*
     * Copies the calling thread's last failure into detail and its message
     * into message_buffer (not terminated). message_required receives the
     * message size. When message_capacity is smaller, nothing else is written
     * and the result is GF_STATUS_BUFFER_TOO_SMALL. message_buffer may be null
     * when message_capacity is zero.
     */
    // ic: Requires not_null(detail) and not_null(message_required) and (not_null(message_buffer) or message_capacity ==
    // ic: 0), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails buffer_too_small (When message_capacity is
    // ic: smaller, nothing else is written and the result is GF_STATUS_BUFFER_TOO_SMALL. message_buffer may be null
    // ic: when message_capacity is zero.), unchanged; invalid_argument (struct_size is below the 1.0 record size),
    // ic: unchanged.
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
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT for a record smaller than the 1.1 size, a null
    // ic: splashscreen, artwork that is not a PNG file or has an extent out of range, a status that is not UTF-8 or too
    // ic: long, or a progress outside [0, 1] with show_progress;), unchanged; invalid_state (GF_STATUS_INVALID_STATE
    // ic: while another splashscreen's handle is open (shown, or handed over and not yet closed);), unchanged;
    // ic: unsupported_capability (GF_STATUS_UNSUPPORTED_CAPABILITY when the host has no splashscreen (no display, a
    // ic: host without GF_TRANSIENT_SURFACE_SPLASHSCREEN) or could not present it.), unchanged. Thread: owner. Returns
    // ic: gf_splashscreen, owned by the caller.
    gf_status (GF_CALL* splashscreen_show)(const gf_splashscreen_options* options, gf_splashscreen* splashscreen);
    /*
     * Since 1.1. Replaces the status line and presents the frame at once.
     * After the handover it accepts and presents nothing.
     * GF_STATUS_STALE_HANDLE for a handle that is not the current
     * splashscreen's; GF_STATUS_INVALID_ARGUMENT, changing nothing, for text
     * that is not UTF-8 or too long; GF_STATUS_UNSUPPORTED_CAPABILITY when
     * the frame could not be presented.
     */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT, changing nothing, for text that is not UTF-8 or too
    // ic: long;), unchanged; invalid_state (stated by the code), unchanged; unsupported_capability
    // ic: (GF_STATUS_UNSUPPORTED_CAPABILITY when the frame could not be presented.), unchanged. Thread: owner.
    gf_status (GF_CALL* splashscreen_set_status)(gf_splashscreen splashscreen, gf_utf8_view status);
    /*
     * Since 1.1. Shows the bar at progress, in [0, 1], or hides it when
     * show_progress is zero (progress is then ignored), and presents the
     * frame at once; as set_status otherwise.
     */
    // ic: Fails invalid_argument (struct_size is below the 1.0 record size), unchanged; invalid_state (stated by the
    // ic: code), unchanged; unsupported_capability (stated by the code), unchanged. Thread: owner.
    gf_status (GF_CALL* splashscreen_set_progress)(gf_splashscreen splashscreen, uint32_t show_progress,
                                                   double progress);
    /*
     * Since 1.1. Closes the splashscreen's window if it still shows and ends
     * its handle; another may be shown after. GF_STATUS_STALE_HANDLE for a
     * handle that is not the current splashscreen's.
     */
    // ic: Thread: owner.
    gf_status (GF_CALL* splashscreen_close)(gf_splashscreen splashscreen);
    /*
     * Since 1.7. state receives where splashscreen stands
     * (gf_splashscreen_state). GF_STATUS_STALE_HANDLE for a handle that is
     * not the current splashscreen's; GF_STATUS_INVALID_ARGUMENT without
     * state. On the thread that showed it, as the other splashscreen
     * functions.
     */
    // ic: Requires not_null(state), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Thread: owner.
    gf_status (GF_CALL* splashscreen_state)(gf_splashscreen splashscreen, gf_splashscreen_state* state);
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
 * One registry per process (H3). A process has one library state, however
 * many consumers load the library or ask it for tables: one UI thread, one
 * set of object, subscription and rule identities, one theme, image
 * registry and set of published values, one application and one run. Two
 * consumers in one process (a C program and a plug-in it loads, say) share
 * all of it: a handle one hands out is valid for the other, a theme one
 * loads is the other's, and only one of them can run a window at a time.
 * Consumers that need separate state run in separate processes. No table
 * takes a context parameter, and 1.x adds none.
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
 *
 * reserved's low 16 bits hold, since 1.2, the font's bundled family: an id
 * from fonts.family_id (Selawik, say), whose face nearest the weight and
 * slant draws before the role's (never synthesized), or zero for the role's
 * faces alone. The role still draws what the family does not cover. Its
 * high 16 bits hold, since 1.3, the font's family stack: the families a
 * theme's font-family list names after the first, drawn after the family
 * and before the role. A stack number is the process's: read it from a font
 * (properties.get) and pass it back within the same process, never store
 * it. An id or stack number the library does not have gives
 * GF_STATUS_INVALID_ARGUMENT; a 1.2 library takes only zero in the high
 * bits, a 1.1 library only zero. (The word keeps its sealed name: renaming
 * a sealed field is a break.)
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
 * Colour layouts (K5). Three layouts appear in this header, each where it
 * is named: a gf_value colour (and every rgba field) is one 32-bit word
 * 0xRRGGBBAA, red in the high byte, alpha in the low byte, not
 * premultiplied; pixel buffers (paint targets, surfaces, endpoints) are
 * premultiplied BGRA bytes, blue first in memory; and Windows Forms'
 * Color.ToArgb, which a binding converts from, is 0xAARRGGBB. A binding
 * converts at its boundary; the library never guesses. One layout is a
 * candidate for 2.0.
 */

/*
 * Arrays of records the caller passes (N14): every array of size-prefixed
 * records in this header (gf_value.elements, gf_rule.conditions,
 * gf_rule.actions, gf_application_config.packages, and the data grid's
 * rows, cells and values) has the stride of its first element's
 * struct_size, the caller's sizeof, which every element repeats; it is at
 * least the record's first size and a multiple of 8. The library steps by
 * that stride, so a record that grows in a later minor is still read
 * correctly from a caller built against an earlier one. A record embedded
 * by value (gf_custom_control_spec.callbacks) is read up to its own
 * struct_size.
 */

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
 * (properties.element_at, events.field_element); an event field of a
 * fixed-size array also lends its elements in place (Event fields below);
 * a COLLECTION the caller
 * passes lists its elements in elements and element_count, each a gf_value
 * of the collection's item kind (not itself a COLLECTION), read during the
 * call only, stepped by the first element's struct_size (the array rule
 * above). A COLLECTION the caller passes holds at most 4096 elements (N16;
 * GF_STATUS_LIMIT_EXCEEDED beyond): a larger list belongs in a data
 * grid's rows (GF_TABLE_DATA_GRID) or a bound source (GF_TABLE_BINDINGS),
 * which add rows in calls of their own. An OBJECT is an object's identity in unsigned_integer (event
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
 * - a fixed-size array (since 1.5; a C++ std::array<T, N>, which the schema
 *   spells as its element kind and count, "target: number[3]") is
 *   COLLECTION with N in unsigned_integer and in element_count, and
 *   elements pointing at N contiguous gf_value records (stride
 *   sizeof(gf_value); null when N is zero), each of the element kind:
 *   BOOLEAN, SIGNED, UNSIGNED, NUMBER, ENUMERATION (its choice's name in
 *   text), POINT, SIZE, RECTANGLE, INSETS or COLOR. The records are lent
 *   for the invoke like the field's value, and events.field_element reads
 *   the same ones. A writable one takes a COLLECTION of exactly N elements
 *   of the element kind, each admitted as a field of that kind would be,
 *   stepped by the first element's struct_size and read during the call
 *   only; the array is replaced whole;
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
 * named fields (Event fields above). "closing" and "closed" carry one
 * read-only ENUMERATION field "reason": why the window is asked to close or
 * closed, as text and number: "user" (0, the user closed the window),
 * "application" (1, the program closed it), "session_end" (2, the user's
 * session is ending: log out, restart, shut down), "test" (3) or "quit"
 * (4, the user quit the application from the system, as macOS Cmd-Q). A
 * "closing" with reason "session_end" may be cancelled only where the
 * platform lets an application hold a session's end. field_count is zero
 * for the other events. A callback writes a writable field with events.write_field; the
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
 *   binds to, or (since 1.1) a control in a window, which binds it to that
 *   control's window: windows.run's root, or a control of its tree, during
 *   the run (from its ready callback, say), or a created window's root
 *   before its run. A control in no window gives
 *   GF_STATUS_WRONG_HANDLE_KIND.
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

// ic: gf_abi::objects: generated from include/gui_forms/c/gf_abi_objects.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. States: alive (initial), disposing, disposed. Observed by gf_objects_table.state():
// ic: alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed GF_OBJECT_STATE_DISPOSED. Thread: ui,
// ic: unless a call says otherwise. Identity: generational; a stale one: returns GF_STATUS_STALE_HANDLE, unchanged.
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
    // ic: Requires not_null(object), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument
    // ic: (Creates an object of the schema type type_name, written in any spelling of its id ("check_box" or
    // ic: "CheckBox"), with a stable_id: one or more '.'-separated segments of lowercase ASCII letters and digits
    // ic: joined by single '-' ("settings.ok-button"; a Windows Forms name such as "textBox1" is spelled "text-box1"),
    // ic: GF_STATUS_INVALID_ARGUMENT with a message stating the rule otherwise.), unchanged; not_found (A type that
    // ic: needs another object (a dock site its workspace or a timer its window, say: create_with), and the types
    // ic: neither slot makes (a binding, which GF_TABLE_BINDINGS makes, and live_surface), give
    // ic: GF_STATUS_UNSUPPORTED_CAPABILITY; an unknown one GF_STATUS_NOT_FOUND.), unchanged; unsupported_capability (A
    // ic: type that needs another object (a dock site its workspace or a timer its window, say: create_with), and the
    // ic: types neither slot makes (a binding, which GF_TABLE_BINDINGS makes, and live_surface), give
    // ic: GF_STATUS_UNSUPPORTED_CAPABILITY; an unknown one GF_STATUS_NOT_FOUND.), unchanged. Returns gf_object, owned
    // ic: by the caller.
    gf_status (GF_CALL* create)(gf_utf8_view type_name, gf_utf8_view stable_id,
                                gf_object* object);
    /* Adds a reference. Any thread. */
    // ic: Thread: any.
    gf_status (GF_CALL* retain)(gf_object object);
    /* Gives back a reference. Any thread; see Threads above. */
    // ic: Thread: any.
    gf_status (GF_CALL* release)(gf_object object);
    /* Disposes the object and its subtree. Their subscriptions end (the
     * releases run before dispose returns) and their posted callbacks only
     * release. */
    gf_status (GF_CALL* dispose)(gf_object object);
    // ic: Requires not_null(state), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* state)(gf_object object, gf_object_state* state);
    /* Copy the schema type id ("check_box"; for an application's own
     * subclass, its nearest schema type's; for a composite control, its
     * definition's type, the document's name with '-' written '_':
     * "kit_card" for kit-card) or the stable id into buffer
     * (not terminated); required receives its size, also with
     * GF_STATUS_BUFFER_TOO_SMALL. */
    // ic: Fails buffer_too_small (Copy the schema type id ("check_box"; for an application's own subclass, its nearest
    // ic: schema type's; for a composite control, its definition's type, the document's name with '-' written '_':
    // ic: "kit_card" for kit-card) or the stable id into buffer (not terminated); required receives its size, also with
    // ic: GF_STATUS_BUFFER_TOO_SMALL.), unchanged.
    gf_status (GF_CALL* type_id)(gf_object object, char* buffer, uint64_t capacity,
                                 uint64_t* required);
    gf_status (GF_CALL* stable_id)(gf_object object, char* buffer, uint64_t capacity,
                                   uint64_t* required);
    /* Puts child, which has no parent, under parent at index (at most the
     * child count; the count appends). Children are ordered back to front:
     * the last one paints on top. A child cannot be its own ancestor. A
     * tree nests at most 64 levels, the root counted as one: a child whose
     * subtree would end deeper gives GF_STATUS_LIMIT_EXCEEDED, changing
     * nothing. This
     * is the ABI's one child order (K4), painter order: index 0 is the
     * bottom-most child. A Windows Forms binding, whose Controls collection
     * puts index 0 on top (z-order), reverses the index at its boundary. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: a control cannot be its own ancestor), unchanged; invalid_state (code: the child already has a
    // ic: parent), unchanged; limit_exceeded (A tree nests at most 64 levels, the root counted as one: a child whose
    // ic: subtree would end deeper gives GF_STATUS_LIMIT_EXCEEDED, changing nothing.), unchanged.
    gf_status (GF_CALL* insert_child)(gf_object parent, gf_object child, uint64_t index);
    /* Takes child from parent; it then ends at its last release. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: the child is not a child of the parent), unchanged.
    gf_status (GF_CALL* remove_child)(gf_object parent, gf_object child);
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* child_count)(gf_object parent, uint64_t* count);
    /* A new reference to the child at index, in the order above. */
    // ic: Requires not_null(child), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument (code:
    // ic: child_at needs an output), unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* child_at)(gf_object parent, uint64_t index, gf_object* child);
    /* A new reference to the parent, or zero when there is none. */
    // ic: Requires not_null(parent), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Returns gf_object, owned by
    // ic: the caller.
    gf_status (GF_CALL* parent)(gf_object object, gf_object* parent);
    /* Creates an object of a type that needs another object, as
     * gf_create_arguments describes. An owner of the wrong type gives
     * GF_STATUS_WRONG_HANDLE_KIND, a stale one GF_STATUS_STALE_HANDLE, a
     * malformed enumeration GF_STATUS_INVALID_ARGUMENT; a type create makes
     * is made as create makes it when arguments asks nothing of it. */
    // ic: Requires not_null(object), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires not_null(arguments),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires arguments_size >= create_arguments_size_1_0,
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: create_with_object, then
    // ic: create_with_arguments, then create_with_arguments_size. Fails invalid_argument (An owner of the wrong type
    // ic: gives GF_STATUS_WRONG_HANDLE_KIND, a stale one GF_STATUS_STALE_HANDLE, a malformed enumeration
    // ic: GF_STATUS_INVALID_ARGUMENT; a type create makes is made as create makes it when arguments asks nothing of
    // ic: it.), unchanged; not_found (code: the schema has no type of that name), unchanged; unsupported_capability
    // ic: (code: a window is made by windows.create), unchanged; wrong_handle_kind (An owner of the wrong type gives
    // ic: GF_STATUS_WRONG_HANDLE_KIND, a stale one GF_STATUS_STALE_HANDLE, a malformed enumeration
    // ic: GF_STATUS_INVALID_ARGUMENT; a type create makes is made as create makes it when arguments asks nothing of
    // ic: it.), unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* create_with)(gf_utf8_view type_name, gf_utf8_view stable_id,
                                     const gf_create_arguments* arguments, gf_object* object);
} gf_objects_table;

/*
 * GF_TABLE_PROPERTIES: properties by name, as the control schema
 * (manifests/control-schema.json) spells them.
 */
// ic: gf_abi::properties: generated from include/gui_forms/c/gf_abi_properties.ic.toml by tools/ic_declarations.py;
// ic: edit the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_properties_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Reads a property into value (an output record whose struct_size the
     * caller sets, at least the 1.0 size). Text is copied into text_buffer
     * and value->text points there; text_required receives its size (zero
     * for other kinds), also with GF_STATUS_BUFFER_TOO_SMALL. An unknown or
     * unreadable property gives GF_STATUS_NOT_FOUND; a kind gf_value cannot
     * hold yet gives GF_STATUS_UNSUPPORTED_CAPABILITY. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails buffer_too_small
    // ic: (Text is copied into text_buffer and value->text points there; text_required receives its size (zero for
    // ic: other kinds), also with GF_STATUS_BUFFER_TOO_SMALL.), unchanged; invalid_argument (code: a property name is
    // ic: not empty), unchanged; not_found (An unknown or unreadable property gives GF_STATUS_NOT_FOUND; a kind
    // ic: gf_value cannot hold yet gives GF_STATUS_UNSUPPORTED_CAPABILITY.), unchanged; unsupported_capability (An
    // ic: unknown or unreadable property gives GF_STATUS_NOT_FOUND; a kind gf_value cannot hold yet gives
    // ic: GF_STATUS_UNSUPPORTED_CAPABILITY.), unchanged; wrong_handle_kind (code: the object is a component, not a
    // ic: control or a window), unchanged.
    gf_status (GF_CALL* get)(gf_object object, gf_utf8_view name, gf_value* value,
                             char* text_buffer, uint64_t text_capacity,
                             uint64_t* text_required);
    /* Writes a property. A value that does not convert to the property's
     * kind gives GF_STATUS_INVALID_ARGUMENT, a read-only property
     * GF_STATUS_INVALID_STATE, an unknown one GF_STATUS_NOT_FOUND. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (A
    // ic: value that does not convert to the property's kind gives GF_STATUS_INVALID_ARGUMENT, a read-only property
    // ic: GF_STATUS_INVALID_STATE, an unknown one GF_STATUS_NOT_FOUND.), unchanged; invalid_state (A value that does
    // ic: not convert to the property's kind gives GF_STATUS_INVALID_ARGUMENT, a read-only property
    // ic: GF_STATUS_INVALID_STATE, an unknown one GF_STATUS_NOT_FOUND.), unchanged; not_found (A value that does not
    // ic: convert to the property's kind gives GF_STATUS_INVALID_ARGUMENT, a read-only property
    // ic: GF_STATUS_INVALID_STATE, an unknown one GF_STATUS_NOT_FOUND.), unchanged; wrong_handle_kind (code: the object
    // ic: is a component, not a control or a window), unchanged.
    gf_status (GF_CALL* set)(gf_object object, gf_utf8_view name, const gf_value* value);
    /* Returns a property to its default. GF_STATUS_UNSUPPORTED_CAPABILITY when
     * it has no reset path. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: a property name is not empty), unchanged; not_found (code: a window has no property of that name),
    // ic: unchanged; unsupported_capability (GF_STATUS_UNSUPPORTED_CAPABILITY when it has no reset path.), unchanged;
    // ic: wrong_handle_kind (code: the object is a component, not a control or a window), unchanged.
    gf_status (GF_CALL* reset)(gf_object object, gf_utf8_view name);
    /* The element at index of the COLLECTION property name, as get reports
     * a value (text copied into text_buffer). GF_STATUS_INVALID_ARGUMENT
     * when the property is not a collection, GF_STATUS_NOT_FOUND when index
     * is not below its count. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT when the property is not a collection, GF_STATUS_NOT_FOUND when index is not
    // ic: below its count.), unchanged; not_found (GF_STATUS_INVALID_ARGUMENT when the property is not a collection,
    // ic: GF_STATUS_NOT_FOUND when index is not below its count.), unchanged; wrong_handle_kind (code: the object is a
    // ic: component, not a control or a window), unchanged.
    gf_status (GF_CALL* element_at)(gf_object object, gf_utf8_view name, uint64_t index, gf_value* value,
                                    char* text_buffer, uint64_t text_capacity, uint64_t* text_required);
    /* Since 1.2. Reads count elements of the COLLECTION property name from
     * position first into values, an array of count records (the array
     * rule above; fields past the 1.0 size are left alone), their text
     * copied into text_buffer one after another, each value's view pointing
     * there. text_required receives the total text size, also with
     * GF_STATUS_BUFFER_TOO_SMALL, which writes no value. The collection is
     * read once per call, where element_at reads it once per element: read
     * a long list here. A range past the count gives GF_STATUS_NOT_FOUND;
     * more than GF_PROPERTY_MAXIMUM_ELEMENTS_PER_CALL elements
     * GF_STATUS_LIMIT_EXCEEDED; otherwise as element_at. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires
    // ic: not_null(text_required) and (not_null(text_buffer) or text_capacity == 0), else returns
    // ic: GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: read_elements_text_buffer, then the state
    // ic: refusal. Fails buffer_too_small (Reads count elements of the COLLECTION property name from position first
    // ic: into values, an array of count records (the array rule above; fields past the 1.0 size are left alone), their
    // ic: text copied into text_buffer one after another, each value's view pointing there. text_required receives the
    // ic: total text size, also with GF_STATUS_BUFFER_TOO_SMALL, which writes no value.), unchanged; invalid_argument
    // ic: (code: read_elements needs a text size output, and a buffer unless its capacity is zero), unchanged;
    // ic: limit_exceeded (A range past the count gives GF_STATUS_NOT_FOUND; more than
    // ic: GF_PROPERTY_MAXIMUM_ELEMENTS_PER_CALL elements GF_STATUS_LIMIT_EXCEEDED; otherwise as element_at.),
    // ic: unchanged; not_found (A range past the count gives GF_STATUS_NOT_FOUND; more than
    // ic: GF_PROPERTY_MAXIMUM_ELEMENTS_PER_CALL elements GF_STATUS_LIMIT_EXCEEDED; otherwise as element_at.),
    // ic: unchanged; unsupported_capability (code: an element's value kind has no gf_value form in this library),
    // ic: unchanged; wrong_handle_kind (code: the object is a component, not a control or a window), unchanged.
    gf_status (GF_CALL* read_elements)(gf_object object, gf_utf8_view name, uint64_t first, uint64_t count,
                                       gf_value* values, char* text_buffer, uint64_t text_capacity,
                                       uint64_t* text_required);
    /* Since 1.2 (N19). get and set by the property's position in the
     * object's schema type, as schema.property_at lists it (for a window,
     * the window type's): a binding resolves a name once, then reaches it
     * by index. A control of a type the schema does not list, or an
     * application subclass, uses its nearest schema type's list (its own
     * extra properties are reached by name). An index past the list gives
     * GF_STATUS_INVALID_ARGUMENT; otherwise as get and set. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (An
    // ic: index past the list gives GF_STATUS_INVALID_ARGUMENT; otherwise as get and set.), unchanged; not_found (code:
    // ic: the control's type is not in the schema), unchanged; wrong_handle_kind (code: the object is a component, not
    // ic: a control or a window), unchanged.
    gf_status (GF_CALL* get_at)(gf_object object, uint64_t index, gf_value* value, char* text_buffer,
                                uint64_t text_capacity, uint64_t* text_required);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: the index is past the type's properties), unchanged; not_found (code: the control's type is not in the
    // ic: schema), unchanged; wrong_handle_kind (code: the object is a component, not a control or a window),
    // ic: unchanged.
    gf_status (GF_CALL* set_at)(gf_object object, uint64_t index, const gf_value* value);
} gf_properties_table;

/* The most elements one properties.read_elements call reads (since 1.2). */
#define GF_PROPERTY_MAXIMUM_ELEMENTS_PER_CALL 65536u

/*
 * GF_TABLE_EVENTS. A subscription made during an emission is first called by
 * the next one; one ended during an emission is not called again, even later
 * in that emission. A subscription ends at disconnect, when its source is
 * disposed, or when its source is destroyed.
 */
// ic: gf_abi::events: generated from include/gui_forms/c/gf_abi_events.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. States: alive (initial), disposing, disposed. Observed by gf_objects_table.state():
// ic: alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed GF_OBJECT_STATE_DISPOSED. Thread: ui,
// ic: unless a call says otherwise. Identity: generational; a stale one: returns GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_events_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Subscribes to an interaction of source by its schema name: any
     * interaction the schema lists for source's type (schema.interaction_at).
     * The event's fields carry the payload (Event fields above). A name the
     * type does not raise gives GF_STATUS_NOT_FOUND. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires
    // ic: not_null(subscription), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: subscription_output, then the state refusal. Fails not_found (A name the type does not raise gives
    // ic: GF_STATUS_NOT_FOUND.), unchanged. Returns gf_subscription, owned by the caller. Transfers callback from
    // ic: caller to library.
    gf_status (GF_CALL* subscribe)(gf_object source, gf_utf8_view interaction,
                                   const gf_callback* callback,
                                   gf_subscription* subscription);
    /* Subscribes to changes of one property; the event has no payload. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires
    // ic: not_null(subscription), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: subscription_output, then the state refusal. Fails not_found (code: the source has no property of that name),
    // ic: unchanged. Returns gf_subscription, owned by the caller. Transfers callback from caller to library.
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
    // ic: Fails invalid_argument (GF_STATUS_INVALID_STATE when event is not running, GF_STATUS_NOT_FOUND for an index
    // ic: past the fields, GF_STATUS_INVALID_ARGUMENT for a field that is not writable or a value it does not admit;
    // ic: nothing is written then.), unchanged; invalid_state (GF_STATUS_INVALID_STATE when event is not running,
    // ic: GF_STATUS_NOT_FOUND for an index past the fields, GF_STATUS_INVALID_ARGUMENT for a field that is not writable
    // ic: or a value it does not admit; nothing is written then.), unchanged; not_found (GF_STATUS_INVALID_STATE when
    // ic: event is not running, GF_STATUS_NOT_FOUND for an index past the fields, GF_STATUS_INVALID_ARGUMENT for a
    // ic: field that is not writable or a value it does not admit; nothing is written then.), unchanged.
    gf_status (GF_CALL* write_field)(gf_event* event, uint64_t field, const gf_value* value);
    /* Reads element index of a COLLECTION field of the event a callback is
     * running for, from within that callback, into element (the caller sets
     * struct_size); its text is lent for the invoke.
     * GF_STATUS_UNSUPPORTED_CAPABILITY when the elements are structs. */
    // ic: Fails invalid_state (code: no callback is running for that event), unchanged; unsupported_capability
    // ic: (GF_STATUS_UNSUPPORTED_CAPABILITY when the elements are structs.), unchanged.
    gf_status (GF_CALL* field_element)(const gf_event* event, uint64_t field, uint64_t index,
                                       gf_value* element);
} gf_events_table;

/*
 * GF_TABLE_DISPATCH: work for the UI thread. Posted callbacks run in the
 * order they were posted, while a window runs or at run_pending.
 */
// ic: gf_abi::dispatch: generated from include/gui_forms/c/gf_abi_dispatch.ic.toml by tools/ic_declarations.py; edit
// ic: the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_dispatch_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Posts callback to run once on the UI thread with sender target and
     * name "invoke". Any thread. target is retained until then; when it has
     * been disposed by then, only release runs. At most 4096 callbacks wait
     * at once (GF_STATUS_LIMIT_EXCEEDED). */
    // ic: Fails limit_exceeded (At most 4096 callbacks wait at once (GF_STATUS_LIMIT_EXCEEDED).), unchanged. Thread:
    // ic: any. Transfers callback from caller to library.
    gf_status (GF_CALL* begin_invoke)(gf_object target, const gf_callback* callback);
    /* Completes deferred releases and runs the callbacks posted before this
     * call; invoked receives how many ran. */
    // ic: Requires not_null(invoked), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* run_pending)(uint64_t* invoked);
} gf_dispatch_table;

/* gf_run_flags: how a window runs. None are defined at 1.0; a later minor
 * may add some, and this library refuses any it does not know. */
typedef uint32_t gf_run_flags;
#define GF_RUN_FLAGS_NONE 0u

/* An interaction: payload lists the event fields its callback receives
 * (Event fields above), each as "name: kind" with the schema's kind words,
 * separated by ", " ("value: boolean"; "value_1: unsigned_integer, value_2:
 * enumeration"; "cancel: boolean, row_id: text"), " optional" after an
 * OPTIONAL field's kind, and a fixed-size array's kind as its element kind
 * and count ("target: number[3]", since 1.5); empty for none (a notification, or a reference to
 * the sender). Since 1.2 it is in this vocabulary; 1.1 spelled the C++
 * argument types. manifests/abi/interaction_fields.json maps the C++
 * spellings to it. mutable_payload is 1 when a handler may change an
 * argument, as a cancellable validation's. The schema table lists a type's
 * in it, and the windows table a window's. */
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
 * Since 1.2 (read when struct_size covers them): minimum_width and
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
 * 1.2). owner is the root of the running window or of an owned window that
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
 * since 1.2), one bit each, as window_properties reports them. */
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

/* The window outer band (since 1.5; the owner's approval of 2026-11-13):
 * a thin strip a window reserves along the edges of its client area,
 * drawn above everything the window draws and hitting nothing. Declaring
 * it reserves its widths at once and lays the window out again; nothing
 * the band shows afterwards (lit, flashing, cycling, a colour, a texture)
 * lays anything out: only its strips repaint. Undeclaring it, a scale
 * change and full screen with GF_WINDOW_BAND_FULL_SCREEN_HIDES are the
 * other changes that lay the window out again. Widths are logical units,
 * snapped per edge to whole device pixels (at least one), and at most
 * gf_host_capabilities.window_band_maximum_width. The content rectangle is
 * the client area less the reserved band (windows.content_rect).
 *
 * gf_window_band_full_screen: whether a declared band keeps its
 * reservation while the window is full screen (where the host reports it,
 * GF_HOST_CAPABILITY_WINDOW_STATE_REPORTS). */
typedef uint32_t gf_window_band_full_screen;
#define GF_WINDOW_BAND_FULL_SCREEN_STAYS 0u
#define GF_WINDOW_BAND_FULL_SCREEN_HIDES 1u

/* gf_window_band_flags: how gf_window_band_options gives its widths.
 * PER_EDGE: edges gives each side's width (0 reserves nothing there) and
 * width is ignored; without it width is every side's (0 takes the theme's
 * band width, else one logical unit). */
typedef uint32_t gf_window_band_flags;
#define GF_WINDOW_BAND_PER_EDGE 1u

/* A band declaration (windows.declare_band). An input record: struct_size
 * at least its 1.5 size, 56; reserved is zero. */
typedef struct gf_window_band_options {
    uint32_t struct_size;
    gf_window_band_flags flags;
    gf_window_band_full_screen full_screen;
    uint32_t reserved;
    double width;
    gf_logical_insets edges;
} gf_window_band_options;

/* gf_window_band_state: what the band shows. REST draws the rest colour
 * (transparent unless set, so a resting band is reserved space showing the
 * window's base or the texture); LIT the lit colour; FLASHING alternates
 * them; CYCLING blends between them along a cosine, starting lit. The
 * library's limiter holds every flash and cycle to at most 1 cycle a
 * second, never flashes a saturated red, and rests the band lit under
 * reduced motion, with ambient motion off and in an occluded window. High
 * contrast draws the high-contrast focus colour, solid, untextured. */
typedef uint32_t gf_window_band_state;
#define GF_WINDOW_BAND_STATE_REST 0u
#define GF_WINDOW_BAND_STATE_LIT 1u
#define GF_WINDOW_BAND_STATE_FLASHING 2u
#define GF_WINDOW_BAND_STATE_CYCLING 3u

/* gf_window_band_role: the theme colour the band lights in when no lit
 * colour is given. THEME is the theme's band lit colour, else the focus
 * ring's; the others are the theme's colours of those names. */
typedef uint32_t gf_window_band_role;
#define GF_WINDOW_BAND_ROLE_THEME 0u
#define GF_WINDOW_BAND_ROLE_FOCUS 1u
#define GF_WINDOW_BAND_ROLE_ACCENT 2u
#define GF_WINDOW_BAND_ROLE_ACCENT_LIGHT 3u
#define GF_WINDOW_BAND_ROLE_TEXT 4u
#define GF_WINDOW_BAND_ROLE_BORDER 5u

/* gf_window_band_appearance_flags: which colours gf_window_band_appearance
 * gives; a colour not given is the theme's. */
typedef uint32_t gf_window_band_appearance_flags;
#define GF_WINDOW_BAND_REST_COLOUR 1u
#define GF_WINDOW_BAND_LIT_COLOUR 2u

/* gf_window_band_texture: a texture under the band's colour, one surface
 * per strip. NONE draws none. PROCEDURAL: texture_kind is a
 * GF_PROCEDURAL_PATTERN_*, texture_seed its seed (for HATCH, which has no
 * noise, its GF_HATCH_STYLE_* instead), texture_primary and
 * texture_secondary its ground and figure colours, texture_tile its tile
 * (2 to 256 logical units; a hatch's tile is one cell of 8 by 8 squares)
 * and texture_strength its strength (0 to 1).
 * FINISH: texture_kind is a GF_FINISH_* (not the glint), texture_seed its
 * seed (as int32), texture_primary its paint and texture_secondary its
 * second colour (dust or weft), texture_tile its grain (0.5 to 4) and
 * texture_strength its relief (0 to 4); its light and sheen are the
 * library's defaults. texture_opacity is 0 to 1. */
typedef uint32_t gf_window_band_texture;
#define GF_WINDOW_BAND_TEXTURE_NONE 0u
#define GF_WINDOW_BAND_TEXTURE_PROCEDURAL 1u
#define GF_WINDOW_BAND_TEXTURE_FINISH 2u
#define GF_PROCEDURAL_PATTERN_FELT 0u
#define GF_PROCEDURAL_PATTERN_WOOD_GRAIN 1u
#define GF_PROCEDURAL_PATTERN_PAPER_FIBRE 2u
#define GF_PROCEDURAL_PATTERN_PINSTRIPE 3u
#define GF_PROCEDURAL_PATTERN_BRUSHED_METAL 4u
#define GF_PROCEDURAL_PATTERN_FRESCO 5u
#define GF_PROCEDURAL_PATTERN_CHECKER 6u
#define GF_PROCEDURAL_PATTERN_HATCH 7u
#define GF_PROCEDURAL_PATTERN_CARBON_FIBRE 8u
#define GF_HATCH_STYLE_HORIZONTAL 0u
#define GF_HATCH_STYLE_VERTICAL 1u
#define GF_HATCH_STYLE_FORWARD_DIAGONAL 2u
#define GF_HATCH_STYLE_BACKWARD_DIAGONAL 3u
#define GF_HATCH_STYLE_CROSS 4u
#define GF_HATCH_STYLE_DIAGONAL_CROSS 5u
#define GF_FINISH_DUSTY_MATTE 0u
#define GF_FINISH_ORANGE_PEEL 1u
#define GF_FINISH_HAMMERTONE 2u
#define GF_FINISH_WRINKLE 3u
#define GF_FINISH_BEAD_BLAST 4u
#define GF_FINISH_SAND_CAST 5u
#define GF_FINISH_FLAT 6u
#define GF_FINISH_CLOTH_WEAVE 7u

/* The band's appearance (windows.set_band_appearance). An input record:
 * struct_size at least its 1.5 size, 80; reserved is zero. Colours are
 * 0xRRGGBBAA. frequency is in cycles a second (finite, not negative; 0
 * holds the band lit), lowered to the limiter's 1. */
typedef struct gf_window_band_appearance {
    uint32_t struct_size;
    gf_window_band_state state;
    gf_window_band_appearance_flags flags;
    gf_window_band_role lit_role;
    uint32_t rest_colour;
    uint32_t lit_colour;
    double frequency;
    gf_window_band_texture texture;
    uint32_t texture_kind;
    uint32_t texture_seed;
    uint32_t texture_primary;
    uint32_t texture_secondary;
    uint32_t reserved;
    double texture_tile;
    double texture_strength;
    double texture_opacity;
} gf_window_band_appearance;

/* gf_window_band_status: the band as it stands (gf_window_band_report).
 * DECLARED; RESERVED (declared, not hidden by full screen, some edge
 * wider than 0); FLASHING and CYCLING (a motion runs now, after the
 * limiter); TEXTURED (the strips draw a texture now). */
typedef uint32_t gf_window_band_status;
#define GF_WINDOW_BAND_DECLARED 1u
#define GF_WINDOW_BAND_RESERVED 2u
#define GF_WINDOW_BAND_FLASHING 4u
#define GF_WINDOW_BAND_CYCLING 8u
#define GF_WINDOW_BAND_TEXTURED 16u

/* The band's report (windows.band_report). An output record: struct_size
 * at least its 1.5 size, 72. edges are the reserved widths after snapping;
 * drawn_colour is the colour the strips draw now; frequency is the running
 * motion's after the limiter (0 when none runs); flash_changes and
 * cycle_frames count the strip repaints of each motion since the window was
 * made. */
typedef struct gf_window_band_report {
    uint32_t struct_size;
    gf_window_band_status status;
    gf_logical_insets edges;
    uint32_t drawn_colour;
    gf_window_band_full_screen full_screen;
    double frequency;
    uint64_t flash_changes;
    uint64_t cycle_frames;
} gf_window_band_report;

/* gf_window_corner_preference (since 1.5): the corners a window asks its
 * host for. SYSTEM keeps the platform's; SQUARE and ROUND ask, and the
 * host reports what it did (gf_window_shaping). */
typedef uint32_t gf_window_corner_preference;
#define GF_WINDOW_CORNER_PREFERENCE_SYSTEM 0u
#define GF_WINDOW_CORNER_PREFERENCE_SQUARE 1u
#define GF_WINDOW_CORNER_PREFERENCE_ROUND 2u

/* gf_window_shaping_flags (since 1.5): what a window's host lets it rely
 * on. HOST_ATTACHED: a host shows the window (the other host bits are zero
 * without it). CORNERS_REPORTED: GF_HOST_CAPABILITY_WINDOW_CORNERS.
 * STATE_REPORTED: GF_HOST_CAPABILITY_WINDOW_STATE_REPORTS. NATIVE_BORDER:
 * the host draws the band's colour itself outside the client area (no
 * host yet). FULL_SCREEN: the host reported the window full screen.
 * COMPLIED: the corners are as the preference asked. */
typedef uint32_t gf_window_shaping_flags;
#define GF_WINDOW_SHAPING_HOST_ATTACHED 1u
#define GF_WINDOW_SHAPING_CORNERS_REPORTED 2u
#define GF_WINDOW_SHAPING_STATE_REPORTED 4u
#define GF_WINDOW_SHAPING_NATIVE_BORDER 8u
#define GF_WINDOW_SHAPING_FULL_SCREEN 16u
#define GF_WINDOW_SHAPING_COMPLIED 32u

/* A window's shaping report (windows.shaping). An output record:
 * struct_size at least its 1.5 size, 32. corner_shapes is the set of
 * gf_window_corner_shape bits the host may report; corner_shape is the
 * one bit of the window's corners now, 0 when unknown (the band then draws
 * square); corner_radius is in logical units, for ROUNDED only. */
typedef struct gf_window_shaping {
    uint32_t struct_size;
    gf_window_shaping_flags flags;
    gf_window_corner_preference preference;
    uint32_t corner_shapes;
    uint32_t corner_shape;
    uint32_t reserved;
    double corner_radius;
} gf_window_shaping;

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
 * presentation_changed, theme_changed, and since 1.6 mdi_child_activate,
 * listed last: previous and current, the active MDI child's id before and
 * after, absent when there was none or none is left).
 *
 * Key input (since 1.2). A window's key_down and key_up reach a subscriber
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
 * root control (since 1.2), owned by the running window or by another of
 * them: a modal window, as Form.ShowDialog shows one, or a non-modal owned
 * window, as Form.Show(owner). Their stable id is their root's, unique
 * among open windows ("main" is the running window's). request_close
 * closes them too. They share the library's theme, images and published
 * values; since 1.2 a value published or withdrawn while they are open
 * reaches them as it reaches the running window.
 *
 * A window has properties (since 1.2), Form's for the native window: text
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
 *
 * The multiple document interface (since 1.6; DK decisions 60 to 63):
 * Windows Forms' Form MDI surface over the window's docking document area.
 * An MDI child is a document of the area, named by its panel id (the
 * docking table's), never a second window. A dock workspace's main site
 * binds the area while it is attached to the window, and unbinds it when it
 * leaves; without an area the window has no children, and activating or
 * arranging changes nothing (activated and arranged receive 0). The
 * container and anchor settings are kept until an area binds, then given to
 * it. Each MDI slot takes window_or_root, a window from create or a control
 * in one; GF_STATUS_INVALID_STATE for a control not in a window,
 * GF_STATUS_DISPOSED for a window disposed or whose run has ended. UI
 * thread.
 */
/* gf_mdi_layout (since 1.6): the arrangements layout_mdi offers (Windows
 * Forms' MdiLayout). */
typedef uint32_t gf_mdi_layout;
/* gf_window_lifecycle (since 1.7): where a window from windows.create
 * stands. CREATED: made, not yet run (windows.run admits it). RUNNING:
 * its run is under way, from run's start until the window is destroyed.
 * ENDED: its run has ended, or it was disposed before one; it refuses what
 * needs the window with GF_STATUS_DISPOSED. */
typedef uint32_t gf_window_lifecycle;
#define GF_WINDOW_LIFECYCLE_CREATED 0u
#define GF_WINDOW_LIFECYCLE_RUNNING 1u
#define GF_WINDOW_LIFECYCLE_ENDED 2u
#define GF_MDI_LAYOUT_CASCADE 0u
#define GF_MDI_LAYOUT_TILE_HORIZONTAL 1u
#define GF_MDI_LAYOUT_TILE_VERTICAL 2u
#define GF_MDI_LAYOUT_ARRANGE_ICONS 3u

/*
 * The visible-area guarantee (since 1.7; stage VA): no content is outside
 * its viewport unless the viewport's owner answers that overflow, and every
 * overflow is recorded by the window. A viewport owner is any control: its
 * children are checked against its child viewport and its design size (the
 * design_size property, a SIZE, 0 x 0 for none) against the same rectangle;
 * the root is also checked against the window's root slot. The answer is
 * the control's viewport fit (controls.set_viewport_fit); an owner that
 * scrolls its content answers by scrolling.
 */
/* gf_overflow_answer (since 1.7): what a viewport owner does when its
 * content, after layout, does not fit (C++ OverflowAnswer). SCROLL keeps the
 * content's size and is carried out only by an owner that scrolls; CONTAIN
 * scales the content uniformly by the smaller axis ratio and aligns it;
 * COVER scales by the larger and crops the excess; CLIP keeps the size and
 * cuts it. */
typedef uint32_t gf_overflow_answer;
#define GF_OVERFLOW_ANSWER_SCROLL 0u
#define GF_OVERFLOW_ANSWER_CONTAIN 1u
#define GF_OVERFLOW_ANSWER_COVER 2u
#define GF_OVERFLOW_ANSWER_CLIP 3u

/* gf_overflow_cause (since 1.7): why content overflowed (C++ OverflowCause):
 * a layout container's children did not fit after reflow; the design size
 * exceeds the viewport; a child placed by position, dock or anchor lies
 * outside; a window's minimum size exceeds its monitor's work area. */
typedef uint32_t gf_overflow_cause;
#define GF_OVERFLOW_CAUSE_MINIMUM_CONTENT_EXCEEDS_VIEWPORT 0u
#define GF_OVERFLOW_CAUSE_DESIGN_SIZE_EXCEEDS_VIEWPORT 1u
#define GF_OVERFLOW_CAUSE_PLACED_CHILD_OUTSIDE_VIEWPORT 2u
#define GF_OVERFLOW_CAUSE_MINIMUM_SIZE_EXCEEDS_WORK_AREA 3u

/* gf_content_alignment (since 1.7): where content sits in leftover space
 * (C++ ContentAlignment, Windows Forms' ContentAlignment). */
typedef uint32_t gf_content_alignment;
#define GF_CONTENT_ALIGNMENT_TOP_LEFT 0u
#define GF_CONTENT_ALIGNMENT_TOP_CENTER 1u
#define GF_CONTENT_ALIGNMENT_TOP_RIGHT 2u
#define GF_CONTENT_ALIGNMENT_MIDDLE_LEFT 3u
#define GF_CONTENT_ALIGNMENT_MIDDLE_CENTER 4u
#define GF_CONTENT_ALIGNMENT_MIDDLE_RIGHT 5u
#define GF_CONTENT_ALIGNMENT_BOTTOM_LEFT 6u
#define GF_CONTENT_ALIGNMENT_BOTTOM_CENTER 7u
#define GF_CONTENT_ALIGNMENT_BOTTOM_RIGHT 8u

/* A control's viewport fit (since 1.7; C++ ViewportFit): an input record
 * for set_viewport_fit and an output record for viewport_fit, whose
 * struct_size the caller sets, at least the 1.7 size. minimum_scale and
 * maximum_scale bound the effective scale of CONTAIN and COVER; a CONTAIN
 * whose ratio falls below minimum_scale takes below_minimum (SCROLL or
 * CLIP) at minimum_scale. The default depends on the kind: a plain
 * control CLIP; Windows Forms panels (Panel, UserControl, SplitterPanel)
 * CONTAIN, 0, 1, SCROLL, MIDDLE_CENTER; media (PictureBox, RasterCanvas,
 * SubSurfaceHost) CONTAIN up to 1024; CSS panels SCROLL. */
typedef struct gf_viewport_fit {
    uint32_t struct_size;
    gf_overflow_answer answer;
    double minimum_scale;
    double maximum_scale;
    gf_overflow_answer below_minimum;
    gf_content_alignment alignment;
} gf_viewport_fit;

/* One overflow a window knows of now (since 1.7; C++ OverflowRecord): an
 * output record of overflow_records. owner is the viewport owner's stable
 * id, a view into the caller's text buffer (empty when the owner left the
 * window during the call). content_extent and viewport_extent are in the
 * owner's logical units; effective_scale is the scale carried out (1 where
 * none is); answered is 0 only when the owner's answer is not carried out. */
typedef struct gf_overflow_record {
    uint32_t struct_size;
    gf_overflow_cause cause;
    gf_overflow_answer answer;
    uint32_t answered;
    gf_logical_size content_extent;
    gf_logical_size viewport_extent;
    double effective_scale;
    gf_utf8_view owner;
} gf_overflow_record;

// ic: gf_abi::windows: generated from include/gui_forms/c/gf_abi_windows.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. States: created (initial), running, ended. Observed by
// ic: gf_windows_table.lifecycle(): created GF_WINDOW_LIFECYCLE_CREATED, running GF_WINDOW_LIFECYCLE_RUNNING, ended
// ic: GF_WINDOW_LIFECYCLE_ENDED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
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
     * sizes). Once admitted, a run ends a window from create whatever its
     * result: after a refusal by the run (INVALID_ARGUMENT,
     * UNSUPPORTED_CAPABILITY) the window is disposed, so a caller makes a
     * new one to try again. Only the INVALID_STATE refusal leaves it. */
    // ic: Admitted in created; then ended. In running: returns GF_STATUS_INVALID_STATE, unchanged. In ended: returns
    // ic: GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT for a window the run
    // ic: refuses, with a message naming the reason (for a client size below the minimum, both sizes).), unchanged;
    // ic: invalid_state (GF_STATUS_INVALID_STATE while another run is under way.), unchanged; unsupported_capability
    // ic: (code: this build has no window host), unchanged; wrong_handle_kind (code: run takes a window or a root
    // ic: control), unchanged.
    gf_status (GF_CALL* run)(gf_object window_or_root, const gf_run_options* options);
    /* Asks the running window, given as the object passed to run, to close;
     * closing may cancel it. GF_STATUS_INVALID_STATE when it is not
     * running. */
    // ic: Admitted in running. In created, ended: returns GF_STATUS_INVALID_STATE, unchanged. Fails invalid_argument
    // ic: (code: the host refused the window's text), unchanged; invalid_state (GF_STATUS_INVALID_STATE when it is not
    // ic: running.), unchanged; limit_exceeded (code: too many modal windows are open), unchanged;
    // ic: unsupported_capability (code: the host cannot open this window during a run), unchanged.
    gf_status (GF_CALL* request_close)(gf_object window_or_root);
    /* Makes a window over root, a control with no parent and not in a
     * window, at the host's default size. The window holds root until it is
     * destroyed. GF_STATUS_INVALID_STATE when root has a parent or a
     * window. */
    // ic: Fails disposed (code: the root has been disposed), unchanged; invalid_argument (code: create needs a window
    // ic: output), unchanged; invalid_state (GF_STATUS_INVALID_STATE when root has a parent or a window.), unchanged.
    // ic: Returns gf_object, owned by the caller.
    gf_status (GF_CALL* create)(gf_object root, gf_object* window);
    /* The number of interactions a window raises (since 1.1). Needs no
     * window or engine, since every window raises the same ones; any thread. */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Thread: any.
    gf_status (GF_CALL* interaction_count)(uint64_t* count);
    /* Interaction index (below interaction_count) as a schema record, the
     * form schema.interaction_at gives a type's (since 1.1); any thread.
     * The views stay valid while the library is loaded.
     * GF_STATUS_INVALID_ARGUMENT past the end. */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT past the end.), unchanged. Thread: any. Returns a borrow
    // ic: of library, valid until the process ends.
    gf_status (GF_CALL* interaction_at)(uint64_t interaction, gf_schema_interaction* record);
    /* Raises the window's help request as a message box's Help button does
     * (Window::request_help, not keyboard-initiated): its help_requested handlers run, then its
     * help providers are offered it until one handles it. handled receives
     * 1 when a handler or provider handled it, else 0 (since 1.1). UI thread.
     * GF_STATUS_DISPOSED once the window is gone. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (code:
    // ic: request_help needs a handled output), unchanged; wrong_handle_kind (code: request_help takes a window),
    // ic: unchanged.
    gf_status (GF_CALL* request_help)(gf_object window, uint32_t* handled);
    /* down receives 1 when physical_key (a USB HID usage) is down in window
     * now, as the window has seen it (pressed, not released, not released by
     * the window losing activation), else 0 (since 1.2). UI thread.
     * GF_STATUS_INVALID_ARGUMENT without down; GF_STATUS_DISPOSED once the
     * window is gone. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT without down;), unchanged; wrong_handle_kind (code: key_state takes a window),
    // ic: unchanged.
    gf_status (GF_CALL* key_state)(gf_object window, uint32_t physical_key, uint32_t* down);
    /* Opens a window over root, a control with no parent and not in a
     * window, owned by options.owner, and runs a nested event loop until it
     * closes (since 1.2): the run's other windows refuse the user's input
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
    // ic: Fails cancelled (When the run ends during the loop (the running window closed), the loop ends with
    // ic: GF_STATUS_CANCELLED and the window closes with the run.), unchanged; disposed (code: the root has been
    // ic: disposed), unchanged; invalid_argument (code: the host refused the window's text), unchanged; invalid_state
    // ic: (UI thread, during a run (GF_STATUS_INVALID_STATE otherwise);), unchanged; limit_exceeded (A callback inside
    // ic: the loop may call show_modal again, up to 8 deep (GF_STATUS_LIMIT_EXCEEDED beyond).), unchanged;
    // ic: unsupported_capability (GF_STATUS_UNSUPPORTED_CAPABILITY where the host has no nested loop;), unchanged.
    gf_status (GF_CALL* show_modal)(gf_object root, const gf_owned_window_options* options,
                                    gf_dialog_result_code* result);
    /* Opens a window over root owned by options.owner, as show_modal does,
     * and returns once it is ready, without a nested loop (since 1.2). The
     * window closes through request_close, its close box, its owner
     * closing or the run ending. The same refusals as show_modal, without
     * the depth limit. */
    // ic: Fails disposed (code: the root has been disposed), unchanged; invalid_argument (code: the host refused the
    // ic: window's text), unchanged; invalid_state (code: the host refused the window (its owner or stable id)),
    // ic: unchanged; limit_exceeded (code: too many modal windows are open), unchanged; unsupported_capability (code:
    // ic: the host cannot open this window during a run), unchanged.
    gf_status (GF_CALL* show_owned)(gf_object root, const gf_owned_window_options* options);
    /* The window properties window's host applies (since 1.2): supported
     * receives the GF_WINDOW_PROPERTY_* bits of those the host honours, 0
     * before the window runs; applied the bits of the written properties
     * the host applied when each was last sent. Either output may be null.
     * UI thread. GF_STATUS_WRONG_HANDLE_KIND for an object that is not a
     * window; GF_STATUS_DISPOSED once the window is gone. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails wrong_handle_kind
    // ic: (GF_STATUS_WRONG_HANDLE_KIND for an object that is not a window;), unchanged.
    gf_status (GF_CALL* window_properties)(gf_object window, gf_window_property_flags* supported,
                                           gf_window_property_flags* applied);
    /* The focused control of a window (since 1.2): window_or_root is a
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
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (code:
    // ic: focused_control needs a control output), unchanged; invalid_state (GF_STATUS_INVALID_STATE for a control not
    // ic: in a window;), unchanged; wrong_handle_kind (code: focused_control takes a window or a control in one),
    // ic: unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* focused_control)(gf_object window_or_root, gf_object* control);
    /* The window outer band and shaping (since 1.5). Each takes window, a
     * window from create (before or during its run), or a control in a
     * window, which names that window. UI thread. GF_STATUS_WRONG_HANDLE_KIND
     * for another object; GF_STATUS_INVALID_STATE for a control not in a
     * window; GF_STATUS_DISPOSED once the window is gone. The band's state
     * is the window's, given only to whoever holds the window or one of its
     * controls; nothing a control draws can reach it.
     *
     * declare_band reserves the band (or re-declares it, laying out again
     * only when a reserved width changes). GF_STATUS_INVALID_ARGUMENT for a
     * null or short record, a width that is not finite from 0 to
     * window_band_maximum_width, an unknown full-screen choice or nonzero
     * reserved, changing nothing. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for a null or short record, a width that is not finite from 0 to
    // ic: window_band_maximum_width, an unknown full-screen choice or nonzero reserved, changing nothing.), unchanged;
    // ic: invalid_state (GF_STATUS_INVALID_STATE for a control not in a window;), unchanged; wrong_handle_kind
    // ic: (GF_STATUS_WRONG_HANDLE_KIND for another object;), unchanged.
    gf_status (GF_CALL* declare_band)(gf_object window, const gf_window_band_options* options);
    /* Removes the band and its reservation (the window is laid out again);
     * nothing for a window without one. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state (code: the
    // ic: control is not in a window), unchanged; wrong_handle_kind (code: the band takes a window or a control in
    // ic: one), unchanged.
    gf_status (GF_CALL* undeclare_band)(gf_object window);
    /* What the band shows: its strips repaint, nothing is laid out. Kept
     * while no band is declared. GF_STATUS_INVALID_ARGUMENT, changing
     * nothing, for a null or short record, an unknown state, role or
     * texture, a frequency that is not finite or below 0, a texture out of
     * range, or nonzero reserved. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT, changing nothing, for a null or short record, an unknown state, role or texture,
    // ic: a frequency that is not finite or below 0, a texture out of range, or nonzero reserved.), unchanged;
    // ic: invalid_state (code: the control is not in a window), unchanged; wrong_handle_kind (code: the band takes a
    // ic: window or a control in one), unchanged.
    gf_status (GF_CALL* set_band_appearance)(gf_object window, const gf_window_band_appearance* appearance);
    /* The band as it stands. GF_STATUS_INVALID_ARGUMENT for a null or short
     * record. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for a null or short record.), unchanged; invalid_state (code: the control is not
    // ic: in a window), unchanged; wrong_handle_kind (code: the band takes a window or a control in one), unchanged.
    gf_status (GF_CALL* band_report)(gf_object window, gf_window_band_report* report);
    /* The corners the window asks its host for, sent at once while a host
     * shows it and when one does. GF_STATUS_INVALID_ARGUMENT for a value
     * outside gf_window_corner_preference, changing nothing. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for a value outside gf_window_corner_preference, changing nothing.), unchanged;
    // ic: invalid_state (code: the control is not in a window), unchanged; wrong_handle_kind (code: the band takes a
    // ic: window or a control in one), unchanged.
    gf_status (GF_CALL* set_corner_preference)(gf_object window, gf_window_corner_preference preference);
    /* The content rectangle in client coordinates: the client area less the
     * reserved band. GF_STATUS_INVALID_ARGUMENT for a null output. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for a null output.), unchanged; invalid_state (code: the control is not in a
    // ic: window), unchanged; wrong_handle_kind (code: the band takes a window or a control in one), unchanged.
    gf_status (GF_CALL* content_rect)(gf_object window, gf_logical_rect* rect);
    /* What the window's host lets its shaping rely on, and its corners now.
     * GF_STATUS_INVALID_ARGUMENT for a null or short record. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for a null or short record.), unchanged; invalid_state (code: the control is not
    // ic: in a window), unchanged; wrong_handle_kind (code: the band takes a window or a control in one), unchanged.
    gf_status (GF_CALL* shaping)(gf_object window, gf_window_shaping* report);
    /* Where the pointer is, as a window last saw it (since 1.5): the
     * position of the last pointer event the window dispatched, which
     * during a callback is the event being dispatched. mouse_enter and
     * mouse_leave carry no fields (Windows Forms' EventArgs), so their
     * handlers read the crossing here: during the move that crosses, the
     * old control's mouse_leave and then the new one's mouse_enter, both
     * at that move's position, before the move's own route.
     * window_or_control is a window from create or a control in a window.
     * location receives the position in the control's coordinates (the
     * window's for a window), window_position in the window's, and inside
     * 1 while the pointer is in the window, else 0; all are zero before
     * the window has seen a pointer event. Any output may be null. UI
     * thread. GF_STATUS_WRONG_HANDLE_KIND for an object that is neither;
     * GF_STATUS_INVALID_STATE for a control not in a window;
     * GF_STATUS_DISPOSED for a window that has been disposed or whose run
     * has ended. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state
    // ic: (GF_STATUS_INVALID_STATE for a control not in a window;), unchanged; wrong_handle_kind
    // ic: (GF_STATUS_WRONG_HANDLE_KIND for an object that is neither;), unchanged.
    gf_status (GF_CALL* pointer_location)(gf_object window_or_control, gf_logical_point* location,
                                          gf_logical_point* window_position, uint32_t* inside);
    /* Since 1.5. Shows the program's business card in the hand over a
     * window (show_business_card): window_or_root is a window from create,
     * or a control in a window, as for focused_control. shown receives 1
     * when the card came out, 0 when the window already showed it; it may
     * be null. The card is put away by Escape, its close mark or a press
     * beside it. UI thread. GF_STATUS_INVALID_STATE for a control not in a
     * window or in a sub-surface; GF_STATUS_DISPOSED for a window that has
     * been disposed or whose run has ended. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state
    // ic: (GF_STATUS_INVALID_STATE for a control not in a window or in a sub-surface;), unchanged; wrong_handle_kind
    // ic: (code: show_business_card takes a window or a control in one), unchanged.
    gf_status (GF_CALL* show_business_card)(gf_object window_or_root, uint32_t* shown);
    /* Since 1.6: the multiple document interface (above). Whether the
     * window is an MDI container (Form.IsMdiContainer): its document area
     * shows its documents as frames rather than tabs. container is 0 or 1. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (code:
    // ic: a switch is 0 or 1), unchanged; invalid_state (code: the control is not in a window), unchanged;
    // ic: wrong_handle_kind (code: the MDI calls take a window or a control in one), unchanged.
    gf_status (GF_CALL* set_mdi_container)(gf_object window_or_root, uint32_t container);
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(container),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: mdi_container_container,
    // ic: then the state refusal. Fails invalid_state (code: the control is not in a window), unchanged;
    // ic: wrong_handle_kind (code: the MDI calls take a window or a control in one), unchanged.
    gf_status (GF_CALL* mdi_container)(gf_object window_or_root, uint32_t* container);
    /* Since 1.6. The number of MDI children (Form.MdiChildren), and the id
     * of the child at index, in tree and tab order, into the caller's
     * buffer (required receives its size, also with
     * GF_STATUS_BUFFER_TOO_SMALL; GF_STATUS_NOT_FOUND past the end). */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(count), else
    // ic: returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: mdi_child_count_count, then the
    // ic: state refusal. Fails invalid_state (code: the control is not in a window), unchanged; wrong_handle_kind
    // ic: (code: the MDI calls take a window or a control in one), unchanged.
    gf_status (GF_CALL* mdi_child_count)(gf_object window_or_root, uint64_t* count);
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails buffer_too_small (the id
    // ic: is longer than capacity: required receives its size, nothing else is written), unchanged; invalid_state
    // ic: (code: the control is not in a window), unchanged; not_found (code: the index is past the window's MDI
    // ic: children), unchanged; wrong_handle_kind (code: the MDI calls take a window or a control in one), unchanged.
    gf_status (GF_CALL* mdi_child_at)(gf_object window_or_root, uint64_t index, char* buffer, uint64_t capacity,
                                      uint64_t* required);
    /* Since 1.6. The active child's id (Form.ActiveMdiChild), as
     * mdi_child_at writes one; empty (required zero) when there is none. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state (code: the
    // ic: control is not in a window), unchanged; wrong_handle_kind (code: the MDI calls take a window or a control in
    // ic: one), unchanged.
    gf_status (GF_CALL* active_mdi_child)(gf_object window_or_root, char* buffer, uint64_t capacity,
                                          uint64_t* required);
    /* Since 1.6. Activates a child (Form.ActivateMdiChild): focus moves
     * into it. activated (may be null) receives 1, or 0 for a child the
     * area does not hold. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state (code: the
    // ic: control is not in a window), unchanged; wrong_handle_kind (code: the MDI calls take a window or a control in
    // ic: one), unchanged.
    gf_status (GF_CALL* activate_mdi_child)(gf_object window_or_root, gf_utf8_view child, uint32_t* activated);
    /* Since 1.6. Arranges the children's frames (Form.LayoutMdi,
     * gf_mdi_layout). arranged (may be null) receives 0 when there was
     * nothing to arrange. A value outside gf_mdi_layout gives
     * GF_STATUS_INVALID_ARGUMENT. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (A
    // ic: value outside gf_mdi_layout gives GF_STATUS_INVALID_ARGUMENT.), unchanged; invalid_state (code: the control
    // ic: is not in a window), unchanged; wrong_handle_kind (code: the MDI calls take a window or a control in one),
    // ic: unchanged.
    gf_status (GF_CALL* layout_mdi)(gf_object window_or_root, gf_mdi_layout layout, uint32_t* arranged);
    /* Since 1.6. Whether minimized children line the bottom of the area
     * (1, the default) or its top (0)
     * (Form.MdiChildrenMinimizedAnchorBottom). */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (code:
    // ic: a switch is 0 or 1), unchanged; invalid_state (code: the control is not in a window), unchanged;
    // ic: wrong_handle_kind (code: the MDI calls take a window or a control in one), unchanged.
    gf_status (GF_CALL* set_mdi_children_minimized_anchor_bottom)(gf_object window_or_root, uint32_t bottom);
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(bottom),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: mdi_children_minimized_anchor_bottom_bottom, then the state refusal. Fails invalid_state (code: the control
    // ic: is not in a window), unchanged; wrong_handle_kind (code: the MDI calls take a window or a control in one),
    // ic: unchanged.
    gf_status (GF_CALL* mdi_children_minimized_anchor_bottom)(gf_object window_or_root, uint32_t* bottom);
    /* Since 1.6. The window's main menu strip (Form.MainMenuStrip): the
     * strip the active child's menu strip merges into (the docking table's
     * set_panel_menu_strip), and whose end shows a maximized child's
     * caption buttons. A menu_strip object or zero for none; another
     * object gives GF_STATUS_WRONG_HANDLE_KIND. The window holds it
     * weakly: the caller's reference keeps it. main_menu_strip gives a new
     * reference the caller releases, or zero when none is set or it is
     * gone. */
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state (code: the
    // ic: control is not in a window), unchanged; wrong_handle_kind (A menu_strip object or zero for none; another
    // ic: object gives GF_STATUS_WRONG_HANDLE_KIND.), unchanged.
    gf_status (GF_CALL* set_main_menu_strip)(gf_object window_or_root, gf_object strip);
    // ic: Admitted in created, running. In ended: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(strip), else
    // ic: returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: main_menu_strip_strip, then the
    // ic: state refusal. Fails invalid_state (code: the control is not in a window), unchanged; wrong_handle_kind
    // ic: (code: the MDI calls take a window or a control in one), unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* main_menu_strip)(gf_object window_or_root, gf_object* strip);
    /* Since 1.7. The overflows the window knows of now, as of its last
     * layout (stage VA), in the order they appeared: records is an array of
     * capacity records (the array rule: each repeats the first's
     * struct_size, at least the 1.7 size), text_buffer receives the owners'
     * stable ids one after another and each record's owner points there.
     * required receives the record count and text_required the text size,
     * also with GF_STATUS_BUFFER_TOO_SMALL, which writes no record. records
     * and text_buffer may be null when their capacity is zero; required
     * and text_required may not. */
    gf_status (GF_CALL* overflow_records)(gf_object window_or_root, gf_overflow_record* records, uint64_t capacity,
                                          uint64_t* required, char* text_buffer, uint64_t text_capacity,
                                          uint64_t* text_required);
    /* Since 1.7. The number of the window's overflows whose answer is not
     * carried out: zero for a window whose every overflow is answered. */
    gf_status (GF_CALL* unanswered_overflow_count)(gf_object window_or_root, uint64_t* count);
    /* Since 1.7. The edges of the window's client area the host or the
     * device hides, in logical units, zero until the host reports any. The
     * root slot keeps clear of them. */
    gf_status (GF_CALL* safe_insets)(gf_object window_or_root, gf_logical_insets* insets);
    /* Since 1.7. lifecycle receives where window, a window from create,
     * stands (gf_window_lifecycle), at any point of its life, disposed
     * included. GF_STATUS_WRONG_HANDLE_KIND for an object that is not a
     * window (a run's root control among them); GF_STATUS_INVALID_ARGUMENT
     * without lifecycle. UI thread. */
    // ic: Requires not_null(lifecycle), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails wrong_handle_kind
    // ic: (GF_STATUS_WRONG_HANDLE_KIND for an object that is not a window (a run's root control among them);),
    // ic: unchanged.
    gf_status (GF_CALL* lifecycle)(gf_object window, gf_window_lifecycle* lifecycle);
} gf_windows_table;

/* gf_schema_kind: what a schema type is. A control is created and placed in
 * a tree; a component is owned by a control or a window; a service is
 * reached through another object and lists nothing. A defined control
 * (since 1.5) is a control made only from a definition, never by its id:
 * objects.create refuses it, and the custom controls table makes one
 * (load_composite, create_composite). It lists nothing itself, as each
 * definition declares its own parts, properties and events. */
typedef uint32_t gf_schema_kind;
#define GF_SCHEMA_KIND_CONTROL 0u
#define GF_SCHEMA_KIND_COMPONENT 1u
#define GF_SCHEMA_KIND_SERVICE 2u
#define GF_SCHEMA_KIND_DEFINED_CONTROL 3u

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
 * valid and unchanged until the process ends, except the composite_*
 * slots', valid while the load they describe is held (gf_schema_composite);
 * these are the one exception to the rule that no library storage is lent.
 * An empty composed_from or enumeration means none.
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
 * Since 1.5, provisional (numbered 1.5 while 1.5 is unreleased, else 1.6).
 * A composite definition the process has loaded (GF_TABLE_CUSTOM_CONTROLS
 * load_composite), as the schema table lists it: composite is the identity
 * of that load, which the composite_* slots take; id is the registered
 * (snake case) name, which objects.type_id gives for a control made from
 * it; name is the type's name as the document writes it; version is
 * "major.minor"; slot is the slot's local ID, empty without one. The counts
 * are of its anatomy (parts, states, custom states), its declared
 * properties (the values it registers: neither presentational nor
 * command properties), its events and its commands.
 *
 * Release rules: a load is listed from load_composite until the caller's
 * hold on the definition ends, by releasing its last reference or by
 * disposing the object; the controls made from it keep working, but are no
 * longer listed. Every gf_utf8_view a composite_* slot writes lends the
 * definition's storage, valid until that hold ends (not until the process
 * ends, as the rest of the schema's text is). An identity is never reused:
 * once its load is released, every composite_* slot given it fails with
 * GF_STATUS_REMOVED and writes nothing, and an identity no load had gives
 * GF_STATUS_INVALID_ARGUMENT. UI thread only.
 */
typedef struct gf_schema_composite {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t composite;
    gf_utf8_view id;
    gf_utf8_view name;
    gf_utf8_view version;
    gf_utf8_view slot;
    uint64_t part_count;
    uint64_t state_count;
    uint64_t custom_state_count;
    uint64_t property_count;
    uint64_t event_count;
    uint64_t command_count;
} gf_schema_composite;

/*
 * GF_TABLE_SCHEMA: the control schema (stage T1) and the engine it themes,
 * as manifests/control-schema.json records them. Types are numbered from
 * zero in the registry's order; every other list from zero in its own. An
 * index past the end gives GF_STATUS_INVALID_ARGUMENT. The first call builds
 * the schema from the library's types; it is the same in every later call.
 */
// ic: gf_abi::schema: generated from include/gui_forms/c/gf_abi_schema.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. Thread: ui, unless a call says otherwise.
typedef struct gf_schema_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    // ic: Fails invalid_argument (code: vocabulary_count needs a known vocabulary and an output), unchanged.
    gf_status (GF_CALL* vocabulary_count)(gf_schema_vocabulary vocabulary, uint64_t* count);
    // ic: Fails invalid_argument (code: vocabulary_at needs a known vocabulary and an output), unchanged.
    gf_status (GF_CALL* vocabulary_at)(gf_schema_vocabulary vocabulary, uint64_t index,
                                       gf_utf8_view* name);
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* type_count)(uint64_t* count);
    /* The index of a type named in any spelling of its id. */
    // ic: Requires not_null(type), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument (code:
    // ic: find_type needs an output), unchanged; not_found (code: the schema has no type of that name), unchanged.
    gf_status (GF_CALL* find_type)(gf_utf8_view name, uint64_t* type);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* type_at)(uint64_t type, gf_schema_type* record);
    // ic: Fails invalid_argument (code: item_system_at needs an item system index), unchanged.
    gf_status (GF_CALL* item_system_at)(uint64_t type, uint64_t system,
                                        gf_schema_item_system* record);
    /* A part of the type (system GF_SCHEMA_NONE) or of one item system. */
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* part_at)(uint64_t type, uint64_t system, uint64_t part,
                                 gf_schema_part* record);
    /* A state of the type or item system, or (part not GF_SCHEMA_NONE) of
     * one of its parts. */
    // ic: Requires not_null(name), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* state_at)(uint64_t type, uint64_t system, uint64_t part, uint64_t state,
                                  gf_utf8_view* name);
    // ic: Requires not_null(name), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* custom_state_at)(uint64_t type, uint64_t system, uint64_t state,
                                         gf_utf8_view* name);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* property_at)(uint64_t type, uint64_t property, gf_schema_property* record);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* choice_at)(uint64_t type, uint64_t property, uint64_t choice,
                                   gf_schema_choice* record);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* interaction_at)(uint64_t type, uint64_t interaction,
                                        gf_schema_interaction* record);
    gf_status (GF_CALL* engine)(gf_schema_engine* record);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* primitive_at)(uint64_t primitive, gf_schema_primitive* record);
    // ic: Fails invalid_argument (code: primitive_name_at needs a known list and an output), unchanged.
    gf_status (GF_CALL* primitive_name_at)(uint64_t primitive, gf_schema_primitive_list list,
                                           uint64_t index, gf_utf8_view* name);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* parameter_at)(uint64_t primitive, uint64_t parameter,
                                      gf_schema_parameter* record);
    // ic: Requires not_null(name), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* parameter_choice_at)(uint64_t primitive, uint64_t parameter,
                                             uint64_t choice, gf_utf8_view* name);
    /* The number of effect programs the library registers (since 1.1). */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* effect_count)(uint64_t* count);
    /* Effect index (below effect_count), in registration order (since
     * 1.1). GF_STATUS_INVALID_ARGUMENT past the end. */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT past the end.), unchanged.
    gf_status (GF_CALL* effect_at)(uint64_t effect, gf_schema_effect* record);
    /* The properties a type provides to each of its children, such as a
     * grid panel's cell (since 1.1): how many, one of them (below the
     * count), and one choice of it, as property_at and choice_at give the
     * type's own. GF_STATUS_INVALID_ARGUMENT past the end. */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT past the end.), unchanged.
    gf_status (GF_CALL* child_property_count)(uint64_t type, uint64_t* count);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* child_property_at)(uint64_t type, uint64_t property, gf_schema_property* record);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* child_choice_at)(uint64_t type, uint64_t property, uint64_t choice,
                                         gf_schema_choice* record);
    /*
     * The slots below are provisional additions (numbered 1.5 while 1.5 is
     * unreleased, else 1.6); a table whose struct_size ends before one does
     * not have it. They list the composite definitions loaded now (see
     * gf_schema_composite for the release rules), the C counterpart of
     * manifests/control-schema.json's composites.
     */
    /* How many loads are listed now, and one of them (index below the
     * count), in load order. Indices move as loads are released; the
     * record's composite does not. */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* composite_count)(uint64_t* count);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged.
    gf_status (GF_CALL* composite_at)(uint64_t index, gf_schema_composite* record);
    /* A part of the composite's anatomy: region, focus, then the declared
     * parts in document order. */
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged; removed (code: the
    // ic: composite's load was released), unchanged.
    gf_status (GF_CALL* composite_part_at)(uint64_t composite, uint64_t part, gf_schema_part* record);
    /* A state of the composite, or (part not GF_SCHEMA_NONE) of one of its
     * parts. */
    // ic: Requires not_null(name), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails removed (code: the
    // ic: composite's load was released), unchanged.
    gf_status (GF_CALL* composite_state_at)(uint64_t composite, uint64_t part, uint64_t state,
                                            gf_utf8_view* name);
    // ic: Requires not_null(name), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails removed (code: the
    // ic: composite's load was released), unchanged.
    gf_status (GF_CALL* composite_custom_state_at)(uint64_t composite, uint64_t state, gf_utf8_view* name);
    /* A declared property, as property_at gives a type's, and one choice
     * of it. */
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged; removed (code: the
    // ic: composite's load was released), unchanged.
    gf_status (GF_CALL* composite_property_at)(uint64_t composite, uint64_t property,
                                               gf_schema_property* record);
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged; removed (code: the
    // ic: composite's load was released), unchanged.
    gf_status (GF_CALL* composite_choice_at)(uint64_t composite, uint64_t property, uint64_t choice,
                                             gf_schema_choice* record);
    /* A declared event: its name as the document writes it, and its payload
     * in the interaction vocabulary (gf_schema_interaction), empty for
     * none; mutable_payload is 0. */
    // ic: Fails invalid_argument (code: the index is past the end of its list), unchanged; removed (code: the
    // ic: composite's load was released), unchanged.
    gf_status (GF_CALL* composite_event_at)(uint64_t composite, uint64_t event,
                                            gf_schema_interaction* record);
    /* A declared command's name, as perform_composite takes it. */
    // ic: Requires not_null(name), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails removed (code: the
    // ic: composite's load was released), unchanged.
    gf_status (GF_CALL* composite_command_at)(uint64_t composite, uint64_t command, gf_utf8_view* name);
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

/* gf_user_theme_policy: how a program admits user themes and the theme it
 * is started with (--theme, GUI_FORMS_THEME_SHEET), as C++
 * UserThemePolicy. */
typedef uint32_t gf_user_theme_policy;
#define GF_USER_THEME_POLICY_LAYERED 0u
#define GF_USER_THEME_POLICY_REPLACES_PROGRAM_SHEET 1u
#define GF_USER_THEME_POLICY_REFUSED 2u

/* gf_theme_request_outcome: what the theme the process was started with
 * did, as C++ ThemeRequestOutcome; PENDING until the first window settles
 * it. */
typedef uint32_t gf_theme_request_outcome;
#define GF_THEME_REQUEST_NONE 0u
#define GF_THEME_REQUEST_PENDING 1u
#define GF_THEME_REQUEST_BUILT_IN 2u
#define GF_THEME_REQUEST_APPLIED 3u
#define GF_THEME_REQUEST_IGNORED_BY_POLICY 4u
#define GF_THEME_REQUEST_UNKNOWN_NAME 5u
#define GF_THEME_REQUEST_REFUSED 6u
#define GF_THEME_REQUEST_UNREADABLE 7u
#define GF_THEME_REQUEST_MALFORMED 8u

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
// ic: gf_abi::themes: generated from include/gui_forms/c/gf_abi_themes.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. Thread: ui, unless a call says otherwise.
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
    // ic: Fails invalid_argument (code: a theme package needs a record of at least the 1.0 size with reserved zero),
    // ic: unchanged; invalid_state (GF_STATUS_INVALID_STATE when a control's inline style does not compile against the
    // ic: new theme: nothing changes then either.), unchanged; limit_exceeded (code: a theme package holds at most 256
    // ic: files), unchanged.
    gf_status (GF_CALL* load)(const gf_theme_package* package, gf_theme_report* report);
    /* Makes the built-in theme the theme again and clears the findings. */
    gf_status (GF_CALL* reset)(void);
    /* Verifies package whole and installs it for @import, replacing an
     * installed package of its name. A refused package changes nothing;
     * either way its findings replace the held ones. At most 256 packages
     * are installed. */
    // ic: Fails invalid_argument (code: a theme package needs a record of at least the 1.0 size with reserved zero),
    // ic: unchanged; limit_exceeded (code: a theme package holds at most 256 files), unchanged.
    gf_status (GF_CALL* install)(const gf_theme_package* package, gf_theme_report* report);
    /* Removes the installed package named name; removed is 1 when there
     * was one, else 0. The theme in use is unchanged. */
    // ic: Requires not_null(removed), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* remove)(gf_utf8_view name, uint32_t* removed);
    /* A finding of the last load or install, in the compiler's order. */
    // ic: Fails invalid_argument (code: no finding has that index), unchanged.
    gf_status (GF_CALL* diagnostic_at)(uint64_t index, gf_theme_diagnostic* record);
    // ic: Fails invalid_argument (code: unknown diagnostic text), unchanged.
    gf_status (GF_CALL* diagnostic_text)(uint64_t index, gf_theme_diagnostic_text text, char* buffer,
                                         uint64_t capacity, uint64_t* required);
    /* The id of the theme in use (a package's name). */
    gf_status (GF_CALL* theme_id)(char* buffer, uint64_t capacity, uint64_t* required);
    /* Publishes a named value for themes to read (var(--name)). name is 1
     * to 64 lowercase ASCII letters, digits, hyphens and underscores,
     * starting with a letter, and value is finite, else
     * GF_STATUS_INVALID_ARGUMENT. Every open window (the running one and,
     * since 1.2, each owned one) repaints the parts that read it. A name once published stays admitted to later loads,
     * withdrawn or not; at most GF_THEME_MAXIMUM_PUBLISHED_VALUES names
     * (GF_STATUS_LIMIT_EXCEEDED). */
    // ic: Fails invalid_argument (Publishes a named value for themes to read (var(--name)). name is 1 to 64 lowercase
    // ic: ASCII letters, digits, hyphens and underscores, starting with a letter, and value is finite, else
    // ic: GF_STATUS_INVALID_ARGUMENT.), unchanged; limit_exceeded (A name once published stays admitted to later loads,
    // ic: withdrawn or not; at most GF_THEME_MAXIMUM_PUBLISHED_VALUES names (GF_STATUS_LIMIT_EXCEEDED).), unchanged.
    gf_status (GF_CALL* publish_value)(gf_utf8_view name, double value);
    /* Withdraws the value of name; it then reads as not published, and the
     * reading expression gives its fallback. Withdrawing a name never
     * published changes nothing. */
    // ic: Fails invalid_argument (code: a published name is 1 to 64 lowercase letters, digits, hyphens and underscores,
    // ic: ), unchanged.
    gf_status (GF_CALL* withdraw_value)(gf_utf8_view name);
    /* present is 1 and value the value when name is published; else
     * present is 0 and value is left unchanged. */
    // ic: Fails invalid_argument (code: value and present are outputs), unchanged.
    gf_status (GF_CALL* published_value)(gf_utf8_view name, double* value, uint32_t* present);
    /* Admits an image file (PNG is the only format) of size bytes into the
     * image registry and writes its id, which IMAGE values name. The bytes
     * are read during the call only. A file that is not a valid PNG gives
     * GF_STATUS_INVALID_ARGUMENT, one past the registry's limits
     * GF_STATUS_LIMIT_EXCEEDED; nothing is admitted then. */
    // ic: Requires not_null(image) and (not_null(bytes) or size == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails invalid_argument (A file that is not a valid PNG gives GF_STATUS_INVALID_ARGUMENT, one past
    // ic: the registry's limits GF_STATUS_LIMIT_EXCEEDED; nothing is admitted then.), unchanged; limit_exceeded (A file
    // ic: that is not a valid PNG gives GF_STATUS_INVALID_ARGUMENT, one past the registry's limits
    // ic: GF_STATUS_LIMIT_EXCEEDED; nothing is admitted then.), unchanged.
    gf_status (GF_CALL* load_image)(const uint8_t* bytes, uint64_t size, uint64_t* image);
    /* Removes an image; controls showing it show none and repaint.
     * GF_STATUS_NOT_FOUND for an id the registry does not hold. */
    // ic: Fails not_found (GF_STATUS_NOT_FOUND for an id the registry does not hold.), unchanged.
    gf_status (GF_CALL* remove_image)(uint64_t image);
    /*
     * Copies width x height pixels of 32-bit premultiplied BGRA (row_bytes
     * apart, at least width * 4) into the image registry, as load_image does
     * a PNG; image receives the new id. pixels may be null only for an empty
     * image. Fails with GF_STATUS_LIMIT_EXCEEDED past the registry's limits,
     * else GF_STATUS_INVALID_ARGUMENT, leaving image unwritten.
     */
    // ic: Fails invalid_argument (Fails with GF_STATUS_LIMIT_EXCEEDED past the registry's limits, else
    // ic: GF_STATUS_INVALID_ARGUMENT, leaving image unwritten.), unchanged; limit_exceeded (Fails with
    // ic: GF_STATUS_LIMIT_EXCEEDED past the registry's limits, else GF_STATUS_INVALID_ARGUMENT, leaving image
    // ic: unwritten.), unchanged.
    gf_status (GF_CALL* load_pixels)(const uint8_t* pixels, uint32_t width, uint32_t height, uint64_t row_bytes,
                                     uint64_t* image);
    /* Loads sheet (since 1.2), the UTF-8 text of one CSS theme sheet (at
     * most GF_THEME_PACKAGE_MAXIMUM_BYTES), as load does a package: the package
     * of that one sheet named name, its manifest made by the library (the
     * sheet's sha256 included). An application's theme kept as a plain
     * file, edited and read again at start-up without a rebuild. A refused
     * sheet changes nothing; its findings name the file sheet.theme.css
     * with their line and column. GF_STATUS_INVALID_ARGUMENT for a view
     * with a size and no data, GF_STATUS_LIMIT_EXCEEDED past the bound. */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT for a view with a size and no data,
    // ic: GF_STATUS_LIMIT_EXCEEDED past the bound.), unchanged; limit_exceeded (GF_STATUS_INVALID_ARGUMENT for a view
    // ic: with a size and no data, GF_STATUS_LIMIT_EXCEEDED past the bound.), unchanged.
    gf_status (GF_CALL* load_sheet)(gf_utf8_view sheet, gf_utf8_view name, gf_theme_report* report);
    /* Chooses how the library's windows admit user themes and the theme the
     * process was started with (--theme <name|built-in|path.css>, or
     * GUI_FORMS_THEME_SHEET), for the resources they will share: the
     * current application's, or the library's own. Resources made later
     * (a new application) start LAYERED. REFUSED ignores the request with a
     * warning. GF_STATUS_INVALID_STATE once a window shares the resources;
     * GF_STATUS_INVALID_ARGUMENT for an unknown policy. */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT for an unknown policy.), unchanged; invalid_state
    // ic: (GF_STATUS_INVALID_STATE once a window shares the resources;), unchanged.
    gf_status (GF_CALL* set_user_theme_policy)(gf_user_theme_policy policy);
    /* What the theme the process was started with did on the resources the
     * library's windows share: NONE without one, PENDING until the first
     * window settles it, then its outcome; a request that is not APPLIED
     * or BUILT_IN changed nothing and wrote its findings to stderr. */
    // ic: Requires not_null(outcome), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* theme_request)(gf_theme_request_outcome* outcome);
} gf_themes_table;

/* gf_host_capability: what a host can do, one bit each; a host reports the
 * set it implements. A later minor may add bits to this word until bit 55
 * is taken (K7); the bits from 56 to 63 stay free in 1.x. When they run
 * short, a second 64-bit word, available_2, follows at the tail of
 * gf_host_capabilities (the record is size-prefixed, so the word is an
 * addition) and later bits go there. A consumer reads a capability's bit
 * from the word its constant's comment names; every constant below is in
 * available. */
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
 * are square to the band. Windows 11 (DWM) shapes and reports them; macOS
 * only reports them, square in full screen and for a borderless window and
 * unknown otherwise (since 1.5; its window_corner_shapes is SQUARE). */
#define GF_HOST_CAPABILITY_WINDOW_CORNERS 17179869184ull
/* Since 1.1: the host reports the native window's state: full screen, and
 * the maximize, snap and restore changes that can reshape its corners. A
 * window outer band declared to hide in full screen hides only where the
 * host reports it. Windows, macOS and X11. */
#define GF_HOST_CAPABILITY_WINDOW_STATE_REPORTS 34359738368ull
/* Since 1.4: hosted content, a program in a process of its own on a content
 * surface (GF_TABLE_CONTENT start_hosted). macOS, Linux and Windows. */
#define GF_HOST_CAPABILITY_HOSTED_CONTENT 68719476736ull
/* Since 1.5: relative pointer mode (gf_controls_table
 * request_relative_pointer). RELATIVE_POINTER: the host delivers motion
 * deltas itself (without it the window derives them from positions).
 * RAW_POINTER_MOTION: the deltas can be unaccelerated. POINTER_CONFINEMENT:
 * the host keeps the pointer inside a rectangle and can hold it still
 * (locked). POINTER_WARP: the host can move the pointer. macOS and X11. */
#define GF_HOST_CAPABILITY_RELATIVE_POINTER 137438953472ull
#define GF_HOST_CAPABILITY_RAW_POINTER_MOTION 274877906944ull
#define GF_HOST_CAPABILITY_POINTER_CONFINEMENT 549755813888ull
#define GF_HOST_CAPABILITY_POINTER_WARP 1099511627776ull
/* Since 1.5: the host opens a web or mail address (http, https or mailto)
 * in the user's own browser or mail program; the business card's website
 * opens through it. Windows, macOS and X11. */
#define GF_HOST_CAPABILITY_OPEN_URL 2199023255552ull
/* Since 1.5: the presentation shim, reserved before the seal.
 * DIRECT_BUFFERS: frames are written with CPU pixels into buffers the
 * window system gives (possibly GPU-backed, treated as a blind interface),
 * falling back to the host's copy on any refusal or loss. HOST_SCROLL: a
 * scroll moves the pixels already presented and only the exposed band is
 * copied. REFERENCE_PRESENTATION: the host is GUI.Forms' own window
 * manager, the reference behaviour for the others. Each is reported only
 * where the host implements it and its presentation policy selects it. */
#define GF_HOST_CAPABILITY_DIRECT_BUFFERS 8796093022208ull
#define GF_HOST_CAPABILITY_HOST_SCROLL 17592186044416ull
#define GF_HOST_CAPABILITY_REFERENCE_PRESENTATION 35184372088832ull
/* Since 1.7: the host shows desktop notifications through the platform's
 * notification service, outside the application's windows (C++
 * HostServices::show_desktop_notification and ToastNotification):
 * UNUserNotificationCenter in a macOS application bundle, toasts on
 * Windows for a packaged process or one with an explicit AppUserModelID,
 * and the freedesktop notification service where the session bus has one.
 * It depends on how the process runs, not only on the host, so the same
 * host may report it or not; a high-integrity build never does. */
#define GF_HOST_CAPABILITY_DESKTOP_NOTIFICATIONS 562949953421312ull
/* Bits 46 to 48 (70368744177664, 140737488355328 and 281474976710656)
 * are reserved: no host reports them and no constant will take them.
 * GUI.Forms has no GPU surfaces (owner, 2026-11-16), and the GPU-backed
 * buffer routes that held them before the seal were withdrawn. */
/* Since 1.7 (stage VA): the host reports the client area's safe insets
 * (windows.safe_insets reads them). */
#define GF_HOST_CAPABILITY_SAFE_INSETS 1125899906842624ull

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
#define GF_POINTER_CURSOR_HELP 10u

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
 * dialog; a flag the kind does not read is refused. The file checks
 * (Windows Forms' CheckFileExists, CheckPathExists and CreatePrompt) are
 * spelled so that no flag keeps each kind's default: an open-file dialog
 * accepts only an existing file in an existing folder unless
 * allow_missing_file or allow_missing_folder is given; a save-file dialog
 * requires an existing folder unless allow_missing_folder is given, and
 * requires an existing file or asks before a new one only with
 * require_existing_file or create_prompt. */
typedef uint32_t gf_dialog_flags;
#define GF_DIALOG_FLAGS_NONE 0u
#define GF_DIALOG_FLAGS_ALLOW_MULTIPLE 1u
#define GF_DIALOG_FLAGS_CONFIRM_OVERWRITE 2u
#define GF_DIALOG_FLAGS_ALLOW_ALPHA 4u
#define GF_DIALOG_FLAGS_ALLOW_MISSING_FILE 8u
#define GF_DIALOG_FLAGS_REQUIRE_EXISTING_FILE 16u
#define GF_DIALOG_FLAGS_ALLOW_MISSING_FOLDER 32u
#define GF_DIALOG_FLAGS_CREATE_PROMPT 64u

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
    /* Since 1.2: the root of the open window the dialog belongs to (the
     * run's root, or an owned window's from show_modal or show_owned), or
     * zero for the run's window, which a 1.0-sized record means. Any other
     * object gives GF_STATUS_INVALID_STATE. */
    gf_object owner;
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
// ic: gf_abi::host: generated from include/gui_forms/c/gf_abi_host.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. Thread: ui, unless a call says otherwise.
typedef struct gf_host_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    // ic: Requires not_null(record), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires record_size >=
    // ic: host_capabilities_size_1_0, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: capabilities_record, then capabilities_record_size.
    gf_status (GF_CALL* capabilities)(gf_host_capabilities* record);
    /* The host's name, as win32-dib, appkit-macos or linux-x11. */
    gf_status (GF_CALL* platform_name)(char* buffer, uint64_t capacity, uint64_t* required);
    /* Shows cursor until the pointer next moves over a control that sets
     * its own. */
    // ic: Fails invalid_argument (code: unknown cursor), unchanged; invalid_state (code: the host has shut down),
    // ic: unchanged; limit_exceeded (code: the request exceeds the host's bounds), unchanged; unsupported_capability
    // ic: (code: the host does not offer this service), unchanged.
    gf_status (GF_CALL* set_cursor)(gf_pointer_cursor cursor);
    /* The clipboard's text, read at each call; GF_STATUS_NOT_FOUND when it
     * holds none. */
    // ic: Requires not_null(required) and (not_null(buffer) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails invalid_state (code: the host has shut down), unchanged; limit_exceeded (code: the request
    // ic: exceeds the host's bounds), unchanged; not_found (GF_STATUS_NOT_FOUND when it holds none.), unchanged;
    // ic: unsupported_capability (code: the host does not offer this service), unchanged.
    gf_status (GF_CALL* read_clipboard_text)(char* buffer, uint64_t capacity, uint64_t* required);
    /* Replaces the clipboard's contents with text (at most 16 MiB). */
    // ic: Fails invalid_argument (code: the host refused the request), unchanged; invalid_state (code: the host has
    // ic: shut down), unchanged; limit_exceeded (code: clipboard text is at most 16 MiB), unchanged;
    // ic: unsupported_capability (code: the host does not offer this service), unchanged.
    gf_status (GF_CALL* write_clipboard_text)(gf_utf8_view text);
    /* Shows a modal dialog over the running window and returns when it
     * closes; posted callbacks and the window's events run meanwhile. The
     * paths it chose replace the held ones when it returns (K8): read them
     * with dialog_path_at before showing another dialog. A dialog shown
     * from a callback while this one is open stores its own paths when it
     * returns, before this one's, so each caller that reads straight after
     * its own call reads its own paths. */
    // ic: Fails invalid_argument (code: the host refused the request), unchanged; invalid_state (code: the host has
    // ic: shut down), unchanged; limit_exceeded (code: the request exceeds the host's bounds), unchanged;
    // ic: unsupported_capability (code: the host does not offer this service), unchanged.
    gf_status (GF_CALL* show_dialog)(const gf_dialog_request* request, gf_dialog_result* result);
    /* A path the last dialog chose, in the host's order. */
    // ic: Fails invalid_argument (code: the last dialog chose no path at that index), unchanged.
    gf_status (GF_CALL* dialog_path_at)(uint64_t index, char* buffer, uint64_t capacity, uint64_t* required);
} gf_host_table;

/*
 * GF_TABLE_DIAGNOSTICS: what a thread is doing. Its functions may be called
 * from any thread and read only the calling thread's state (the switches
 * are the process's); they open no shadow frame of their own, except
 * raise_native_exception (since 1.2).
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
/* Since 1.5. A boundary gate of the library's runtime that is none of the
 * above: name is its operation, such as a native callback entry's site
 * ("windows.window_procedure"). A reader of an older minor shows an unknown
 * kind by its number. */
#define GF_SHADOW_FRAME_KIND_GATE 7u

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

/*
 * Failure snapshots (since 1.2). When an exception inside the library
 * reaches a boundary, the library copies, on the failing thread, the
 * failure and the shadow frames it was thrown under: at a function of these
 * tables whose call it refuses, and at a native callback entry (a window
 * procedure, an AppKit or AT-SPI method), which ends the process (the
 * fail-fast boundary) after its report prints the snapshot. The frames are
 * those open where the exception was thrown, the frames it unwound
 * included; a thread's snapshot is replaced by its next one and is read
 * with failure_snapshot, as copies, after the refused call has returned.
 *
 * Exception reflection is a process switch, off at start. Off, a refused
 * call reports an exception the library has no status for as
 * GF_STATUS_INTERNAL; on, as GF_STATUS_NATIVE_EXCEPTION, which a binding
 * raises as its language's exception carrying the snapshot's frames. An
 * exception the library maps to a status of its own (GF_STATUS_OUT_OF_MEMORY,
 * GF_STATUS_INVALID_ARGUMENT, GF_STATUS_INVALID_STATE, ...) keeps it either
 * way, and also leaves a snapshot. The fail-fast boundary ends the process
 * either way: its caller is foreign code, which takes no status.
 */

/* The bytes a snapshot keeps of its site. */
#define GF_FAILURE_SITE_CAPACITY 64u
/* The bytes a snapshot keeps of its message (as gf_error_detail's). */
#define GF_FAILURE_MESSAGE_CAPACITY 512u

/*
 * The calling thread's last failure snapshot. An output record, as
 * gf_abi_version_info. status is the status the failing call returned
 * (GF_STATUS_NATIVE_EXCEPTION for the fail-fast boundary), GF_STATUS_OK
 * when the thread has no snapshot. table_id and slot_index name the
 * refusing function, as in gf_error_detail; both are zero at a native
 * callback entry. sequence counts the thread's snapshots from 1. depth
 * counts the frames open where the exception was thrown, recorded or not;
 * frame_count the recorded ones (at most GF_SHADOW_STACK_CAPACITY), which
 * failure_snapshot copies. site holds site_size bytes of UTF-8 naming the
 * boundary ("c_abi" for a function of these tables, else the callback
 * entry, such as "windows.window_procedure"), not terminated, the bytes
 * after it zero. message_size is the byte length of the message.
 */
typedef struct gf_failure_snapshot {
    uint32_t struct_size;
    gf_status status;
    gf_table_id table_id;
    uint32_t slot_index;
    uint64_t sequence;
    uint32_t frame_count;
    uint32_t depth;
    uint32_t site_size;
    uint32_t message_size;
    /* GF_FAILURE_SITE_CAPACITY bytes. */
    uint8_t site[64];
} gf_failure_snapshot;

/*
 * Failure detail (since 1.5): what a development build of the library knows
 * of the failure in the thread's snapshot beyond its frames, for a binding
 * to attach to its language's exception (GuiFormsException's native detail
 * in the C# binding). type holds type_size bytes of UTF-8 naming the
 * exception's C++ type, demangled where the runtime can ("std::runtime_error"),
 * not terminated, the bytes after it zero; trace holds trace_count return
 * addresses of the native stack where the exception was thrown, innermost
 * first, as numbers in this process (a symbolizer reads them against the
 * process's module map). A release build of the library records neither:
 * type_size and trace_count are zero. status and sequence are the
 * snapshot's, so a binding can tell that the detail is its failure's.
 *
 * flags (provisional: added to 1.5's record after its first form; it stays
 * 1.5's if 1.5 is unreleased when it lands, else it becomes 1.6's) says
 * more of the failure in every build. GF_FAILURE_DETAIL_FLAGS_FRAMES_LOST_AT_PARK:
 * the failure was parked by the library and rethrown later, and the park
 * could not keep its own copy of the frames (its allocation was refused,
 * out of memory included); the snapshot's frames are then what the thread
 * still held of the record, possibly none, not necessarily the whole
 * record. The library accepts a record of 1.5's first size (536 bytes,
 * without flags and reserved) and writes only the bytes that fit; a library
 * from before flags leaves them as the caller set them, so set them to
 * zero.
 */
#define GF_FAILURE_TYPE_CAPACITY 256u
#define GF_FAILURE_TRACE_CAPACITY 32u

typedef uint32_t gf_failure_detail_flags;
#define GF_FAILURE_DETAIL_FLAGS_NONE 0u
#define GF_FAILURE_DETAIL_FLAGS_FRAMES_LOST_AT_PARK 1u

/* The calling thread's last failure detail. An output record, as
 * gf_abi_version_info. */
typedef struct gf_failure_detail {
    uint32_t struct_size;
    gf_status status;
    uint64_t sequence;
    uint32_t type_size;
    uint32_t trace_count;
    /* GF_FAILURE_TYPE_CAPACITY bytes. */
    uint8_t type[256];
    /* GF_FAILURE_TRACE_CAPACITY addresses. */
    uint64_t trace[32];
    /* Provisional (see above). */
    gf_failure_detail_flags flags;
    uint32_t reserved;
} gf_failure_detail;

// ic: gf_abi::diagnostics: generated from include/gui_forms/c/gf_abi_diagnostics.ic.toml by tools/ic_declarations.py;
// ic: edit the declaration, not these lines. Thread: ui, unless a call says otherwise.
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
    // ic: Requires not_null(required) and (not_null(frames) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (When capacity is smaller, nothing else is written and the result is
    // ic: GF_STATUS_BUFFER_TOO_SMALL. frames may be null when capacity is zero.), unchanged; invalid_argument (code:
    // ic: the output record is null or smaller than its 1.0 size), unchanged.
    gf_status (GF_CALL* shadow_frames)(gf_shadow_frame* frames, uint64_t capacity, uint64_t* required,
                                       gf_shadow_stack_info* info);
    /*
     * The invalidation overlay (since 1.2), a debugging mode: every area a
     * window presents again is outlined for 300 ms, in a style naming its
     * cause (red: a control's invalidation; orange: layout; blue: a host
     * expose; green: a scroll copy; magenta: a live surface; cyan: external
     * content through a compatibility paint endpoint), with a legend. The
     * host composites it at present time, so it causes no damage of its
     * own. set_invalidation_overlay sets the process switch (enabled 0 or
     * 1), which every window without a setting of its own follows from its
     * next damage; it starts as the environment variable
     * GUI_FORMS_DEBUG_INVALIDATION asks (set, not empty and not "0").
     * invalidation_overlay reads it into enabled. Any thread.
     * GF_STATUS_INVALID_ARGUMENT for enabled other than 0 or 1, or a null
     * output.
     */
    // ic: Requires enabled <= 1, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Thread: any.
    gf_status (GF_CALL* set_invalidation_overlay)(uint32_t enabled);
    // ic: Requires not_null(enabled), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* invalidation_overlay)(uint32_t* enabled);
    /*
     * Since 1.2. Copies the calling thread's last failure snapshot into
     * snapshot, its frames, outermost first, into frames and its message
     * into message_buffer (not terminated). frames_required and
     * message_required receive the frame count and the message size. When
     * frame_capacity or message_capacity is smaller, nothing else is
     * written and the result is GF_STATUS_BUFFER_TOO_SMALL. frames and
     * message_buffer may be null when their capacity is zero. A thread with
     * no snapshot reads status GF_STATUS_OK and no frames. The snapshot
     * stays until the thread's next one or clear_failure_snapshot; the
     * frames and text are copies, and nothing is lent.
     */
    // ic: Requires not_null(frames_required) and not_null(message_required) and (not_null(frames) or frame_capacity ==
    // ic: 0) and (not_null(message_buffer) or message_capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (When frame_capacity or message_capacity is smaller, nothing else is
    // ic: written and the result is GF_STATUS_BUFFER_TOO_SMALL. frames and message_buffer may be null when their
    // ic: capacity is zero.), unchanged; invalid_argument (code: the output record is null or smaller than its 1.0
    // ic: size), unchanged.
    gf_status (GF_CALL* failure_snapshot)(gf_failure_snapshot* snapshot, gf_shadow_frame* frames,
                                          uint64_t frame_capacity, uint64_t* frames_required,
                                          char* message_buffer, uint64_t message_capacity,
                                          uint64_t* message_required);
    /* Since 1.2. Forgets the calling thread's failure snapshot. */
    gf_status (GF_CALL* clear_failure_snapshot)(void);
    /*
     * Since 1.2. Exception reflection's process switch (enabled 0 or 1), off
     * at start; exception_reflection reads it into enabled. Any thread.
     * GF_STATUS_INVALID_ARGUMENT for enabled other than 0 or 1, or a null
     * output.
     */
    // ic: Requires enabled <= 1, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Thread: any.
    gf_status (GF_CALL* set_exception_reflection)(uint32_t enabled);
    // ic: Requires not_null(enabled), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* exception_reflection)(uint32_t* enabled);
    /*
     * Since 1.2, a debugging aid with which a binding tests its reflection:
     * throws an exception with message (UTF-8, at most
     * GF_FAILURE_MESSAGE_CAPACITY bytes) inside the library, under a frame
     * of this call (the one diagnostics function that opens one), and
     * refuses the call as any other exception would: GF_STATUS_NATIVE_EXCEPTION
     * with reflection on, GF_STATUS_INTERNAL off, a snapshot either way.
     * GF_STATUS_INVALID_ARGUMENT, with no snapshot, for a message that is
     * not UTF-8 or too long.
     */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT, with no snapshot, for a message that is not UTF-8 or too
    // ic: long.), unchanged.
    gf_status (GF_CALL* raise_native_exception)(gf_utf8_view message);
    /*
     * Since 1.5. Copies the calling thread's last failure detail (the
     * snapshot's exception type and native trace) into detail. A thread
     * with no snapshot reads status GF_STATUS_OK, sequence zero and nothing
     * else. The detail stays and is forgotten with the snapshot
     * (clear_failure_snapshot). GF_STATUS_INVALID_ARGUMENT for a null
     * detail or one smaller than its 1.5 size.
     */
    // ic: Requires not_null(output), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires output_size >=
    // ic: failure_detail_size_1_5, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: failure_detail_output, then failure_detail_output_size.
    gf_status (GF_CALL* failure_detail)(gf_failure_detail* detail);
    /*
     * Since 1.5. The diagnostic overlay: the invalidation outlines and,
     * whenever they are on, the counters card at each window's top left
     * (renderer, controls, layout and paint passes, damage, input and
     * focus, wakes, presents and the worst present). It is the same
     * process switch as set_invalidation_overlay (enabled 0 or 1);
     * toggle_diagnostic_overlay turns it to the other state, the business
     * card's gesture (D held with a secondary press on the vendor name),
     * and writes the new state into enabled, which may be null. Any
     * thread. GF_STATUS_INVALID_ARGUMENT for enabled other than 0 or 1.
     */
    // ic: Requires enabled <= 1, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Thread: any.
    gf_status (GF_CALL* set_diagnostic_overlay)(uint32_t enabled);
    gf_status (GF_CALL* toggle_diagnostic_overlay)(uint32_t* enabled);
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

/* gf_relative_pointer_mode (since 1.5; C++ RelativePointerMode): FREE keeps
 * the ordinary pointer (shown, moving, clicking) and adds every motion
 * sample as a delta; CONFINED keeps the pointer inside the control (and the
 * window's release regions), its buttons and wheel samples with no clicks;
 * LOCKED hides the pointer and holds it still, only deltas arriving. */
typedef uint32_t gf_relative_pointer_mode;
#define GF_RELATIVE_POINTER_MODE_FREE 0u
#define GF_RELATIVE_POINTER_MODE_CONFINED 1u
#define GF_RELATIVE_POINTER_MODE_LOCKED 2u

/* gf_relative_pointer_result (since 1.5; C++ RelativePointerResult):
 * GRANTED as asked; DEGRADED below what was asked (the grant says what);
 * DETACHED, NOT_ACTIVE (the window inactive, or a dialog or menu open
 * without the control), NO_PRESS (until_release without a held button) and
 * INVALID_REQUEST (an unknown mode) refuse, changing nothing. */
typedef uint32_t gf_relative_pointer_result;
#define GF_RELATIVE_POINTER_RESULT_GRANTED 0u
#define GF_RELATIVE_POINTER_RESULT_DEGRADED 1u
#define GF_RELATIVE_POINTER_RESULT_DETACHED 2u
#define GF_RELATIVE_POINTER_RESULT_NOT_ACTIVE 3u
#define GF_RELATIVE_POINTER_RESULT_NO_PRESS 4u
#define GF_RELATIVE_POINTER_RESULT_INVALID_REQUEST 5u

/* gf_relative_pointer_end (since 1.5; C++ RelativePointerEnd): why a mode
 * ended (the relative_pointer_ended event's reason, and last_end). */
typedef uint32_t gf_relative_pointer_end;
#define GF_RELATIVE_POINTER_END_RELEASED 0u
#define GF_RELATIVE_POINTER_END_ESCAPE 1u
#define GF_RELATIVE_POINTER_END_FOCUS_LOST 2u
#define GF_RELATIVE_POINTER_END_DEACTIVATED 3u
#define GF_RELATIVE_POINTER_END_MODAL 4u
#define GF_RELATIVE_POINTER_END_DETACHED 5u
#define GF_RELATIVE_POINTER_END_PRESS_RELEASED 6u
#define GF_RELATIVE_POINTER_END_SUPERSEDED 7u

/* A relative pointer request (since 1.5; C++ RelativePointerRequest). An
 * input record: struct_size is sizeof(gf_relative_pointer_request) (24).
 * hide_cursor, raw_motion and until_release are 0 or 1. */
typedef struct gf_relative_pointer_request {
    uint32_t struct_size;
    gf_relative_pointer_mode mode;
    uint32_t hide_cursor;
    uint32_t raw_motion;
    uint32_t until_release;
    uint32_t reserved;
} gf_relative_pointer_request;

/* What a request got (since 1.5; C++ RelativePointerGrant). An output
 * record, as gf_abi_version_info. host_relative: the host delivers deltas
 * (0: the window derives them from positions). */
typedef struct gf_relative_pointer_grant {
    uint32_t struct_size;
    gf_relative_pointer_result result;
    gf_relative_pointer_mode mode;
    uint32_t raw;
    uint32_t cursor_hidden;
    uint32_t host_relative;
} gf_relative_pointer_grant;

/* A window's relative pointer state as seen from one of its controls
 * (since 1.5; C++ RelativePointerSnapshot and RelativePointerSupport). An
 * output record, as gf_abi_version_info. owned: this control holds the
 * mode. The support fields say what the window's host can do. */
typedef struct gf_relative_pointer_state {
    uint32_t struct_size;
    uint32_t owned;
    uint32_t active;
    uint32_t suspended;
    gf_relative_pointer_mode mode;
    uint32_t raw;
    uint32_t host_relative;
    gf_relative_pointer_end last_end;
    uint64_t grants;
    uint64_t samples;
    uint64_t suspensions;
    uint64_t resumptions;
    uint64_t ends;
    uint32_t support_relative_motion;
    uint32_t support_raw_motion;
    uint32_t support_lock;
    uint32_t support_confine;
    uint32_t support_warp;
    uint32_t reserved;
} gf_relative_pointer_state;

/*
 * Content effects (since 1.5): a filter and mask on a control and its
 * descendants together, as the theme's filter and mask on a control rule
 * (C++ Control::set_content_effect, PaintGroupEffect). The control's chunk
 * and its descendants' chunks, a custom control's painted pixels among
 * them, draw into one group; the group's image passes the colour matrix,
 * then the colour table, then the blur, then the mask layers, and is
 * composited where the control's window paints. A damage under an effect
 * spreads by its reach (ceil(1.5 x blur) + 3 logical units).
 *
 * gf_effect_mask_mode: how a mask layer's colour becomes its weight
 * (CSS mask-mode; the inverse modes are house extensions).
 * gf_effect_mask_composite: how a layer joins the layers beneath it
 * (CSS mask-composite).
 */
typedef uint32_t gf_effect_mask_mode;
#define GF_EFFECT_MASK_MODE_ALPHA 0u
#define GF_EFFECT_MASK_MODE_LUMINANCE 1u
#define GF_EFFECT_MASK_MODE_INVERSE_ALPHA 2u
#define GF_EFFECT_MASK_MODE_INVERSE_LUMINANCE 3u
typedef uint32_t gf_effect_mask_composite;
#define GF_EFFECT_MASK_COMPOSITE_ADD 0u
#define GF_EFFECT_MASK_COMPOSITE_SUBTRACT 1u
#define GF_EFFECT_MASK_COMPOSITE_INTERSECT 2u
#define GF_EFFECT_MASK_COMPOSITE_EXCLUDE 3u

/* The most mask layers of a content effect, and its largest blur in
 * logical units (the standard deviation is half of it, as CSS blur()'s
 * radius is read). */
#define GF_CONTENT_EFFECT_MAXIMUM_MASKS 4u
#define GF_CONTENT_EFFECT_MAXIMUM_BLUR 16u

/*
 * One mask layer: a linear ramp from start_rgba at start to end_rgba at
 * end, both 0xRRGGBBAA, the points in the group's normalized box (0,0 its
 * top left, 1,1 its bottom right; any finite value). Equal points give a
 * solid layer of start_rgba. reserved is 0. struct_size is the caller's
 * sizeof, the stride of an array of layers.
 */
typedef struct gf_effect_mask_layer {
    uint32_t struct_size;
    gf_effect_mask_mode mode;
    gf_effect_mask_composite composite;
    uint32_t start_rgba;
    uint32_t end_rgba;
    uint32_t reserved;
    gf_logical_point start;
    gf_logical_point end;
} gf_effect_mask_layer;

/*
 * A content effect. has_colour_matrix and has_colour_table are 0 or 1;
 * colour_matrix is row-major 4 x 5 on straight colour with channels in
 * [0, 1] (rows red, green, blue, alpha; the fifth column a constant; each
 * result clamped), the identity when has_colour_matrix is 0 and only the
 * table is given; colour_table is 256 entries each of red, green, blue and
 * alpha in that order, applied after the matrix (CSS feComponentTransfer's
 * table). blur is 0 to GF_CONTENT_EFFECT_MAXIMUM_BLUR. masks is mask_count
 * (at most GF_CONTENT_EFFECT_MAXIMUM_MASKS) layers, bottom first, null for
 * none; the library copies everything during the call.
 */
typedef struct gf_content_effect {
    uint32_t struct_size;
    uint32_t has_colour_matrix;
    uint32_t has_colour_table;
    uint32_t mask_count;
    double colour_matrix[20];
    uint8_t colour_table[1024];
    double blur;
    const gf_effect_mask_layer* masks;
} gf_content_effect;

/*
 * GF_TABLE_CONTROLS: operations on a control that are not state: layout
 * transactions, window-relative bounds, pointer capture and scrolling. A
 * control's state (a range's bounds, the cursor, the auto-scroll settings,
 * ...) is schema properties (GF_TABLE_PROPERTIES). UI thread
 * only. A scrolling function on a control that does not scroll gives
 * GF_STATUS_WRONG_HANDLE_KIND.
 */
// ic: gf_abi::controls: generated from include/gui_forms/c/gf_abi_controls.ic.toml by tools/ic_declarations.py; edit
// ic: the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_controls_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Defers layout until the matching resume_layout; transactions nest. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* suspend_layout)(gf_object object);
    /* Ends one suspension; perform_layout (0 or 1) lays out a deferred change
     * when the outermost one ends. Without a suspension it does nothing, as
     * Windows Forms tolerates an unmatched ResumeLayout. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: perform_layout is 0 or 1), unchanged.
    gf_status (GF_CALL* resume_layout)(gf_object object, uint32_t perform_layout);
    /* Lays the control's children out now. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* perform_layout)(gf_object object);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* layout_state)(gf_object object, gf_layout_transaction* state);
    /* The control's bounds in its window's client space, after layout. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(bounds),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: window_bounds_bounds, then
    // ic: the state refusal.
    gf_status (GF_CALL* window_bounds)(gf_object object, gf_logical_rect* bounds);
    /* Takes (1) or releases (0) the pointer for the control. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: captured is 0 or 1), unchanged.
    gf_status (GF_CALL* set_pointer_capture)(gf_object object, uint32_t captured);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires
    // ic: not_null(captured), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: pointer_capture_captured, then the state refusal.
    gf_status (GF_CALL* pointer_capture)(gf_object object, uint32_t* captured);
    gf_status (GF_CALL* scroll_state)(gf_object object, gf_scroll_snapshot* state);
    /* Sets one axis whole; an invalid axis gives INVALID_ARGUMENT and changes
     * nothing. An auto-scrolling control keeps its own range (the axis's
     * minimum and maximum are ignored), so its value must lie in that range. */
    // ic: Fails invalid_argument (code: an axis has a known orientation, finite non-negative bounds and changes, and a
    // ic: value ), unchanged.
    gf_status (GF_CALL* set_scroll_axis)(gf_object object, gf_scroll_orientation orientation,
                                         const gf_scroll_axis* axis);
    /* Scrolls object, after layout, so that child, its descendant, shows. */
    gf_status (GF_CALL* scroll_into_view)(gf_object object, gf_object child);
    /* Asks for a repaint of damage, in the control's logical units, or of
     * the whole control when damage is null; a custom control's paint
     * callback then runs (GF_TABLE_CUSTOM_CONTROLS). A damage rectangle
     * that is not finite, or has a negative size, gives INVALID_ARGUMENT. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: damage is finite with a size not negative), unchanged.
    gf_status (GF_CALL* invalidate)(gf_object object, const gf_logical_rect* damage);
    /* Gives the control keyboard focus, as Control.Focus and Select do
     * (since 1.2): it becomes its window's focused control, the one its
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
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state
    // ic: (GF_STATUS_INVALID_STATE, changing nothing, for a control not in a window, disabled or hidden (or under a
    // ic: disabled or hidden ancestor), not focusable (its focusable property, or its edit part's for a composite),
    // ic: outside the window's open focus scope (a dialog or overlay), or whose window's validation kept focus where it
    // ic: was.), unchanged.
    gf_status (GF_CALL* focus)(gf_object object, uint32_t* focused);
    /* Whether the control is its window's focused control (Focused; a
     * composite field is while its edit part has the caret), and whether it
     * or a descendant holds focus (ContainsFocus), as 0 or 1 (since 1.2).
     * A control not in a window has neither. Either output may be null. UI
     * thread. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* focus_state)(gf_object object, uint32_t* focused, uint32_t* contains_focus);
    /* Relative pointer mode (since 1.5): the control asks its window for
     * the pointer's motion as deltas, every sample, as the control's
     * relative_pointer event (the interaction catalog's
     * RelativePointerEventArgs), until relative_pointer_ended. Escape, focus
     * leaving the control, the window's deactivation, a dialog or menu,
     * and the control's disposal always end it; over a release region
     * (set_pointer_release_regions) a confined or locked mode is suspended
     * and the ordinary pointer reaches the region. grant receives the
     * outcome (a refusal is GF_STATUS_OK with a refusing result).
     * GF_STATUS_INVALID_ARGUMENT for a request flag that is not 0 or 1. UI
     * thread. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for a request flag that is not 0 or 1.), unchanged.
    gf_status (GF_CALL* request_relative_pointer)(gf_object object, const gf_relative_pointer_request* request,
                                                  gf_relative_pointer_grant* grant);
    /* Ends the control's own mode; released receives 1, or 0 when it had
     * none (may be null). */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* release_relative_pointer)(gf_object object, uint32_t* released);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* relative_pointer_state)(gf_object object, gf_relative_pointer_state* state);
    /* Moves the pointer to position, in the control's logical units, where
     * the host can (a locked mode moves its virtual position); warped
     * receives 1 or 0 (may be null). Only while the control holds the mode.
     * A position that is not finite gives INVALID_ARGUMENT. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: the position is not finite), unchanged.
    gf_status (GF_CALL* warp_pointer)(gf_object object, gf_logical_point position, uint32_t* warped);
    /* Sets the release regions of the control's window, in window logical
     * units: the shell's capsule and its kin (at most 8; count 0 clears).
     * A region that is not finite or is empty, or more than 8, gives
     * INVALID_ARGUMENT, changing nothing. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: at most 8 regions, and an array for any), unchanged; invalid_state (code: the control is not in a
    // ic: window), unchanged.
    gf_status (GF_CALL* set_pointer_release_regions)(gf_object object, const gf_logical_rect* regions,
                                                     uint32_t count);
    /* Sets the control's content effect (since 1.5), or clears it when
     * effect is null; a cleared control shows its theme's effect, if any.
     * An effect smaller than its 1.5 size, a flag that is not 0 or 1, a
     * value that is not finite, a blur out of range, more than 4 layers,
     * a layer array whose strides differ, or a layer with an unknown mode
     * or composite or a nonzero reserved gives INVALID_ARGUMENT, changing
     * nothing. A custom control's painted pixels are inside its group too:
     * this is the group effect for custom paint. UI thread. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: the effect is smaller than its 1.5 size), unchanged.
    gf_status (GF_CALL* set_content_effect)(gf_object object, const gf_content_effect* effect);
    /* Since 1.7. The control's viewport fit (stage VA): what it does when
     * its content, after layout, does not fit its viewport. A record
     * smaller than its 1.7 size, an answer or below_minimum outside
     * gf_overflow_answer, a below_minimum other than SCROLL or CLIP, a
     * scale that is not finite, a minimum_scale below 0, a maximum_scale
     * not above 0, below minimum_scale or above 1024, or an alignment
     * outside gf_content_alignment gives INVALID_ARGUMENT, changing
     * nothing. UI thread. */
    gf_status (GF_CALL* set_viewport_fit)(gf_object object, const gf_viewport_fit* fit);
    gf_status (GF_CALL* viewport_fit)(gf_object object, gf_viewport_fit* fit);
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
 * One styled text run (since 1.2; C++ TextDecoration): UTF-8 byte offsets
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
 * and history (TextBox), and its styled text runs (since 1.2). Positions are UTF-8 byte offsets
 * into the text property; a position inside a grapheme cluster or past the
 * text gives GF_STATUS_INVALID_ARGUMENT. Another control gives
 * GF_STATUS_WRONG_HANDLE_KIND. UI thread only.
 */
// ic: gf_abi::text_editing: generated from include/gui_forms/c/gf_abi_text_editing.ic.toml by tools/ic_declarations.py;
// ic: edit the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_text_editing_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Selects from anchor to caret; the caret is where typing goes. */
    // ic: Fails invalid_argument (code: a selection lies within the text), unchanged.
    gf_status (GF_CALL* select)(gf_object object, uint64_t anchor, uint64_t caret);
    // ic: Fails invalid_argument (code: selection needs anchor and caret outputs), unchanged.
    gf_status (GF_CALL* selection)(gf_object object, uint64_t* anchor, uint64_t* caret);
    /* The grapheme boundary after (direction 1) or before (-1) position;
     * position itself at either end of the text. */
    // ic: Fails invalid_argument (code: grapheme_boundary needs an output and a direction of 1 or -1), unchanged.
    gf_status (GF_CALL* grapheme_boundary)(gf_object object, uint64_t position, int32_t direction,
                                           uint64_t* boundary);
    /* Replaces length bytes at start with replacement as one history step,
     * as typing does (filters apply); changed receives 1 when the text
     * changed, 0 when the box refused it (read-only, a filter, the limit). */
    // ic: Requires not_null(changed), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument (code:
    // ic: replace_range needs a changed output), unchanged.
    gf_status (GF_CALL* replace_range)(gf_object object, uint64_t start, uint64_t length,
                                       gf_utf8_view replacement, uint32_t* changed);
    /* Undoes or redoes one history step; changed 0 when there is none. */
    // ic: Requires not_null(changed), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* undo)(gf_object object, uint32_t* changed);
    // ic: Requires not_null(changed), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* redo)(gf_object object, uint32_t* changed);
    /* Starts the history afresh without touching the text. */
    gf_status (GF_CALL* clear_undo_history)(gf_object object);
    /* Styled text runs (since 1.2; TextBox::set_decorations): decoration
     * layers over the text, composed in ascending layer order, which follow
     * every edit and never change the text. A multiline box draws them. A
     * range past the text, a range ending before it starts, a weight
     * outside 1 to 1000, an underline or ink out of range, an unknown field
     * bit or a nonzero reserved gives GF_STATUS_INVALID_ARGUMENT and changes
     * nothing. decorations may be null when count is 0.
     * Replaces layer's decorations; count 0 clears the layer. */
    // ic: Fails invalid_argument (A range past the text, a range ending before it starts, a weight outside 1 to 1000,
    // ic: an underline or ink out of range, an unknown field bit or a nonzero reserved gives GF_STATUS_INVALID_ARGUMENT
    // ic: and changes nothing. decorations may be null when count is 0.), unchanged.
    gf_status (GF_CALL* set_decorations)(gf_object object, uint32_t layer,
                                         const gf_text_decoration* decorations, uint64_t count);
    /* Replaces layer's decorations within [start, end) only, as an
     * incremental highlighter does after an edit: decorations inside go,
     * those crossing the bounds keep their parts outside, and the given
     * ones (each inside the range) come in. Repaints only that range's
     * rows unless a weight or italics changed. */
    // ic: Fails invalid_argument (code: the range lies within the text), unchanged.
    gf_status (GF_CALL* replace_decorations)(gf_object object, uint32_t layer, uint64_t start, uint64_t end,
                                             const gf_text_decoration* decorations, uint64_t count);
    /* Clears every layer. */
    gf_status (GF_CALL* clear_decorations)(gf_object object);
    /* The number of decorations in layer, in start order (0 for a layer
     * without any). */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* decoration_count)(gf_object object, uint32_t layer, uint64_t* count);
    /* Decoration index (below decoration_count) of layer, as it stands
     * after the edits since it was set. GF_STATUS_INVALID_ARGUMENT past the
     * end. */
    // ic: Requires not_null(decoration), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT past the end.), unchanged.
    gf_status (GF_CALL* decoration_at)(gf_object object, uint32_t layer, uint64_t index,
                                       gf_text_decoration* decoration);
    /* Since 1.2. The position in UTF-16 code units of the byte offset
     * utf8_offset in the text, for a caller whose positions are UTF-16 (a
     * Windows Forms SelectionStart), and back: utf16_to_utf8_offset gives
     * the byte offset of a UTF-16 position. An offset past the text, inside
     * a UTF-8 sequence or between the halves of a surrogate pair gives
     * GF_STATUS_INVALID_ARGUMENT. Positions on scalar boundaries convert
     * both ways exactly. */
    // ic: Requires not_null(utf16_offset), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument
    // ic: (An offset past the text, inside a UTF-8 sequence or between the halves of a surrogate pair gives
    // ic: GF_STATUS_INVALID_ARGUMENT.), unchanged.
    gf_status (GF_CALL* utf8_to_utf16_offset)(gf_object object, uint64_t utf8_offset, uint64_t* utf16_offset);
    // ic: Requires not_null(utf8_offset), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument
    // ic: (code: utf16_to_utf8_offset needs an output), unchanged.
    gf_status (GF_CALL* utf16_to_utf8_offset)(gf_object object, uint64_t utf16_offset, uint64_t* utf8_offset);
} gf_text_editing_table;

/*
 * GF_TABLE_OVERLAYS: overlay roots and tooltips. An overlay is a root control,
 * without a parent, shown over its owner's window until close_overlay, until
 * its owner leaves the window or until the window closes. Tooltips belong to
 * a tool_tip component (create_with). UI thread only.
 *
 * open_anchored (since 1.2) shows a content control beside an anchor, as a
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
#define GF_ANCHORED_OVERLAY_ALIGN_END 2u
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
 * How open_anchored shows content (since 1.2). width and height are the
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

/*
 * gf_tool_tips_setting: a window's tooltip switch (since 1.2). A window
 * that follows takes the program's switch (the current application's;
 * shown when there is none); the others decide for the window alone.
 * Tooltips whose text is empty or only white space never show, whatever
 * the switches.
 */
typedef uint32_t gf_tool_tips_setting;
#define GF_TOOL_TIPS_FOLLOW 0u
#define GF_TOOL_TIPS_SHOWN 1u
#define GF_TOOL_TIPS_HIDDEN 2u

// ic: gf_abi::overlays: generated from include/gui_forms/c/gf_abi_overlays.ic.toml by tools/ic_declarations.py; edit
// ic: the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_overlays_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Shows overlay over owner's window. owner must be in a running window
     * (else GF_STATUS_INVALID_STATE); an overlay already open gives
     * GF_STATUS_INVALID_STATE. */
    // ic: Fails invalid_state (Shows overlay over owner's window. owner must be in a running window (else
    // ic: GF_STATUS_INVALID_STATE); an overlay already open gives GF_STATUS_INVALID_STATE.), unchanged.
    gf_status (GF_CALL* open_overlay)(gf_object owner, gf_object overlay);
    /* Closes overlay; one not open gives GF_STATUS_NOT_FOUND. */
    // ic: Fails not_found (Closes overlay; one not open gives GF_STATUS_NOT_FOUND.), unchanged.
    gf_status (GF_CALL* close_overlay)(gf_object overlay);
    /* The text tool_tip shows for target; empty text removes it. */
    // ic: Fails wrong_handle_kind (code: the object is not a tool_tip component), unchanged.
    gf_status (GF_CALL* set_tool_tip)(gf_object tool_tip, gf_object target, gf_utf8_view text);
    /* Shows target's text now, for duration_ms milliseconds (0: until the
     * pointer leaves or hide_tool_tip). */
    // ic: Fails wrong_handle_kind (code: the object is not a tool_tip component), unchanged.
    gf_status (GF_CALL* show_tool_tip)(gf_object tool_tip, gf_object target, uint64_t duration_ms);
    // ic: Fails wrong_handle_kind (code: the object is not a tool_tip component), unchanged.
    gf_status (GF_CALL* hide_tool_tip)(gf_object tool_tip);
    /* Shows content, a control with no parent and not in a window, beside
     * anchor, a control in a running window (since 1.2). The library holds
     * content while it is open. GF_STATUS_INVALID_STATE when anchor is not
     * in a window or is disabled, or content is already open or has a
     * parent; GF_STATUS_INVALID_ARGUMENT for options smaller than their 1.2
     * size, unknown flags or a size or gap that is negative or not finite.
     * Nothing is shown on a refusal. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for options smaller than their 1.2 size, unknown flags or a size or gap that is
    // ic: negative or not finite.), unchanged; invalid_state (GF_STATUS_INVALID_STATE when anchor is not in a window or
    // ic: is disabled, or content is already open or has a parent;), unchanged.
    gf_status (GF_CALL* open_anchored)(gf_object anchor, gf_object content,
                                       const gf_anchored_overlay_options* options);
    /* Closes content's anchored overlay: its "closed" event follows before
     * this returns. GF_STATUS_NOT_FOUND when it is not open (since 1.2). */
    // ic: Fails not_found (GF_STATUS_NOT_FOUND when it is not open (since 1.2).), unchanged.
    gf_status (GF_CALL* close_anchored)(gf_object content);
    /* Sets a window's tooltip switch, or with window null the program's
     * (the current application's; GF_STATUS_INVALID_STATE without one),
     * where FOLLOW is refused. Turning tooltips off closes one that shows.
     * GF_STATUS_INVALID_ARGUMENT for an unknown setting;
     * GF_STATUS_WRONG_HANDLE_KIND when window is not a window (since 1.2). */
    // ic: Fails disposed (code: the window has been disposed or has closed), unchanged; invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for an unknown setting;), unchanged; invalid_state (GF_STATUS_INVALID_STATE
    // ic: without one), where FOLLOW is refused.), unchanged; wrong_handle_kind (GF_STATUS_WRONG_HANDLE_KIND when
    // ic: window is not a window (since 1.2).), unchanged.
    gf_status (GF_CALL* set_tool_tips_shown)(gf_object window, gf_tool_tips_setting setting);
    /* Whether a window's tooltips show now (its switch, else the
     * program's), or with window null the program's switch: 1 or 0 in
     * shown, and the window's own setting in setting, which may be null
     * (FOLLOW for the program's) (since 1.2). */
    // ic: Requires not_null(shown), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails disposed (code: the
    // ic: window has been disposed or has closed), unchanged; wrong_handle_kind (code: the object is not a window),
    // ic: unchanged.
    gf_status (GF_CALL* tool_tips_shown)(gf_object window, uint32_t* shown, gf_tool_tips_setting* setting);
} gf_overlays_table;

/*
 * GF_TABLE_PROPERTY_GRID: a property grid's selection and its editing
 * operations (PropertyGrid). Its sort is the property_sort property. A name
 * is a property of the selected objects. Another control gives
 * GF_STATUS_WRONG_HANDLE_KIND. UI thread only.
 */
// ic: gf_abi::property_grid: generated from include/gui_forms/c/gf_abi_property_grid.ic.toml by
// ic: tools/ic_declarations.py; edit the declaration, not these lines. States: alive (initial), disposing, disposed.
// ic: Observed by gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_property_grid_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Shows the properties the count controls share; count 0 clears. The
     * grid keeps them as it keeps a child, without a reference. */
    // ic: Fails invalid_argument (code: select_objects takes at most 4096 objects, and objects when count is not zero),
    // ic: unchanged.
    gf_status (GF_CALL* select_objects)(gf_object grid, const gf_object* objects, uint64_t count);
    /* Reads the selected objects' properties afresh. */
    gf_status (GF_CALL* refresh)(gf_object grid);
    /* Parses text into the named property as its editor would; accepted
     * receives 0 when the text does not convert or the property refuses it. */
    // ic: Requires not_null(accepted), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* try_set_text)(gf_object grid, gf_utf8_view name, gf_utf8_view text, uint32_t* accepted);
    /* Resets the named property; reset receives 0 when it cannot reset. */
    // ic: Requires not_null(reset), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* reset_property)(gf_object grid, gf_utf8_view name, uint32_t* reset);
    /* Opens the named property's editor; activated 0 when it has none. */
    // ic: Requires not_null(activated), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
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
 * What a custom control publishes to accessibility (since 1.2), filled by
 * its semantics callback. The library sets struct_size and fills the record
 * with the base's own description first, so a callback changes only what it
 * knows. role is an index into the schema's semantic roles
 * (GF_SCHEMA_VOCABULARY_SEMANTIC_ROLES); name, value and description are
 * UTF-8 the callback lends until the library's next callback on the
 * control. A role past the vocabulary, or text that is not UTF-8, makes the
 * library keep the base's description.
 */
typedef struct gf_custom_semantics {
    uint32_t struct_size;
    uint32_t role;
    gf_utf8_view name;
    gf_utf8_view value;
    gf_utf8_view description;
} gf_custom_semantics;

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
 * A custom control's layout callbacks (since 1.2), each optional (null
 * keeps the base's behaviour) and called as the gf_custom_control_callbacks
 * are, with their context: measure writes the control's preferred size for
 * available (each axis finite; 1e9 stands for unbounded) into preferred,
 * for auto-sizing layouts; hit_test writes 1 to hit when point (in the
 * control's logical units, inside its bounds) is on the control, 0 to let
 * the pointer through to what lies under it; semantics fills a
 * gf_custom_semantics. Each returns GF_STATUS_OK to answer; any other
 * status, a negative or non-finite size, or a hit other than 0 or 1 makes
 * the library use the base's answer. Like paint, they must not change
 * controls. (They are a record of their own because the spec holds the
 * 1.0 callbacks by value, which cannot grow.) reserved is zero.
 */
typedef struct gf_custom_layout_callbacks {
    uint32_t struct_size;
    uint32_t reserved;
    gf_status (GF_CALL* measure)(void* context, gf_object control, gf_logical_size available,
                                 gf_logical_size* preferred);
    gf_status (GF_CALL* hit_test)(void* context, gf_object control, gf_logical_point point, uint32_t* hit);
    gf_status (GF_CALL* semantics)(void* context, gf_object control, gf_custom_semantics* semantics);
} gf_custom_layout_callbacks;

/*
 * A custom control's definition. property_size is the size of one
 * gf_custom_property as the caller declares it (at least the 1.0 size); the
 * library steps through properties by it. properties may be null when
 * property_count is 0; at most GF_CUSTOM_MAXIMUM_PROPERTIES. callbacks is
 * read up to its own struct_size; fields past it are taken as zero.
 *
 * layout (since 1.2, read when struct_size covers it) points at the
 * control's layout callbacks, or is null for none; create copies the
 * record up to its struct_size, so it need live only through the call.
 */
typedef struct gf_custom_control_spec {
    uint32_t struct_size;
    gf_custom_base base;
    gf_custom_control_flags flags;
    uint32_t property_size;
    const gf_custom_property* properties;
    uint64_t property_count;
    gf_custom_control_callbacks callbacks;
    const gf_custom_layout_callbacks* layout;
} gf_custom_control_spec;

/*
 * Since 1.5. A composite control (a `.control` document): parts, states,
 * properties, events and commands declared in the document, its children
 * stock controls, the C form of a C++ CompositeControl. load_composite
 * reads, checks and compiles the document once into a definition, an
 * object the caller holds like any other (objects.release frees it; the
 * controls made from it keep what they need). The children's element
 * names are resolved among child_types, each a schema type id in any
 * spelling (GF_TABLE_SCHEMA), at most GF_COMPOSITE_MAXIMUM_CHILD_TYPES;
 * a type that is not a control made from its stable id alone is refused.
 * file names the document in the diagnostics. Everything in the record is
 * read during the call only; struct_size is at least the 1.5 size and
 * reserved zero. A refused document gives GF_STATUS_INVALID_ARGUMENT with
 * its diagnostics, one line each, as the failure's message as far as it
 * holds them (runtime.error_detail), and no definition.
 *
 * create_composite makes a control from a definition with stable_id (the
 * objects table's rule) and gives the caller one reference; its children
 * have stable_id.<local> for each `@local` of the document. Its type id
 * (objects.type_id) is the document's name in snake case, which the
 * schema table lists among its composites (composite_count, since 1.5,
 * provisional) while the definition is held; its own properties are reached through
 * GF_TABLE_PROPERTIES under their registered names, and its declared
 * events through events.subscribe under theirs, with the payload, when it
 * has one, as the field "value". perform_composite runs one of its
 * commands by name, as a part's activation does; performed receives 1
 * when it ran, 0 when it is not enabled (it may be null), and a name the
 * document does not declare gives GF_STATUS_NOT_FOUND. composite_child
 * gives a new reference to the child of local ID local_id (`title` for
 * `@title`), GF_STATUS_NOT_FOUND when there is none. Another control
 * gives GF_STATUS_WRONG_HANDLE_KIND. UI thread only.
 */
#define GF_COMPOSITE_MAXIMUM_CHILD_TYPES 64u

typedef struct gf_composite_spec {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view file;
    gf_utf8_view document;
    const gf_utf8_view* child_types;
    uint64_t child_type_count;
} gf_composite_spec;

// ic: gf_abi::custom_controls: generated from include/gui_forms/c/gf_abi_custom_controls.ic.toml by
// ic: tools/ic_declarations.py; edit the declaration, not these lines. States: alive (initial), disposing, disposed.
// ic: Observed by gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_custom_controls_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Creates a custom control with stable_id (the objects table's rule:
     * lowercase '.'-separated, '-'-joined segments) and gives the
     * caller one reference to it. A malformed spec gives INVALID_ARGUMENT
     * and calls no callback, release among them. */
    // ic: Requires not_null(spec) and not_null(object), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires
    // ic: spec_size >= spec_size_1_0, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: create_outputs, then create_spec_size. Fails invalid_argument (code: a stable id that breaks the rule, a
    // ic: property definition the spec cannot hold, or layout callbacks whose struct_size is below 32, not a multiple
    // ic: of 8, or whose reserved is not zero), unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* create)(const gf_custom_control_spec* spec, gf_utf8_view stable_id, gf_object* object);
    /* Reports that property index changed, as a C++ control raises its
     * changed event: subscribers and bindings read it afresh. A property
     * declared without GF_CUSTOM_PROPERTY_CHANGE_NOTIFICATIONS gives
     * INVALID_ARGUMENT; another control gives WRONG_HANDLE_KIND. */
    // ic: Fails invalid_argument (code: the index is past the properties), unchanged; wrong_handle_kind (code: the
    // ic: control is not a custom control), unchanged.
    gf_status (GF_CALL* notify_changed)(gf_object object, uint64_t index);
    /* Since 1.5. Composite controls (load_composite to composite_child),
     * below. */
    // ic: Fails invalid_argument (code: load_composite needs a gf_composite_spec of at least the 1.5 size and a
    // ic: definition output), unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* load_composite)(const gf_composite_spec* spec, gf_object* definition);
    // ic: Requires not_null(object), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails disposed (code: the
    // ic: definition has been disposed), unchanged; wrong_handle_kind (code: create_composite takes a definition),
    // ic: unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* create_composite)(gf_object definition, gf_utf8_view stable_id, gf_object* object);
    // ic: Fails not_found (code: the composite declares no command of that name), unchanged.
    gf_status (GF_CALL* perform_composite)(gf_object control, gf_utf8_view command, uint32_t* performed);
    // ic: Requires not_null(child), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails not_found (code: the
    // ic: composite has no child of that local id), unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* composite_child)(gf_object control, gf_utf8_view local_id, gf_object* child);
} gf_custom_controls_table;

/*
 * GF_TABLE_BINDINGS: data binding, as C++ binds: a binding_source (made
 * with create_with over a window, or over a control in a running window)
 * holds records of named fields and a
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
/* The most records one bindings.add_records call takes (since 1.2). */
#define GF_BINDING_MAXIMUM_RECORDS_PER_CALL 65536u
/* The position add_record gives a record the source's filter hides (since
 * 1.2). */
#define GF_BINDING_HIDDEN_POSITION 18446744073709551615ull
/* The column that names a whole row: its own error text, or a change to
 * every cell of it (since 1.2). */
#define GF_BINDING_WHOLE_ROW 18446744073709551615ull

/*
 * Since 1.2 (N31): data providers. A source shows the rows of a provider in
 * place instead of holding copies: the provider declares its columns once,
 * answers the source's reads (the row count, a cell, a row's stable id) and
 * accepts or refuses its writes, and reports its own changes through
 * bindings.notify. A source made by create_with starts with the library's
 * own provider, a column table, which add_record and the other record slots
 * fill (growing a column for each new field name).
 *
 * The provider is untrusted data: a callback that fails, or answers a value
 * of the wrong kind, text that is not UTF-8 or past the bounds, or an id
 * that breaks the StableId rule, makes that cell or row an error the source
 * shows, never a failure of the source. Only the row count is trusted to
 * change through notify alone.
 */

/* One column of a provider: its name (a registered name in any spelling;
 * names are distinct after folding), the gf_value_kind it holds
 * (GF_VALUE_KIND_NULL for any kind; every column takes a NULL value), and
 * read_only 0 or 1. reserved is zero. */
typedef struct gf_binding_column {
    uint32_t struct_size;
    gf_value_kind kind;
    gf_utf8_view name;
    uint32_t read_only;
    uint32_t reserved;
} gf_binding_column;

/* gf_binding_sort_direction. */
typedef uint32_t gf_binding_sort_direction;
#define GF_BINDING_SORT_ASCENDING 0u
#define GF_BINDING_SORT_DESCENDING 1u

/* One sort key: a column by name and its direction. Equal keys keep the
 * provider's order; text compares ignoring case, by code point. */
typedef struct gf_binding_sort_key {
    uint32_t struct_size;
    gf_binding_sort_direction direction;
    gf_utf8_view column;
} gf_binding_sort_key;

/* gf_binding_predicate_operator: how a cell is compared with the operand
 * (contains and starts_with compare text ignoring case; is_null and
 * is_not_null take no operand). */
typedef uint32_t gf_binding_predicate_operator;
#define GF_BINDING_PREDICATE_EQUAL 0u
#define GF_BINDING_PREDICATE_NOT_EQUAL 1u
#define GF_BINDING_PREDICATE_LESS 2u
#define GF_BINDING_PREDICATE_LESS_OR_EQUAL 3u
#define GF_BINDING_PREDICATE_GREATER 4u
#define GF_BINDING_PREDICATE_GREATER_OR_EQUAL 5u
#define GF_BINDING_PREDICATE_CONTAINS 6u
#define GF_BINDING_PREDICATE_STARTS_WITH 7u
#define GF_BINDING_PREDICATE_IS_NULL 8u
#define GF_BINDING_PREDICATE_IS_NOT_NULL 9u

/* One filter predicate: a column by name, an operator and its operand (NULL
 * for none). A filter shows the rows every predicate holds for. */
typedef struct gf_binding_predicate {
    uint32_t struct_size;
    gf_binding_predicate_operator op;
    gf_utf8_view column;
    const gf_value* operand;
} gf_binding_predicate;

/* gf_binding_notification_kind: a provider's change. reset: anything may
 * have changed (the columns too); inserted and removed: count rows at row;
 * moved: count rows at row now start at target; changed: the cell (row,
 * target), or every cell of the row with target GF_BINDING_WHOLE_ROW. Rows
 * are the provider's, counted after the change for inserted, before it
 * for removed and moved. */
typedef uint32_t gf_binding_notification_kind;
#define GF_BINDING_NOTIFY_RESET 0u
#define GF_BINDING_NOTIFY_INSERTED 1u
#define GF_BINDING_NOTIFY_REMOVED 2u
#define GF_BINDING_NOTIFY_MOVED 3u
#define GF_BINDING_NOTIFY_CHANGED 4u

/* gf_binding_provider_flags: what a provider does itself. */
typedef uint32_t gf_binding_provider_flags;
/* apply_sort orders its rows; otherwise the source keeps its own index. */
#define GF_BINDING_PROVIDER_SORTS 1u
/* apply_filter subsets its rows; otherwise the source keeps its own index. */
#define GF_BINDING_PROVIDER_FILTERS 2u
/* insert_row adds rows. */
#define GF_BINDING_PROVIDER_INSERTS 4u
/* remove_rows removes rows. */
#define GF_BINDING_PROVIDER_REMOVES 8u

/*
 * A provider's callbacks, called on the source's UI thread with context,
 * only from within a call on the source (a read while painting, a binding's
 * transfer, a slot), never after release. Rows are the provider's own
 * order, below its row count. Text a callback lends (a value's text, an id,
 * a reason, an error) stays valid until the library's next callback on the
 * provider. Each returns GF_STATUS_OK to answer; any other status is a
 * refusal or, for a read, a cell error.
 *
 * row_count writes the number of rows (at most 2^31). read_value fills
 * value, whose struct_size the library sets, with the cell (row, column).
 * read_stable_id writes the row's stable id. write_value receives a value
 * already checked against the column's kind and read-only flag, and writes
 * accepted 1 to take it into the provider's data, 0 to refuse it with
 * reason; the provider reports nothing for a write it accepted (the source
 * applies it). read_cell_state, which may be null, writes the cell's
 * read_only (0 or 1) and error text (empty for none), or the row's with
 * column GF_BINDING_WHOLE_ROW. apply_sort and apply_filter, required with
 * their flags, reorder or subset the rows (count 0 clears) and report a
 * reset afterwards; insert_row and remove_rows, required with their flags,
 * write accepted and report nothing (the source applies it). release,
 * which may be null, is called once, when the source lets the provider go
 * (another set_provider, or its disposal); no callback runs after it.
 * reserved is zero.
 */
typedef struct gf_binding_provider_callbacks {
    uint32_t struct_size;
    gf_binding_provider_flags flags;
    void* context;
    gf_status (GF_CALL* row_count)(void* context, uint64_t* count);
    gf_status (GF_CALL* read_value)(void* context, uint64_t row, uint64_t column, gf_value* value);
    gf_status (GF_CALL* read_stable_id)(void* context, uint64_t row, gf_utf8_view* stable_id);
    gf_status (GF_CALL* write_value)(void* context, uint64_t row, uint64_t column, const gf_value* value,
                                     uint32_t* accepted, gf_utf8_view* reason);
    gf_status (GF_CALL* read_cell_state)(void* context, uint64_t row, uint64_t column, uint32_t* read_only,
                                         gf_utf8_view* error);
    gf_status (GF_CALL* apply_sort)(void* context, const gf_binding_sort_key* keys, uint64_t count);
    gf_status (GF_CALL* apply_filter)(void* context, const gf_binding_predicate* predicates, uint64_t count);
    gf_status (GF_CALL* insert_row)(void* context, uint64_t row, gf_utf8_view stable_id, const gf_value* values,
                                    uint64_t value_count, uint32_t* accepted);
    gf_status (GF_CALL* remove_rows)(void* context, uint64_t row, uint64_t count, uint32_t* accepted);
    void (GF_CALL* release)(void* context);
} gf_binding_provider_callbacks;

// ic: gf_abi::bindings: generated from include/gui_forms/c/gf_abi_bindings.ic.toml by tools/ic_declarations.py; edit
// ic: the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_bindings_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Binds as arguments says and gives the caller one reference to the
     * binding. The control keeps the binding until unbind or its disposal.
     * A property that is not bindable gives GF_STATUS_INVALID_ARGUMENT. */
    // ic: Fails disposed (code: the target has been disposed), unchanged; invalid_argument (A property that is not
    // ic: bindable gives GF_STATUS_INVALID_ARGUMENT.), unchanged. Returns gf_object, owned by the caller.
    gf_status (GF_CALL* bind)(const gf_binding_arguments* arguments, gf_object* binding);
    /* Removes the binding from its control, which disposes it, as
     * ControlBindingsCollection::remove does; a later call on it gives
     * GF_STATUS_DISPOSED. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state (code:
    // ic: the binding is no longer bound), unchanged.
    gf_status (GF_CALL* unbind)(gf_object binding);
    /* Sets the control's property from the source (read_value) or the
     * source's field from the control (write_value), as the binding's modes
     * would; updated receives 0 when conversion or validation refused it. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(updated),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: read_value_updated, then the
    // ic: state refusal.
    gf_status (GF_CALL* read_value)(gf_object binding, uint32_t* updated);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(updated),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: write_value_updated, then
    // ic: the state refusal.
    gf_status (GF_CALL* write_value)(gf_object binding, uint32_t* updated);
    /* Appends a record; index receives its position
     * (GF_BINDING_HIDDEN_POSITION when the source's filter hides it). */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(index),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: add_record_index, then the
    // ic: state refusal. Fails invalid_state (code: the source's provider does not insert rows), unchanged.
    gf_status (GF_CALL* add_record)(gf_object source, const gf_binding_record* record, uint64_t* index);
    /* Inserts a record at index (at most the count). */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: the index is past the records), unchanged; invalid_state (code: the source's provider does not insert
    // ic: rows), unchanged.
    gf_status (GF_CALL* insert_record)(gf_object source, uint64_t index, const gf_binding_record* record);
    /* Removes the record at index; removed 0 for an index past the
     * records. A source that allows no removal gives
     * GF_STATUS_INVALID_STATE. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(removed),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: remove_record_removed, then
    // ic: the state refusal. Fails invalid_state (A source that allows no removal gives GF_STATUS_INVALID_STATE.),
    // ic: unchanged.
    gf_status (GF_CALL* remove_record)(gf_object source, uint64_t index, uint32_t* removed);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* clear_records)(gf_object source);
    /* Moves to the record at position (-1 for none); moved 0 when it is
     * already there. A position past the records gives
     * GF_STATUS_INVALID_ARGUMENT. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(moved),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: set_position_moved, then the
    // ic: state refusal. Fails invalid_argument (A position past the records gives GF_STATUS_INVALID_ARGUMENT.),
    // ic: unchanged.
    gf_status (GF_CALL* set_position)(gf_object source, int64_t position, uint32_t* moved);
    /* The current position (-1 for none) and the count of records. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: position needs both outputs), unchanged.
    gf_status (GF_CALL* position)(gf_object source, int64_t* position, uint64_t* count);
    /* The current record's field, as properties.get gives a value; no
     * current record or no such field gives GF_STATUS_NOT_FOUND. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails not_found (The
    // ic: current record's field, as properties.get gives a value; no current record or no such field gives
    // ic: GF_STATUS_NOT_FOUND.), unchanged.
    gf_status (GF_CALL* current_field)(gf_object source, gf_utf8_view field, gf_value* value, char* text_buffer,
                                       uint64_t text_capacity, uint64_t* text_required);
    /* Sets the current record's field; accepted 0 when there is no current
     * record or it, or the source, allows no edit. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires
    // ic: not_null(accepted), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: set_current_field_accepted, then the state refusal.
    gf_status (GF_CALL* set_current_field)(gf_object source, gf_utf8_view field, const gf_value* value,
                                           uint32_t* accepted);
    /* Since 1.2 (N31). Appends count records (the array rule: the stride
     * is records[0].struct_size), validated whole before any is added, so
     * a refusal adds none; first receives the first one's position. The
     * source raises one reset list change for the call instead of one per
     * record (when it raises list changes at all). More than
     * GF_BINDING_MAXIMUM_RECORDS_PER_CALL records give
     * GF_STATUS_LIMIT_EXCEEDED; count 0 adds nothing and writes first. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(first)
    // ic: and (not_null(records) or count == 0), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several
    // ic: refusals apply: add_records_records, then the state refusal. Fails invalid_argument (code: add_records needs
    // ic: records and a first output), unchanged; invalid_state (code: the source's provider does not insert rows),
    // ic: unchanged; limit_exceeded (More than GF_BINDING_MAXIMUM_RECORDS_PER_CALL records give
    // ic: GF_STATUS_LIMIT_EXCEEDED; count 0 adds nothing and writes first.), unchanged.
    gf_status (GF_CALL* add_records)(gf_object source, const gf_binding_record* records, uint64_t count,
                                     uint64_t* first);
    /* Since 1.2 (N31). Binds the provider callbacks describes, with
     * column_count columns (at most GF_BINDING_MAXIMUM_FIELDS), to source;
     * provider receives its id for notify. NULL callbacks bind a new empty
     * column table. The previous provider is let go (its release is called
     * and its id refused from then on); the first row becomes current. The
     * library copies columns and callbacks during the call. */
    // ic: Requires not_null(provider), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires not_null(callbacks)
    // ic: or column_count == 0, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: set_provider_provider, then set_provider_columns. Fails invalid_argument (code: provider callbacks or columns
    // ic: their checks refuse), unchanged; wrong_handle_kind (code: the object is not a binding source), unchanged.
    gf_status (GF_CALL* set_provider)(gf_object source, const gf_binding_column* columns, uint64_t column_count,
                                      const gf_binding_provider_callbacks* callbacks, uint64_t* provider);
    /* Since 1.2 (N31). Reports a provider's change, from any thread: on
     * the source's UI thread outside a callback it applies at once,
     * otherwise it is posted and applied there in order. An id the source
     * has let go, or whose source is disposed, gives GF_STATUS_REMOVED. */
    // ic: Fails invalid_argument (code: kind is a gf_binding_notification_kind), unchanged; removed (An id the source
    // ic: has let go, or whose source is disposed, gives GF_STATUS_REMOVED.), unchanged. Thread: any.
    gf_status (GF_CALL* notify)(uint64_t provider, gf_binding_notification_kind kind, uint64_t row, uint64_t count,
                                uint64_t target);
    /* Since 1.2 (N31). Sorts the source's rows by count keys (0 clears),
     * through the provider when it sorts, otherwise through the source's
     * own index; an unknown column gives GF_STATUS_NOT_FOUND and changes
     * nothing. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails not_found (Sorts the
    // ic: source's rows by count keys (0 clears), through the provider when it sorts, otherwise through the source's
    // ic: own index; an unknown column gives GF_STATUS_NOT_FOUND and changes nothing.), unchanged.
    gf_status (GF_CALL* set_sort)(gf_object source, const gf_binding_sort_key* keys, uint64_t count);
    /* Since 1.2 (N31). Filters the source's rows by count predicates (0
     * clears), as set_sort does. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails not_found (code: a
    // ic: predicate names a column the source has not got), unchanged.
    gf_status (GF_CALL* set_filter)(gf_object source, const gf_binding_predicate* predicates, uint64_t count);
    /* Since 1.2 (N31). The source's generation, which every change
     * advances, and its count of shown rows. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: generation needs its two outputs), unchanged.
    gf_status (GF_CALL* generation)(gf_object source, uint64_t* generation, uint64_t* count);
    /* Since 1.2 (N31). The cell (row, column) of the shown rows, read at
     * generation, as properties.get gives a value; a generation that is no
     * longer the source's gives GF_STATUS_STALE_HANDLE, a row or column
     * past the source's GF_STATUS_NOT_FOUND, and a cell the provider gave
     * no valid answer for GF_STATUS_INVALID_STATE with its error as the
     * failure's message. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state (The
    // ic: cell (row, column) of the shown rows, read at generation, as properties.get gives a value; a generation that
    // ic: is no longer the source's gives GF_STATUS_STALE_HANDLE, a row or column past the source's
    // ic: GF_STATUS_NOT_FOUND, and a cell the provider gave no valid answer for GF_STATUS_INVALID_STATE with its error
    // ic: as the failure's message.), unchanged; not_found (The cell (row, column) of the shown rows, read at
    // ic: generation, as properties.get gives a value; a generation that is no longer the source's gives
    // ic: GF_STATUS_STALE_HANDLE, a row or column past the source's GF_STATUS_NOT_FOUND, and a cell the provider gave
    // ic: no valid answer for GF_STATUS_INVALID_STATE with its error as the failure's message.), unchanged.
    gf_status (GF_CALL* read_cell)(gf_object source, uint64_t generation, uint64_t row, uint64_t column,
                                   gf_value* value, char* text_buffer, uint64_t text_capacity,
                                   uint64_t* text_required);
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

// ic: gf_abi::subsurfaces: generated from include/gui_forms/c/gf_abi_subsurfaces.ic.toml by tools/ic_declarations.py;
// ic: edit the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_subsurfaces_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Gives host content to show; a null content stops the sub-surface and
     * leaves the region empty. A running sub-surface is stopped and its
     * thread joined first. The library takes content->build only when this
     * returns GF_STATUS_OK. A host that is not a sub_surface_host gives
     * GF_STATUS_WRONG_HANDLE_KIND. UI thread. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: a gf_subsurface_content needs struct_size of at least 40 and opaque 0 or 1), unchanged;
    // ic: wrong_handle_kind (A host that is not a sub_surface_host gives GF_STATUS_WRONG_HANDLE_KIND.), unchanged.
    gf_status (GF_CALL* set_content)(gf_object host, const gf_subsurface_content* content);
    /* Hands root, a control created on the calling thread, to the
     * sub-surface whose build callback is running there; the sub-surface
     * holds it from then on, and the caller keeps its own reference. Only
     * inside a build callback, elsewhere GF_STATUS_INVALID_STATE; a second
     * call replaces the first. */
    // ic: Fails disposed (code: the object has been disposed), unchanged; invalid_state (Only inside a build callback,
    // ic: elsewhere GF_STATUS_INVALID_STATE; a second call replaces the first.), unchanged.
    gf_status (GF_CALL* set_root)(gf_object root);
    /* The host's state; GF_SUBSURFACE_STATE_STOPPED while it has no
     * thread. UI thread. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(state),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: state_state, then the state
    // ic: refusal. Fails wrong_handle_kind (code: the object is not a sub_surface_host), unchanged.
    gf_status (GF_CALL* state)(gf_object host, gf_subsurface_state* state);
    /* The content epoch of the newest configuration the host sent its
     * sub-surface (each frame the sub-surface shows was produced at one);
     * zero before the first. UI thread. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(epoch),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: content_epoch_epoch, then
    // ic: the state refusal. Fails wrong_handle_kind (code: the object is not a sub_surface_host), unchanged.
    gf_status (GF_CALL* content_epoch)(gf_object host, uint64_t* epoch);
    /* Why the sub-surface failed (GF_SUBSURFACE_STATE_FAILED), as text;
     * empty in every other state. UI thread. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails wrong_handle_kind
    // ic: (code: the object is not a sub_surface_host), unchanged.
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
 * (packages may be null when the count is zero; the stride is the first
 * package's struct_size), compiled after the base
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

/*
 * Since 1.5. The program's identity, as the business card shows it: the
 * product, its version, the vendor (display text, printed large), the
 * website (empty or an https:// address), the copyright and the credits.
 * An input record: struct_size at least the 1.5 size, reserved zero; the
 * text is UTF-8, read during the call only.
 */
typedef struct gf_program_identity {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view product;
    gf_utf8_view version;
    gf_utf8_view vendor;
    gf_utf8_view website;
    gf_utf8_view copyright;
    gf_utf8_view credits;
} gf_program_identity;

// ic: gf_abi::application: generated from include/gui_forms/c/gf_abi_application.ic.toml by tools/ic_declarations.py;
// ic: edit the declaration, not these lines. Thread: ui, unless a call says otherwise.
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
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT for an unknown base or source, a non-zero reserved field,
    // ic: or packages null with a non-zero count.), unchanged; invalid_state (GF_STATUS_INVALID_STATE, making nothing
    // ic: and leaving the findings, while an application is current or while the library holds objects made without
    // ic: one;), unchanged.
    gf_status (GF_CALL* create)(const gf_application_config* config, gf_theme_report* report);
    /* Ends the application's time as current. windows_alive receives the
     * number of its windows still alive. GF_STATUS_INVALID_STATE, changing
     * nothing, while any is; once closed, the theme is the built-in one
     * again (as themes.reset), windows made afterwards use the process
     * default and create may make another application. Closing
     * with no application accepts and does nothing. */
    // ic: Requires not_null(windows_alive), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_state
    // ic: (GF_STATUS_INVALID_STATE, changing nothing, while any is; once closed, the theme is the built-in one again
    // ic: (as themes.reset), windows made afterwards use the process default and create may make another application.),
    // ic: unchanged.
    gf_status (GF_CALL* close)(uint64_t* windows_alive);
    /* Whether an application is current. */
    // ic: Requires not_null(state), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* state)(gf_application_state* state);
    /* Since 1.5. Sets the program's identity (set_program_identity), read by
     * every business card; validated whole, so a refusal leaves the
     * previous identity. UI thread. GF_STATUS_INVALID_ARGUMENT for a null
     * or short record, a non-zero reserved field, text that is not UTF-8 or
     * a website that is neither empty nor an https:// address. */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT for a null or short record, a non-zero reserved field,
    // ic: text that is not UTF-8 or a website that is neither empty nor an https:// address.), unchanged.
    gf_status (GF_CALL* set_program_identity)(const gf_program_identity* identity);
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
/* Since 1.2 (10 and 11 are C++ only). A session of the window's drag engine
 * is planning, dragging or settling on the control object or one of its
 * descendants. */
#define GF_RULE_CONDITION_DRAGGING 12u
/* Since 1.2. The control object is the container of the running drag's
 * selected candidate (the group container its client named). */
#define GF_RULE_CONDITION_DROP_CANDIDATE 13u

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
/* Since 1.2. Enters the control target's keyboard drag mode, so a program
 * binds its own move key; a control that declares no drag, or a window
 * already running one, is left as it is. */
#define GF_RULE_ACTION_BEGIN_KEYBOARD_DRAG 6u

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
 * action_count actions (1 to 8), each array stepped by its first element's
 * struct_size; add copies them. A SET action's value is converted to its
 * property's type by add, which refuses one that does not convert
 * (GF_STATUS_INVALID_ARGUMENT). Fields a kind does not read are zero.
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

// ic: gf_abi::rules: generated from include/gui_forms/c/gf_abi_rules.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. Thread: ui, unless a call says otherwise. Identity: generational; a stale one:
// ic: returns GF_STATUS_STALE_HANDLE, unchanged.
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
    // ic: Fails disposed (GF_STATUS_INVALID_ARGUMENT for a malformed record (an unknown kind or flag, a missing key,
    // ic: object, property or callback, a collection value, nonzero reserved fields), GF_STATUS_WRONG_HANDLE_KIND when
    // ic: window is not a window or an object is not of the kind its field names, GF_STATUS_DISPOSED for a disposed
    // ic: window or object, GF_STATUS_NOT_FOUND for an interaction the source's type does not raise or a), unchanged;
    // ic: invalid_argument (GF_STATUS_INVALID_ARGUMENT for a malformed record (an unknown kind or flag, a missing key,
    // ic: object, property or callback, a collection value, nonzero reserved fields), GF_STATUS_WRONG_HANDLE_KIND when
    // ic: window is not a window or an object is not of the kind its field names, GF_STATUS_DISPOSED for a disposed
    // ic: window or object, GF_STATUS_NOT_FOUND for an interaction the source's type does not raise or a), unchanged;
    // ic: invalid_state (GF_STATUS_INVALID_ARGUMENT for a malformed record (an unknown kind or flag, a missing key,
    // ic: object, property or callback, a collection value, nonzero reserved fields), GF_STATUS_WRONG_HANDLE_KIND when
    // ic: window is not a window or an object is not of the kind its field names, GF_STATUS_DISPOSED for a disposed
    // ic: window or object, GF_STATUS_NOT_FOUND for an interaction the source's type does not raise or a), unchanged;
    // ic: limit_exceeded (GF_STATUS_INVALID_ARGUMENT for a malformed record (an unknown kind or flag, a missing key,
    // ic: object, property or callback, a collection value, nonzero reserved fields), GF_STATUS_WRONG_HANDLE_KIND when
    // ic: window is not a window or an object is not of the kind its field names, GF_STATUS_DISPOSED for a disposed
    // ic: window or object, GF_STATUS_NOT_FOUND for an interaction the source's type does not raise or a), unchanged;
    // ic: not_found (GF_STATUS_INVALID_ARGUMENT for a malformed record (an unknown kind or flag, a missing key, object,
    // ic: property or callback, a collection value, nonzero reserved fields), GF_STATUS_WRONG_HANDLE_KIND when window
    // ic: is not a window or an object is not of the kind its field names, GF_STATUS_DISPOSED for a disposed window or
    // ic: object, GF_STATUS_NOT_FOUND for an interaction the source's type does not raise or a), unchanged;
    // ic: wrong_handle_kind (GF_STATUS_INVALID_ARGUMENT for a malformed record (an unknown kind or flag, a missing key,
    // ic: object, property or callback, a collection value, nonzero reserved fields), GF_STATUS_WRONG_HANDLE_KIND when
    // ic: window is not a window or an object is not of the kind its field names, GF_STATUS_DISPOSED for a disposed
    // ic: window or object, GF_STATUS_NOT_FOUND for an interaction the source's type does not raise or a), unchanged.
    // ic: Returns gf_rule_token, owned by the caller.
    gf_status (GF_CALL* add)(gf_object window, const gf_rule* rule, gf_rule_token* token);
    /* Ends token's rule when it is armed and frees the token. */
    gf_status (GF_CALL* remove)(gf_rule_token token);
    /* token's rule: GF_RULE_STATE_ARMED or GF_RULE_STATE_STALE. */
    // ic: Requires not_null(state), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* state)(gf_rule_token token, gf_rule_state* state);
} gf_rules_table;

/*
 * GF_TABLE_DATA_GRID (since 1.2): a data_grid_view's columns, rows, cells,
 * current cell, selection and edit (C++ DataGridView). Its events (cell
 * value changed, current cell changed, selection changed, cell begin and
 * end edit, validating, user added row and the others the schema lists)
 * are interactions: events.subscribe takes their names on the grid. Its
 * other settings (selection_mode, multi_select, allow_user_to_add_rows,
 * read_only, ...) are properties (GF_TABLE_PROPERTIES). Another control
 * gives GF_STATUS_WRONG_HANDLE_KIND. UI thread only.
 *
 * Columns are numbered by their index, which display order does not
 * change. Rows are named two ways: a row ID, the stable identity the grid
 * keeps its current cell and selection by, and a position, its place in
 * the order shown now (0 at the top), which inserting, erasing or sorting
 * rows changes. row_id and row_position convert. Cells are named by row ID
 * and column. The rows a program edits here are the grid's own (unbound)
 * rows, the C++ `rows` collection; while the grid shows a binding source's
 * records (its data_source) or virtual rows, the row edits give
 * GF_STATUS_INVALID_STATE and the cells read and write the source's
 * values. With allow_user_to_add_rows the grid also shows a new row after
 * its rows: row_count counts it and names its position, and it is no row
 * of the collection (positions for the row edits stop before it).
 *
 * Arrays of records the caller passes (rows, cells, values) have the
 * stride of their first element's struct_size, the caller's sizeof, which
 * every element repeats; each element is at least its 1.1 size (a
 * gf_value its 1.0 size).
 */

/* gf_data_grid_column_kind: what a column's cells are. */
typedef uint32_t gf_data_grid_column_kind;
#define GF_DATA_GRID_COLUMN_KIND_TEXT 0u
#define GF_DATA_GRID_COLUMN_KIND_CHECK_BOX 1u
#define GF_DATA_GRID_COLUMN_KIND_COMBO_BOX 2u
#define GF_DATA_GRID_COLUMN_KIND_BUTTON 3u
#define GF_DATA_GRID_COLUMN_KIND_IMAGE 4u
#define GF_DATA_GRID_COLUMN_KIND_LINK 5u

/* The most rows one call inserts, appends or reads, and the most choices a
 * combo box column has. */
#define GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL 65536u
#define GF_DATA_GRID_MAXIMUM_COLUMN_ITEMS 4096u

/*
 * A column. As input (insert_column), the caller sets struct_size to at
 * least the 1.1 size and reserved to zero, and the library reads the record
 * and what it references during the call: name is the column's identity
 * (unique in the grid, not empty, without '.'); header_text the header's
 * text; kind its cells' kind; value_kind the kind a committed text edit
 * converts to (BOOLEAN, SIGNED, UNSIGNED, NUMBER or TEXT; NULL takes TEXT);
 * read_only 0 or 1; hidden 0 or 1 (a hidden column keeps its place,
 * width and values, but is not laid out, painted, hit, moved to by keys,
 * included in selection ranges or published); width and
 * minimum_width in logical units (zero takes the defaults, 100 and 20; a
 * width is at least the minimum, the minimum at least 1);
 * data_property_name the binding source field the column shows (may be
 * empty); items and item_count a combo box column's choices (null and zero
 * for other kinds). As output (column_at) the record is filled, its texts
 * copied into the caller's buffer, items null and item_count the number of
 * choices (column_item reads them).
 */
typedef struct gf_data_grid_column {
    uint32_t struct_size;
    gf_data_grid_column_kind kind;
    gf_value_kind value_kind;
    uint32_t read_only;
    uint32_t hidden;
    uint32_t reserved;
    double width;
    double minimum_width;
    gf_utf8_view name;
    gf_utf8_view header_text;
    gf_utf8_view data_property_name;
    const gf_utf8_view* items;
    uint64_t item_count;
} gf_data_grid_column;

/*
 * A row the caller gives the grid, read during the call: its stable id
 * (unique in the grid; empty takes a generated one, "row.1", "row.2", ...),
 * read_only (0 or 1), its header's text, and cell_count values, one per
 * column in column order (fewer leave the rest empty; more than the columns
 * give GF_STATUS_INVALID_ARGUMENT; cells may be null when the count is
 * zero). A value's text is copied.
 */
typedef struct gf_data_grid_row {
    uint32_t struct_size;
    uint32_t read_only;
    gf_utf8_view stable_id;
    gf_utf8_view header_text;
    const gf_value* cells;
    uint64_t cell_count;
} gf_data_grid_row;

/* A cell by row position and column index. The record's layout is fixed:
 * an array of them has the stride sizeof(gf_data_grid_cell). */
typedef struct gf_data_grid_cell {
    uint64_t row;
    uint64_t column;
} gf_data_grid_cell;

// ic: gf_abi::data_grid: generated from include/gui_forms/c/gf_abi_data_grid.ic.toml by tools/ic_declarations.py; edit
// ic: the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_data_grid_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* The number of columns. */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* column_count)(gf_object grid, uint64_t* count);
    /* Column index (below the count) into column, an output record of at
     * least the 1.1 size; its name, header text and data property name are
     * copied into text_buffer, one after another, and the views point
     * there. text_required receives their total size, also with
     * GF_STATUS_BUFFER_TOO_SMALL, which leaves the record unwritten.
     * GF_STATUS_NOT_FOUND past the columns. */
    // ic: Fails buffer_too_small (Column index (below the count) into column, an output record of at least the 1.1
    // ic: size; its name, header text and data property name are copied into text_buffer, one after another, and the
    // ic: views point there. text_required receives their total size, also with GF_STATUS_BUFFER_TOO_SMALL, which
    // ic: leaves the record unwritten.), unchanged; invalid_argument (code: column_at needs a column record of at least
    // ic: the 1.1 size, a text size output, and a ), unchanged; not_found (GF_STATUS_NOT_FOUND past the columns.),
    // ic: unchanged.
    gf_status (GF_CALL* column_at)(gf_object grid, uint64_t index, gf_data_grid_column* column, char* text_buffer,
                                   uint64_t text_capacity, uint64_t* text_required);
    /* A combo box column's choice item (below its item_count), as text.
     * GF_STATUS_NOT_FOUND past the columns or the choices. */
    // ic: Fails not_found (GF_STATUS_NOT_FOUND past the columns or the choices.), unchanged.
    gf_status (GF_CALL* column_item)(gf_object grid, uint64_t column, uint64_t item, char* buffer,
                                     uint64_t capacity, uint64_t* required);
    /* Inserts column at index, at most the count (the count appends). Later
     * columns move up one index, the rows' values move with their columns
     * and the new column's cells start empty. Ends any edit and clears the
     * selection, the current cell, and the error text, styles and sort the
     * grid keeps by column. A malformed column, a repeated name or an index
     * past the count gives GF_STATUS_INVALID_ARGUMENT, changing nothing. */
    // ic: Fails invalid_argument (A malformed column, a repeated name or an index past the count gives
    // ic: GF_STATUS_INVALID_ARGUMENT, changing nothing.), unchanged; limit_exceeded (code: a combo box column has at
    // ic: most 4096 choices), unchanged.
    gf_status (GF_CALL* insert_column)(gf_object grid, uint64_t index, const gf_data_grid_column* column);
    /* Removes the column at index with the rows' values in it, ending the
     * edit and the selection as insert_column does. GF_STATUS_NOT_FOUND
     * past the columns. */
    // ic: Fails not_found (GF_STATUS_NOT_FOUND past the columns.), unchanged.
    gf_status (GF_CALL* remove_column)(gf_object grid, uint64_t index);
    /* The index of the column named name; GF_STATUS_NOT_FOUND for none. */
    // ic: Requires not_null(index), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails not_found
    // ic: (GF_STATUS_NOT_FOUND for none.), unchanged.
    gf_status (GF_CALL* column_index)(gf_object grid, gf_utf8_view name, uint64_t* index);
    /* Change one setting of the column at column, as insert_column reads
     * it; GF_STATUS_NOT_FOUND past the columns. A width below the column's
     * minimum gives GF_STATUS_INVALID_ARGUMENT; an auto-sized column takes
     * its automatic width again at the next layout. Making a column
     * read-only discards an edit in it. Hiding the current cell's column
     * discards an edit in it and moves the current cell to the next shown
     * column of its row (the previous one past the last), or clears it when
     * no column is shown. */
    // ic: Fails invalid_argument (A width below the column's minimum gives GF_STATUS_INVALID_ARGUMENT; an auto-sized
    // ic: column takes its automatic width again at the next layout.), unchanged; not_found (GF_STATUS_NOT_FOUND past
    // ic: the columns.), unchanged.
    gf_status (GF_CALL* set_column_header_text)(gf_object grid, uint64_t column, gf_utf8_view text);
    // ic: Fails not_found (code: the column is past the grid's columns), unchanged.
    gf_status (GF_CALL* set_column_width)(gf_object grid, uint64_t column, double width);
    // ic: Fails invalid_argument (code: read_only is 0 or 1), unchanged; not_found (code: the column is past the grid's
    // ic: columns), unchanged.
    gf_status (GF_CALL* set_column_read_only)(gf_object grid, uint64_t column, uint32_t read_only);
    // ic: Fails invalid_argument (code: hidden is 0 or 1), unchanged; not_found (code: the column is past the grid's
    // ic: columns), unchanged.
    gf_status (GF_CALL* set_column_hidden)(gf_object grid, uint64_t column, uint32_t hidden);
    /* Every row shown (count), the new row included, and the new row's
     * position (new_row; -1 while none is shown). */
    // ic: Fails invalid_argument (code: row_count needs a count and a new row output), unchanged.
    gf_status (GF_CALL* row_count)(gf_object grid, uint64_t* count, int64_t* new_row);
    /* Insert count rows at index (at most the collection's count), or
     * append them (first receives the first one's position), in one edit.
     * All or nothing: a repeated or malformed stable id, a malformed value,
     * more values than columns or an index past the rows gives
     * GF_STATUS_INVALID_ARGUMENT and adds none; more than
     * GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL gives GF_STATUS_LIMIT_EXCEEDED.
     * The grid makes one row of its own per row given, the copy the values
     * need. rows may be null when count is zero. */
    // ic: Fails invalid_argument (All or nothing: a repeated or malformed stable id, a malformed value, more values
    // ic: than columns or an index past the rows gives GF_STATUS_INVALID_ARGUMENT and adds none; more than
    // ic: GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL gives GF_STATUS_LIMIT_EXCEEDED.), unchanged; limit_exceeded (All or
    // ic: nothing: a repeated or malformed stable id, a malformed value, more values than columns or an index past the
    // ic: rows gives GF_STATUS_INVALID_ARGUMENT and adds none; more than GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL gives
    // ic: GF_STATUS_LIMIT_EXCEEDED.), unchanged.
    gf_status (GF_CALL* insert_rows)(gf_object grid, uint64_t index, const gf_data_grid_row* rows, uint64_t count);
    // ic: Requires not_null(first), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails limit_exceeded (code: one
    // ic: call adds at most 65536 rows), unchanged.
    gf_status (GF_CALL* append_rows)(gf_object grid, const gf_data_grid_row* rows, uint64_t count,
                                     uint64_t* first);
    /* Erases count rows from index; a range past the collection gives
     * GF_STATUS_INVALID_ARGUMENT. Erasing the current cell's row makes the
     * row that takes its position, or the new last row, current. */
    // ic: Fails invalid_argument (Erases count rows from index; a range past the collection gives
    // ic: GF_STATUS_INVALID_ARGUMENT.), unchanged.
    gf_status (GF_CALL* erase_rows)(gf_object grid, uint64_t index, uint64_t count);
    /* Replaces the row at index (below the collection's count) with row,
     * whose stable id may be the replaced row's or one no other row has. */
    // ic: Requires not_null(row), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires row_size >=
    // ic: row_size_1_1, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: replace_row_row, then replace_row_row_size.
    gf_status (GF_CALL* replace_row)(gf_object grid, uint64_t index, const gf_data_grid_row* row);
    /* Removes every row of any source (a data source or virtual provider is
     * let go) and returns the grid to its own rows. */
    gf_status (GF_CALL* clear_rows)(gf_object grid);
    /* The ID of the row at position, copied into buffer (not terminated);
     * GF_STATUS_NOT_FOUND past the rows. */
    // ic: Fails not_found (GF_STATUS_NOT_FOUND past the rows.), unchanged.
    gf_status (GF_CALL* row_id)(gf_object grid, uint64_t position, char* buffer, uint64_t capacity,
                                uint64_t* required);
    /* The position of the row with ID row_id; GF_STATUS_NOT_FOUND for none. */
    // ic: Requires not_null(position), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails not_found
    // ic: (GF_STATUS_NOT_FOUND for none.), unchanged.
    gf_status (GF_CALL* row_position)(gf_object grid, gf_utf8_view row_id, uint64_t* position);
    /* A cell's value, as properties.get gives one. A row ID naming no row
     * or a column past the columns gives GF_STATUS_NOT_FOUND, here and in
     * the other cell slots. as_kind NULL gives the value as stored; another
     * kind gives it converted (BindingValue's conversion: a number from its
     * text, text from a number, ...), or GF_STATUS_INVALID_ARGUMENT, with
     * value unwritten, when it has no form of that kind. An empty cell is
     * NULL either way. */
    // ic: Fails invalid_argument (A row ID naming no row or a column past the columns gives GF_STATUS_NOT_FOUND, here
    // ic: and in the other cell slots. as_kind NULL gives the value as stored; another kind gives it converted
    // ic: (BindingValue's conversion: a number from its text, text from a number, ...), or GF_STATUS_INVALID_ARGUMENT,
    // ic: with value unwritten, when it has no form of that kind.), unchanged; not_found (A row ID naming no row or a
    // ic: column past the columns gives GF_STATUS_NOT_FOUND, here and in the other cell slots. as_kind NULL gives the
    // ic: value as stored; another kind gives it converted (BindingValue's conversion: a number from its text, text
    // ic: from a number, ...), or GF_STATUS_INVALID_ARGUMENT, with value unwritten, when it has no form of that kind.),
    // ic: unchanged.
    gf_status (GF_CALL* cell_value)(gf_object grid, gf_utf8_view row_id, uint64_t column, gf_value_kind as_kind,
                                    gf_value* value, char* text_buffer, uint64_t text_capacity,
                                    uint64_t* text_required);
    /* Stores value in a cell from the program (read-only does not apply),
     * as given, raising cell_value_changed when it differs. accepted
     * receives 0 when a binding source or virtual provider refused it. */
    // ic: Requires not_null(accepted), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails not_found (code: the
    // ic: column is past the grid's columns), unchanged.
    gf_status (GF_CALL* set_cell_value)(gf_object grid, gf_utf8_view row_id, uint64_t column, const gf_value* value,
                                        uint32_t* accepted);
    /* The text a cell shows: its value formatted (cell_formatting and the
     * cell's style), a button or link column's text for an empty cell. */
    // ic: Fails not_found (code: the column is past the grid's columns), unchanged.
    gf_status (GF_CALL* cell_text)(gf_object grid, gf_utf8_view row_id, uint64_t column, char* buffer,
                                   uint64_t capacity, uint64_t* required);
    /* Reads one column's values in row_count rows from position first_row
     * into values, an array of row_count records (the array rule above;
     * fields past the 1.0 size are left alone), their text copied into
     * text_buffer one after another, each value's view pointing there.
     * text_required receives the total text size, also with
     * GF_STATUS_BUFFER_TOO_SMALL, which writes no value. The grid's own rows
     * are read in place, with no allocation per cell; a binding source's or
     * virtual provider's values are asked of it as cell_value does (twice
     * when the call first measures the text). A range past the rows gives
     * GF_STATUS_NOT_FOUND; more than GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL
     * rows GF_STATUS_LIMIT_EXCEEDED. */
    // ic: Requires not_null(text_required) and (not_null(text_buffer) or text_capacity == 0), else returns
    // ic: GF_STATUS_INVALID_ARGUMENT, unchanged. Fails buffer_too_small (Reads one column's values in row_count rows
    // ic: from position first_row into values, an array of row_count records (the array rule above; fields past the 1.0
    // ic: size are left alone), their text copied into text_buffer one after another, each value's view pointing there.
    // ic: text_required receives the total text size, also with GF_STATUS_BUFFER_TOO_SMALL, which writes no value.),
    // ic: unchanged; limit_exceeded (A range past the rows gives GF_STATUS_NOT_FOUND; more than
    // ic: GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL rows GF_STATUS_LIMIT_EXCEEDED.), unchanged; not_found (A range past the
    // ic: rows gives GF_STATUS_NOT_FOUND; more than GF_DATA_GRID_MAXIMUM_ROWS_PER_CALL rows GF_STATUS_LIMIT_EXCEEDED.),
    // ic: unchanged; unsupported_capability (code: a cell's value kind has no gf_value form in this library),
    // ic: unchanged.
    gf_status (GF_CALL* read_cells)(gf_object grid, uint64_t column, uint64_t first_row, uint64_t row_count,
                                    gf_value* values, char* text_buffer, uint64_t text_capacity,
                                    uint64_t* text_required);
    /* The current cell's position and column; present receives 0, and row
     * and column are left, when there is none. */
    // ic: Fails invalid_argument (code: current_cell needs present, row and column outputs), unchanged.
    gf_status (GF_CALL* current_cell)(gf_object grid, uint32_t* present, uint64_t* row, uint64_t* column);
    /* Moves the current cell to row_id's row and column (an empty row_id
     * for none), committing any edit first, and selects it alone. moved
     * receives 0, changing nothing, when the edit or the row's validation
     * refuses. A cell in a hidden column gives GF_STATUS_INVALID_STATE.
     * The grid's own settings are schema properties (properties.set and
     * get): read_only, selection_mode (a data_grid_view_selection_mode choice),
     * multi_select and allow_user_to_add_rows. */
    // ic: Requires not_null(moved), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_state (A cell in
    // ic: a hidden column gives GF_STATUS_INVALID_STATE.), unchanged; not_found (code: the column is past the grid's
    // ic: columns), unchanged.
    gf_status (GF_CALL* set_current_cell)(gf_object grid, gf_utf8_view row_id, uint64_t column, uint32_t* moved);
    /* The selected rows' positions in order (full-row selections, or in
     * cell mode the rows whose every cell is selected) into the caller's
     * array of capacity elements; count receives their number, also with
     * GF_STATUS_BUFFER_TOO_SMALL, which writes none. positions may be null
     * when capacity is zero. */
    // ic: Requires not_null(count) and (not_null(positions) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (The selected rows' positions in order (full-row selections, or in cell
    // ic: mode the rows whose every cell is selected) into the caller's array of capacity elements; count receives
    // ic: their number, also with GF_STATUS_BUFFER_TOO_SMALL, which writes none. positions may be null when capacity is
    // ic: zero.), unchanged.
    gf_status (GF_CALL* selected_rows)(gf_object grid, uint64_t* positions, uint64_t capacity, uint64_t* count);
    /* The selected cells by row then column, as selected_rows gives rows. */
    // ic: Requires not_null(count) and (not_null(cells) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (code: the array is smaller than the selected cells), unchanged.
    gf_status (GF_CALL* selected_cells)(gf_object grid, gf_data_grid_cell* cells, uint64_t capacity,
                                        uint64_t* count);
    /* Selects (1) or deselects (0) a row, or a cell (in full-row mode, its
     * row). Without multi_select, selecting clears the rest first. Raises
     * selection_changed once when the selection changes. */
    // ic: Fails invalid_argument (code: selected is 0 or 1), unchanged; not_found (code: no row of the grid has that
    // ic: row ID), unchanged.
    gf_status (GF_CALL* set_row_selected)(gf_object grid, gf_utf8_view row_id, uint32_t selected);
    // ic: Fails invalid_argument (code: selected is 0 or 1), unchanged; not_found (code: the column is past the grid's
    // ic: columns), unchanged.
    gf_status (GF_CALL* set_cell_selected)(gf_object grid, gf_utf8_view row_id, uint64_t column,
                                           uint32_t selected);
    gf_status (GF_CALL* clear_selection)(gf_object grid);
    /* Selects every cell (or row); does nothing without multi_select. */
    gf_status (GF_CALL* select_all)(gf_object grid);
    /* Puts the current text cell in edit, or opens the current combo box
     * cell's choices; began receives 0 for no current cell, a read-only
     * cell, a check box or button cell, or a cancelled cell_begin_edit.
     * select_all (0 or 1) selects the editor's text. */
    // ic: Fails invalid_argument (code: begin_edit takes select_all 0 or 1 and a began output), unchanged.
    gf_status (GF_CALL* begin_edit)(gf_object grid, uint32_t select_all, uint32_t* began);
    /* Commits the edit in progress (cell_validating, conversion, the
     * value): committed receives 1 when it committed or no edit was in
     * progress, 0 when the cell stays in edit, marked invalid. */
    // ic: Requires not_null(committed), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* end_edit)(gf_object grid, uint32_t* committed);
    /* Discards the edit in progress. */
    gf_status (GF_CALL* cancel_edit)(gf_object grid);
} gf_data_grid_table;

/*
 * GF_TABLE_DOCKING (since 1.2): a dock workspace's panels and layout (C++
 * DockWorkspace), for a program whose own docking vocabulary maps onto
 * GUI.Forms docking (a Windows Forms docking library's shim, for example).
 * The workspace is a dock_workspace object (objects.create); a dock_site
 * made with objects.create_with, the workspace as its owner, shows the
 * main site in a window, and the workspace opens one floating window per
 * floating site once attach_windows binds the running window. Every edit
 * the user makes in a site (dragging with the drop tiles, the caption's
 * pin and close buttons, the tab strips, the seams, auto-hide flyouts,
 * floating windows) goes through the same layout. The workspace's
 * interactions (events.subscribe): layout_changed (DockLayoutChange, once
 * per applied transaction or restore), activation_changed and
 * document_closing (mutable: set cancel to keep the documents). Another
 * object gives GF_STATUS_WRONG_HANDLE_KIND. UI thread only.
 *
 * Panels are named by their id, the program's stable text (any non-empty
 * UTF-8; a saved layout records it). Stacks, splits and the document area
 * are nodes named by number, and floating sites by number, as the layout
 * assigns them; zero is none. Numbers stay stable across edits and are
 * saved with the layout.
 */

/* gf_dock_panel_kind: a tool panel or a document. */
typedef uint32_t gf_dock_panel_kind;
#define GF_DOCK_PANEL_KIND_TOOL 0u
#define GF_DOCK_PANEL_KIND_DOCUMENT 1u

/* gf_dock_placement: where a panel is. HIDDEN keeps its place for a later
 * show; ABSENT is never placed, or closed. */
typedef uint32_t gf_dock_placement;
#define GF_DOCK_PLACEMENT_DOCKED 0u
#define GF_DOCK_PLACEMENT_FLOATING 1u
#define GF_DOCK_PLACEMENT_AUTO_HIDDEN 2u
#define GF_DOCK_PLACEMENT_HIDDEN 3u
#define GF_DOCK_PLACEMENT_ABSENT 4u

/* gf_dock_side: a side of a node or of the main site. NONE only in output. */
typedef uint32_t gf_dock_side;
#define GF_DOCK_SIDE_LEFT 0u
#define GF_DOCK_SIDE_TOP 1u
#define GF_DOCK_SIDE_RIGHT 2u
#define GF_DOCK_SIDE_BOTTOM 3u
#define GF_DOCK_SIDE_NONE 4u

/* gf_dock_node_kind: what a node is. NONE only in output. */
typedef uint32_t gf_dock_node_kind;
#define GF_DOCK_NODE_KIND_SPLIT 0u
#define GF_DOCK_NODE_KIND_TOOL_STACK 1u
#define GF_DOCK_NODE_KIND_DOCUMENT_STACK 2u
#define GF_DOCK_NODE_KIND_DOCUMENT_AREA 3u
#define GF_DOCK_NODE_KIND_NONE 4u

/* gf_dock_allowed: the places a panel permits, as flags. DOCKED: a tool
 * stack of the main site; DOCUMENT_AREA: the document area (a tool panel
 * also needs dock_as_document); TABBED: sharing a stack; HIDDEN: the
 * close button and the hide edit hide it (else a close closes it).
 * SIDE_LEFT to SIDE_BOTTOM: the sides of the document area its docked stack
 * may stand on and the window edges it may be auto-hidden to; none of the
 * four set permits every side. CLOSE_BUTTON_HIDDEN (not a place, outside
 * ALL): its caption and tab show no close button. */
typedef uint32_t gf_dock_allowed;
#define GF_DOCK_ALLOWED_DOCKED 0x1u
#define GF_DOCK_ALLOWED_DOCUMENT_AREA 0x2u
#define GF_DOCK_ALLOWED_FLOATING 0x4u
#define GF_DOCK_ALLOWED_AUTO_HIDDEN 0x8u
#define GF_DOCK_ALLOWED_TABBED 0x10u
#define GF_DOCK_ALLOWED_HIDDEN 0x20u
#define GF_DOCK_ALLOWED_CLOSED 0x40u
#define GF_DOCK_ALLOWED_SIDE_LEFT 0x80u
#define GF_DOCK_ALLOWED_SIDE_TOP 0x100u
#define GF_DOCK_ALLOWED_SIDE_RIGHT 0x200u
#define GF_DOCK_ALLOWED_SIDE_BOTTOM 0x400u
#define GF_DOCK_ALLOWED_ALL 0x7ffu
#define GF_DOCK_ALLOWED_CLOSE_BUTTON_HIDDEN 0x800u

/* gf_dock_option_flags: a workspace's options (set_options).
 * FLOATS_SHOWN_WHILE_INACTIVE: floating tool windows stay shown while the
 * application is inactive (else the host hides them then);
 * DOCUMENT_TABS_HIDDEN: document stacks show no tab strip (a single
 * document interface); AUTO_HIDE_NO_HOVER: resting the pointer on a side tab
 * does not open its flyout (a press does); AREA_BACKGROUND: area_background
 * paints the document area in place of the theme's well. */
typedef uint32_t gf_dock_option_flags;
#define GF_DOCK_OPTION_FLOATS_SHOWN_WHILE_INACTIVE 0x1u
#define GF_DOCK_OPTION_DOCUMENT_TABS_HIDDEN 0x2u
#define GF_DOCK_OPTION_AUTO_HIDE_NO_HOVER 0x4u
#define GF_DOCK_OPTION_AREA_BACKGROUND 0x8u
#define GF_DOCK_OPTION_ALL 0xfu

/* gf_dock_window_visibility: one floating window's override of
 * FLOATS_SHOWN_WHILE_INACTIVE. WORKSPACE: the workspace's option; SHOWN:
 * stays shown while the application is inactive; POLICY: the host hides it
 * then. */
typedef uint32_t gf_dock_window_visibility;
#define GF_DOCK_WINDOW_VISIBILITY_WORKSPACE 0u
#define GF_DOCK_WINDOW_VISIBILITY_SHOWN 1u
#define GF_DOCK_WINDOW_VISIBILITY_POLICY 2u

/* gf_dock_edit_kind: one primitive layout edit (C++ DockEdit). */
typedef uint32_t gf_dock_edit_kind;
#define GF_DOCK_EDIT_DOCK_INTO 0u
#define GF_DOCK_EDIT_DOCK_BESIDE 1u
#define GF_DOCK_EDIT_DOCK_AT_EDGE 2u
#define GF_DOCK_EDIT_FLOAT 3u
#define GF_DOCK_EDIT_AUTO_HIDE 4u
#define GF_DOCK_EDIT_UNPIN 5u
#define GF_DOCK_EDIT_HIDE 6u
#define GF_DOCK_EDIT_SHOW 7u
#define GF_DOCK_EDIT_CLOSE 8u
#define GF_DOCK_EDIT_MOVE 9u
#define GF_DOCK_EDIT_SELECT 10u
#define GF_DOCK_EDIT_SET_RATIO 11u
#define GF_DOCK_EDIT_SET_FLOATING_FRAME 12u
/* Since 1.6: the frames presentation (C++ DockEdit's frame edits). */
#define GF_DOCK_EDIT_SET_DOCUMENT_PRESENTATION 13u
#define GF_DOCK_EDIT_SET_FRAME_BOUNDS 14u
#define GF_DOCK_EDIT_SET_FRAME_STATE 15u
#define GF_DOCK_EDIT_SET_FRAME_ICON_SLOT 16u

/* gf_dock_document_presentation (since 1.6): how the main site's document
 * area shows its documents: as tabs in document stacks, or as overlapping
 * document frames inside the area (a multiple document interface). */
typedef uint32_t gf_dock_document_presentation;
#define GF_DOCK_DOCUMENT_PRESENTATION_TABS 0u
#define GF_DOCK_DOCUMENT_PRESENTATION_FRAMES 1u

/* gf_dock_frame_state (since 1.6): a document frame at its bounds,
 * minimized to an icon along an edge of the document area, or filling the
 * area (Windows Forms' FormWindowState for an MDI child). */
typedef uint32_t gf_dock_frame_state;
#define GF_DOCK_FRAME_STATE_NORMAL 0u
#define GF_DOCK_FRAME_STATE_MINIMIZED 1u
#define GF_DOCK_FRAME_STATE_MAXIMIZED 2u

/* gf_dock_frame_arrangement (since 1.6): how arrange_frames lays the
 * frames out (Windows Forms' MdiLayout): cascade, tiled one above another,
 * tiled side by side, or the minimized frames' icons numbered again. */
typedef uint32_t gf_dock_frame_arrangement;
#define GF_DOCK_FRAME_ARRANGEMENT_CASCADE 0u
#define GF_DOCK_FRAME_ARRANGEMENT_TILE_HORIZONTAL 1u
#define GF_DOCK_FRAME_ARRANGEMENT_TILE_VERTICAL 2u
#define GF_DOCK_FRAME_ARRANGEMENT_ARRANGE_ICONS 3u

/* gf_dock_refusal: why the layout refused an edit (C++ DockRefusalReason). */
typedef uint32_t gf_dock_refusal;
#define GF_DOCK_REFUSAL_NONE 0u
#define GF_DOCK_REFUSAL_UNKNOWN_PANEL 1u
#define GF_DOCK_REFUSAL_UNKNOWN_NODE 2u
#define GF_DOCK_REFUSAL_UNKNOWN_SITE 3u
#define GF_DOCK_REFUSAL_NOT_A_STACK 4u
#define GF_DOCK_REFUSAL_NOT_A_SPLIT 5u
#define GF_DOCK_REFUSAL_NOT_AUTO_HIDDEN 6u
#define GF_DOCK_REFUSAL_TARGET_IS_SUBJECT 7u
#define GF_DOCK_REFUSAL_INVALID_RATIO 8u
#define GF_DOCK_REFUSAL_INVALID_FRAME 9u
#define GF_DOCK_REFUSAL_INVALID_EXTENT 10u
#define GF_DOCK_REFUSAL_FLOATING_SITE_LIMIT 11u
#define GF_DOCK_REFUSAL_IDENTITIES_EXHAUSTED 12u
#define GF_DOCK_REFUSAL_NOT_ROOT_LEVEL 13u
#define GF_DOCK_REFUSAL_NOT_IN_MAIN_SITE 14u
#define GF_DOCK_REFUSAL_ALREADY_PLACED 15u
#define GF_DOCK_REFUSAL_NOT_PLACED 16u
#define GF_DOCK_REFUSAL_INVARIANT_VIOLATED 17u
#define GF_DOCK_REFUSAL_PLACEMENT_NOT_ALLOWED 18u
#define GF_DOCK_REFUSAL_TABBING_NOT_ALLOWED 19u
#define GF_DOCK_REFUSAL_KIND_MISMATCH 20u
#define GF_DOCK_REFUSAL_GROUP_NOT_ADMITTED 21u
#define GF_DOCK_REFUSAL_SITE_REFUSES_KIND 22u
#define GF_DOCK_REFUSAL_SITE_REFUSES_GROUP 23u
#define GF_DOCK_REFUSAL_USER_DOCKING_LOCKED 24u

/*
 * A panel to register, read during the call. The caller sets struct_size
 * to at least the 1.1 size and reserved to zero. id is the panel's
 * identity (not empty, unique in the workspace); title what its caption
 * and tab show; kind; allowed (GF_DOCK_ALLOWED_* flags); dock_as_document
 * 0 or 1; default_placement (DOCKED, FLOATING or AUTO_HIDDEN), with
 * default_side, default_ratio (in (0, 1)), default_extent (logical units,
 * positive) and default_frame (desktop logical units, positive size): where
 * a show puts the panel the layout has no memory of; minimum_size its
 * content's smallest size (zero for none); content the control the panel
 * shows (zero for an empty area until set_panel_content gives one). The
 * table holds a reference to content until another replaces it or the
 * workspace is released; closing the panel keeps it for a later show.
 */
typedef struct gf_dock_panel {
    uint32_t struct_size;
    gf_dock_panel_kind kind;
    gf_dock_allowed allowed;
    uint32_t dock_as_document;
    gf_dock_placement default_placement;
    gf_dock_side default_side;
    double default_ratio;
    double default_extent;
    gf_logical_rect default_frame;
    gf_logical_size minimum_size;
    gf_utf8_view id;
    gf_utf8_view title;
    gf_object content;
    uint64_t reserved;
} gf_dock_panel;

/*
 * One edit, read during the call; struct_size at least the 1.1 size,
 * reserved zero. The subject is the panel named panel, or, with
 * subject_is_stack 1, the stack subject_stack (a caption drag moves the
 * stack). The fields each kind reads (C++ DockEdit's factories):
 * DOCK_INTO target_node (a stack or the document area), index;
 * DOCK_BESIDE target_node, side, ratio; DOCK_AT_EDGE side, ratio (against
 * the main site's outer edge); FLOAT frame (desktop logical units);
 * AUTO_HIDE side (the window edge), extent; UNPIN subject_stack; HIDE,
 * SHOW, CLOSE and SELECT panel; MOVE panel, index (within its stack);
 * SET_RATIO target_node (a split), ratio; SET_FLOATING_FRAME site, frame.
 * An index past the end appends.
 *
 * Since 1.6, the frames presentation's edits, which name a document docked
 * in the main site's document area (the layout refuses another with
 * NOT_PLACED or NOT_IN_MAIN_SITE), in either presentation, and which the
 * user docking lock does not refuse: SET_DOCUMENT_PRESENTATION
 * presentation; SET_FRAME_BOUNDS panel, frame (the frame's normal bounds in
 * the document area's coordinates, positive size); SET_FRAME_STATE panel,
 * frame_state (maximizing one frame restores a maximized other and
 * activates the document; minimizing takes the first free icon slot);
 * SET_FRAME_ICON_SLOT panel, index (the slot). SET_DOCUMENT_PRESENTATION
 * and SET_FRAME_STATE read the 1.6 fields: a record shorter than the 1.6
 * size gives GF_STATUS_INVALID_ARGUMENT for them.
 */
typedef struct gf_dock_edit {
    uint32_t struct_size;
    gf_dock_edit_kind kind;
    uint32_t subject_is_stack;
    uint32_t subject_stack;
    uint32_t target_node;
    uint32_t site;
    gf_dock_side side;
    uint32_t index;
    double ratio;
    double extent;
    gf_logical_rect frame;
    gf_utf8_view panel;
    uint64_t reserved;
    /* Since 1.6. */
    gf_dock_document_presentation presentation;
    gf_dock_frame_state frame_state;
} gf_dock_edit;

/*
 * Where a panel is (locate). placement; for a docked, floating or
 * auto-hidden panel, stack (its stack node), index (its place there),
 * stack_kind, selected (1 when it is its stack's selected tab) and
 * stack_panel_count; site the floating site of a floating panel; side, for
 * a panel docked in the main site, the side of the document area its
 * stack stands on (NONE inside the document area), and for an auto-hidden
 * panel the window edge it is pinned to. Fields that do not apply are zero
 * (NONE for side and stack_kind).
 */
typedef struct gf_dock_location {
    uint32_t struct_size;
    gf_dock_placement placement;
    uint32_t stack;
    uint32_t index;
    uint32_t site;
    gf_dock_node_kind stack_kind;
    gf_dock_side side;
    uint32_t selected;
    uint32_t stack_panel_count;
    uint32_t reserved;
} gf_dock_location;

/*
 * A workspace's options, read during the call; struct_size at least the 1.1
 * size, reserved zero. flags (gf_dock_option_flags); area_background a
 * colour as 0xRRGGBBAA, read only with AREA_BACKGROUND.
 */
typedef struct gf_dock_options {
    uint32_t struct_size;
    gf_dock_option_flags flags;
    uint32_t area_background;
    uint32_t reserved;
} gf_dock_options;

/*
 * A document's frame in the frames presentation (frame_memory, since 1.6;
 * C++ DockFrameMemory), an output record: the caller sets struct_size to at
 * least the 1.6 size. present is 1 when the document has a frame (it has
 * shown as a frame, or the program placed one), else 0 with the other
 * fields zero. bounds is the normal frame in the document area's
 * coordinates, kept while the frame is minimized or maximized; state;
 * icon_slot its place in the row of icons while minimized (0 first); order
 * its place in the stack of frames (a higher order is in front; the area's
 * frames are numbered from 1, back to front). A frame is kept while its
 * document is elsewhere, so it comes back where it was.
 */
typedef struct gf_dock_frame_memory {
    uint32_t struct_size;
    uint32_t present;
    gf_logical_rect bounds;
    gf_dock_frame_state state;
    uint32_t icon_slot;
    uint32_t order;
    uint32_t reserved;
} gf_dock_frame_memory;

// ic: gf_abi::docking: generated from include/gui_forms/c/gf_abi_docking.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. States: alive (initial), disposing, disposed. Observed by gf_objects_table.state():
// ic: alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed GF_OBJECT_STATE_DISPOSED. Thread: ui,
// ic: unless a call says otherwise. Identity: generational; a stale one: returns GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_docking_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Registers a panel (gf_dock_panel). The id is a stable id
     * ('.'-separated segments of lowercase ASCII letters and digits joined
     * by single '-') and must be new: another spelling, or an id
     * registered before, gives GF_STATUS_INVALID_ARGUMENT. Registering does
     * not place the panel; a SHOW edit does. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (The
    // ic: id is a stable id ('.'-separated segments of lowercase ASCII letters and digits joined by single '-') and
    // ic: must be new: another spelling, or an id registered before, gives GF_STATUS_INVALID_ARGUMENT.), unchanged.
    gf_status (GF_CALL* register_panel)(gf_object workspace, const gf_dock_panel* panel);
    /* Sets the control a registered panel shows the next time its content
     * is made (zero: an empty area). While the workspace holds content it
     * made for the panel (shown, or hidden since), another control gives
     * GF_STATUS_INVALID_STATE; close the panel first. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state (While
    // ic: the workspace holds content it made for the panel (shown, or hidden since), another control gives
    // ic: GF_STATUS_INVALID_STATE; close the panel first.), unchanged; not_found (code: no panel of that id is
    // ic: registered), unchanged.
    gf_status (GF_CALL* set_panel_content)(gf_object workspace, gf_utf8_view panel, gf_object content);
    /* Sets a registered panel's title. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails not_found (code: no
    // ic: panel of that id is registered), unchanged.
    gf_status (GF_CALL* set_panel_title)(gf_object workspace, gf_utf8_view panel, gf_utf8_view title);
    /* Applies one edit as the program's (gf_dock_edit) in one transaction.
     * A refused edit gives GF_STATUS_INVALID_STATE, leaves the layout
     * unchanged and writes the reason into refusal (may be null); an
     * applied one writes GF_DOCK_REFUSAL_NONE. SHOW, HIDE, CLOSE and
     * FLOAT (once attach_windows has succeeded) go through the workspace's
     * own operations: SHOW of a placed panel activates it; a floated panel
     * opens its window. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: an edit record has at least the 1.1 size and reserved zero), unchanged; invalid_state (A refused edit
    // ic: gives GF_STATUS_INVALID_STATE, leaves the layout unchanged and writes the reason into refusal (may be null);
    // ic: an applied one writes GF_DOCK_REFUSAL_NONE.), unchanged; not_found (code: no panel of that id is registered),
    // ic: unchanged.
    gf_status (GF_CALL* apply_edit)(gf_object workspace, const gf_dock_edit* edit, gf_dock_refusal* refusal);
    /* Where a registered panel is, into location (at least the 1.1 size). */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires
    // ic: not_null(location), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires location_size >=
    // ic: location_size_1_1, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: locate_location, then locate_location_size, then the state refusal. Fails not_found (code: no panel of that
    // ic: id is registered), unchanged.
    gf_status (GF_CALL* locate)(gf_object workspace, gf_utf8_view panel, gf_dock_location* location);
    /* The layout as XML (C++ DockLayout::write_xml) into the caller's
     * buffer; required receives its size, also with
     * GF_STATUS_BUFFER_TOO_SMALL. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails buffer_too_small (The
    // ic: layout as XML (C++ DockLayout::write_xml) into the caller's buffer; required receives its size, also with
    // ic: GF_STATUS_BUFFER_TOO_SMALL.), unchanged.
    gf_status (GF_CALL* save_layout)(gf_object workspace, char* buffer, uint64_t capacity, uint64_t* required);
    /* Restores a layout save_layout wrote. Panels it names that are not
     * registered are kept as placeholders. diagnostic_count (may be null)
     * receives the number of restore diagnostics. Malformed or refused XML
     * gives GF_STATUS_INVALID_ARGUMENT and leaves the layout unchanged. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (Malformed or refused XML gives GF_STATUS_INVALID_ARGUMENT and leaves the layout unchanged.), unchanged.
    gf_status (GF_CALL* load_layout)(gf_object workspace, gf_utf8_view xml, uint64_t* diagnostic_count);
    /* Activates a placed panel (its stack selects it; an auto-hidden one's
     * flyout opens); GF_STATUS_INVALID_STATE when it is not placed. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state
    // ic: (GF_STATUS_INVALID_STATE when it is not placed.), unchanged; not_found (code: no panel of that id is
    // ic: registered), unchanged.
    gf_status (GF_CALL* activate_panel)(gf_object workspace, gf_utf8_view panel);
    /* Binds the window being run (windows.run) as the workspace's main
     * window, so floating sites get floating windows. GF_STATUS_INVALID_STATE
     * while no window is running or before it is ready; again once bound is
     * GF_STATUS_OK. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_state
    // ic: (GF_STATUS_INVALID_STATE while no window is running or before it is ready; again once bound is
    // ic: GF_STATUS_OK.), unchanged; unsupported_capability (code: the host has no runtime windows, or the workspace
    // ic: refused the window), unchanged.
    gf_status (GF_CALL* attach_windows)(gf_object workspace);
    /* The main site's document area node. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(node),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: document_area_node, then the
    // ic: state refusal.
    gf_status (GF_CALL* document_area)(gf_object workspace, uint32_t* node);
    /* The id of the panel at index in stack into the caller's buffer;
     * required receives its size. GF_STATUS_NOT_FOUND past the stack's
     * panels; another node than a stack gives GF_STATUS_INVALID_ARGUMENT. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_NOT_FOUND past the stack's panels; another node than a stack gives GF_STATUS_INVALID_ARGUMENT.),
    // ic: unchanged; not_found (GF_STATUS_NOT_FOUND past the stack's panels; another node than a stack gives
    // ic: GF_STATUS_INVALID_ARGUMENT.), unchanged.
    gf_status (GF_CALL* panel_at)(gf_object workspace, uint32_t stack, uint64_t index, char* buffer,
                                  uint64_t capacity, uint64_t* required);
    /* The floating sites' numbers, in the layout's order, into sites
     * (capacity entries); count receives how many there are, also with
     * GF_STATUS_BUFFER_TOO_SMALL. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(count)
    // ic: and (not_null(sites) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several
    // ic: refusals apply: floating_sites_sites, then the state refusal. Fails buffer_too_small (The floating sites'
    // ic: numbers, in the layout's order, into sites (capacity entries); count receives how many there are, also with
    // ic: GF_STATUS_BUFFER_TOO_SMALL.), unchanged.
    gf_status (GF_CALL* floating_sites)(gf_object workspace, uint32_t* sites, uint64_t capacity, uint64_t* count);
    /* A floating site's frame (its window's client area, desktop logical
     * units) and its first stack (zero when it has none). */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: floating_site needs its outputs), unchanged; not_found (code: no floating site of that number),
    // ic: unchanged.
    gf_status (GF_CALL* floating_site)(gf_object workspace, uint32_t site, gf_logical_rect* frame,
                                       uint32_t* first_stack);
    /* Replaces a registered panel's restrictions, defaults, minimum size,
     * close button and title (gf_dock_panel); content is not read
     * (set_panel_content). Where the panel stands is unchanged; later edits
     * obey the new restrictions. An unregistered id gives
     * GF_STATUS_NOT_FOUND; another kind than it was registered with gives
     * GF_STATUS_INVALID_ARGUMENT. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument (An
    // ic: unregistered id gives GF_STATUS_NOT_FOUND; another kind than it was registered with gives
    // ic: GF_STATUS_INVALID_ARGUMENT.), unchanged; invalid_state (code: the workspace refused the descriptor),
    // ic: unchanged; not_found (An unregistered id gives GF_STATUS_NOT_FOUND; another kind than it was registered with
    // ic: gives GF_STATUS_INVALID_ARGUMENT.), unchanged.
    gf_status (GF_CALL* update_panel)(gf_object workspace, const gf_dock_panel* panel);
    /* Sets the workspace's options (gf_dock_options); each takes effect at
     * once. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: an options record has at least the 1.1 size, known flags and reserved zero), unchanged.
    gf_status (GF_CALL* set_options)(gf_object workspace, const gf_dock_options* options);
    /* Sets one floating site's override of FLOATS_SHOWN_WHILE_INACTIVE
     * (gf_dock_window_visibility), kept while the site exists. An unknown
     * site gives GF_STATUS_NOT_FOUND. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: a visibility is WORKSPACE, SHOWN or POLICY), unchanged; not_found (An unknown site gives
    // ic: GF_STATUS_NOT_FOUND.), unchanged.
    gf_status (GF_CALL* set_floating_window_visibility)(gf_object workspace, uint32_t site,
                                                        gf_dock_window_visibility visibility);
    /* Since 1.6: the frames presentation and the multiple document
     * interface (DK decisions 60 to 63). The main site realizes the
     * frames; the workspace is the document area of the window its main
     * site is attached to, which gives that window its MDI slots (the
     * windows table's, since 1.6). The presentation and the frames are
     * saved with the layout. Set the presentation and the frames with
     * apply_edit's 1.6 kinds.
     *
     * The document area's presentation. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires
    // ic: not_null(presentation), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: document_presentation_presentation, then the state refusal.
    gf_status (GF_CALL* document_presentation)(gf_object workspace, gf_dock_document_presentation* presentation);
    /* Arranges the frames in the main site's document area as last laid
     * out (gf_dock_frame_arrangement), as one program transaction: applied
     * with nothing to change when there is no main site, no frame or no
     * laid-out area. A refused arrangement gives GF_STATUS_INVALID_STATE
     * and writes the reason into refusal (may be null), as apply_edit. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: an arrangement is CASCADE, TILE_HORIZONTAL, TILE_VERTICAL or ARRANGE_ICONS), unchanged; invalid_state
    // ic: (A refused arrangement gives GF_STATUS_INVALID_STATE and writes the reason into refusal (may be null), as
    // ic: apply_edit.), unchanged.
    gf_status (GF_CALL* arrange_frames)(gf_object workspace, gf_dock_frame_arrangement arrangement,
                                        gf_dock_refusal* refusal);
    /* A registered document's frame (gf_dock_frame_memory, at least the 1.6
     * size); present 0 when it has none. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(frame),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires frame_size >= frame_memory_size_1_6, else
    // ic: returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: frame_memory_frame, then
    // ic: frame_memory_frame_size, then the state refusal. Fails not_found (code: no panel of that id is registered),
    // ic: unchanged.
    gf_status (GF_CALL* frame_memory)(gf_object workspace, gf_utf8_view document, gf_dock_frame_memory* frame);
    /* Whether minimized frames line the bottom of the area (1, the default)
     * or its top (0); not saved with the layout. The bound window's
     * set_mdi_children_minimized_anchor_bottom writes it too. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: bottom is 0 or 1), unchanged.
    gf_status (GF_CALL* set_frame_icons_at_bottom)(gf_object workspace, uint32_t bottom);
    /* The number of documents in the main site's document area (the MDI
     * children), whichever the presentation. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(count),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: area_document_count_count,
    // ic: then the state refusal.
    gf_status (GF_CALL* area_document_count)(gf_object workspace, uint64_t* count);
    /* The id of the document at index in the area, in tree and tab order,
     * into the caller's buffer; required receives its size, also with
     * GF_STATUS_BUFFER_TOO_SMALL. GF_STATUS_NOT_FOUND past the end. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails buffer_too_small (The
    // ic: id of the document at index in the area, in tree and tab order, into the caller's buffer; required receives
    // ic: its size, also with GF_STATUS_BUFFER_TOO_SMALL.), unchanged; not_found (GF_STATUS_NOT_FOUND past the end.),
    // ic: unchanged.
    gf_status (GF_CALL* area_document_at)(gf_object workspace, uint64_t index, char* buffer, uint64_t capacity,
                                          uint64_t* required);
    /* The id of the area's active document (the most recently active one
     * still in the area; else, with frames shown, the frontmost frame;
     * else the selected document of the area's first stack), as
     * area_document_at writes one; empty (required zero) when the area is
     * empty. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* area_active_document)(gf_object workspace, char* buffer, uint64_t capacity,
                                              uint64_t* required);
    /* Gives a registered panel a menu strip (a menu_strip object; zero
     * takes it away): while the panel's document is the area's active
     * document, the strip's items merge into the main window's main menu
     * strip (windows set_main_menu_strip) by their merge actions, and the
     * merge is reverted when another document becomes active or the panel
     * leaves (Windows Forms' MDI menu merging). The workspace holds the
     * strip; it need not be shown. Another object than a menu strip gives
     * GF_STATUS_WRONG_HANDLE_KIND. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails not_found (code: no
    // ic: panel of that id is registered), unchanged; wrong_handle_kind (Another object than a menu strip gives
    // ic: GF_STATUS_WRONG_HANDLE_KIND.), unchanged.
    gf_status (GF_CALL* set_panel_menu_strip)(gf_object workspace, gf_utf8_view panel, gf_object strip);
    /* The id of the document whose menu strip is merged into the main menu
     * strip, as area_active_document writes one; empty when none is. */
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged.
    gf_status (GF_CALL* merged_document)(gf_object workspace, char* buffer, uint64_t capacity, uint64_t* required);
    /* Since 1.7: tool strip merging, as the menu strip's. The workspace's
     * merge tool strip (a tool_strip or menu_strip object; zero takes it
     * away) is the main window's tool strip, which the application names
     * (Windows Forms leaves tool strips to ToolStripManager.Merge): while a
     * panel's document is the area's active document, the panel's tool
     * strip's items merge into it by their merge actions, and the merge is
     * reverted when another document becomes active or the panel leaves.
     * The workspace holds both strips; a panel's need not be shown.
     * Another object than a tool strip gives GF_STATUS_WRONG_HANDLE_KIND.
     * merge_tool_strip gives the merge tool strip as a new reference the
     * caller releases, or zero when none is set. merged_tool_strip_document
     * writes the id of the document whose strip is merged, as
     * area_active_document writes one; empty when none is. */
    gf_status (GF_CALL* set_merge_tool_strip)(gf_object workspace, gf_object strip);
    gf_status (GF_CALL* merge_tool_strip)(gf_object workspace, gf_object* strip);
    gf_status (GF_CALL* set_panel_tool_strip)(gf_object workspace, gf_utf8_view panel, gf_object strip);
    gf_status (GF_CALL* merged_tool_strip_document)(gf_object workspace, char* buffer, uint64_t capacity,
                                                    uint64_t* required);
    /* Since 1.7: ribbon merging, likewise: the active document's ribbon
     * (set_panel_ribbon, a ribbon object; zero takes it away) merges its
     * tabs into the workspace's merge ribbon (set_merge_ribbon) and is
     * reverted when another document becomes active or the panel leaves.
     * A merge the ribbons refuse leaves both as they were, and
     * merged_ribbon_document then writes nothing (required zero). Another
     * object than a ribbon gives GF_STATUS_WRONG_HANDLE_KIND. merge_ribbon
     * gives a new reference or zero, as merge_tool_strip. */
    gf_status (GF_CALL* set_merge_ribbon)(gf_object workspace, gf_object ribbon);
    gf_status (GF_CALL* merge_ribbon)(gf_object workspace, gf_object* ribbon);
    gf_status (GF_CALL* set_panel_ribbon)(gf_object workspace, gf_utf8_view panel, gf_object ribbon);
    gf_status (GF_CALL* merged_ribbon_document)(gf_object workspace, char* buffer, uint64_t capacity,
                                                uint64_t* required);
} gf_docking_table;

/*
 * GF_TABLE_FONTS (since 1.2): the bundled font families a gf_font names in
 * its reserved word (owner decision D2; the family registry, owner
 * decisions of 2026-11-13). A family is a fixed set of faces the library
 * ships, with a number that never changes; an application's own families
 * are loaded apart from them (load_family, below).
 * family_id gives the id for a name (ASCII case ignored): a family's own
 * name, then a name the library's font substitution table maps to a
 * bundled face (Segoe UI and Tahoma to Selawik). Resolve a name once and
 * pass the id. A name nothing maps to a bundled family (Arial, whose
 * substitute is not bundled with every program, or an unknown name) gives
 * GF_STATUS_NOT_FOUND: it is unmapped, and the role's faces draw. Since
 * 1.3 a role's own face (Portsmouth Rapids, Carlito, Cousine) has an id as
 * well; a 1.2 library gives GF_STATUS_NOT_FOUND for it. family_name gives
 * the name of an id (the name Windows Forms' Font.FontFamily.Name reports).
 * Both read only the library's constant tables and may be called from any
 * thread.
 *
 * Application font families (since 1.5, provisional: the integrator
 * renumbers the minor and the slots at fold time; owner decision of
 * 2026-11-15, "App-loaded fonts: inside composites and subsurfaces"). An
 * application loads its own font bytes as a family of its own
 * (load_family), with an id from 256 up that a gf_font's reserved word
 * carries like a bundled family's. Such a family draws only inside
 * composite controls (a composite document's look may name it by its name)
 * and inside sub-surfaces; stock controls, themes and GUI.Forms' own
 * interface drop it with a warning line and draw their bundled faces.
 *
 * load_family validates every face first (the table checks of
 * gui_forms::validate_app_font: an sfnt with TrueType or CFF outlines, no
 * collection, a sound table directory, and head, hhea, hmtx, maxp, cmap,
 * name, post and the outline tables present and consistent) and copies the
 * bytes, so the caller may free them on return. 1 to
 * GF_APP_FONT_MAXIMUM_FACES faces, each a gf_app_font_face whose
 * struct_size is at least the 1.5 size, reserved zero, weight 1 to 1000 and
 * italic 0 or 1, of at most GF_APP_FONT_MAXIMUM_BYTES bytes. The array is
 * stepped by the first face's struct_size, a multiple of 8 that every face
 * repeats, like every array of size-prefixed records. The name is 1
 * to 64 bytes of UTF-8 without control characters, quotes, commas or
 * semicolons, and no bundled family, CSS generic or loaded application
 * family has it. A refusal gives GF_STATUS_INVALID_ARGUMENT (the reason,
 * the face and the table as the failure's message), or
 * GF_STATUS_LIMIT_EXCEEDED once 256 families have been loaded in the
 * process; nothing is registered. A face that later fails to open, shape
 * or rasterise quarantines its family for the process: its text draws with
 * the font's bundled faces.
 *
 * unload_family unloads a family load_family gave: new text no longer
 * resolves it at once, and its bytes are freed when the last text engine
 * that opened them goes (deferred receives 1 then, 0 when they were freed
 * at once; it may be null). An id that is not loaded (never given, or
 * already unloaded) gives GF_STATUS_NOT_FOUND. An id is never reused.
 * Families still loaded when the library unloads are unloaded then. Both
 * may be called from any thread.
 */
#define GF_APP_FONT_MAXIMUM_FACES 4u
#define GF_APP_FONT_MAXIMUM_BYTES 67108864u

typedef struct gf_app_font_face {
    uint32_t struct_size;
    uint32_t reserved;
    uint32_t weight;
    uint32_t italic;
    gf_bytes_view bytes;
} gf_app_font_face;

// ic: gf_abi::fonts: generated from include/gui_forms/c/gf_abi_fonts.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. Thread: ui, unless a call says otherwise.
typedef struct gf_fonts_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    // ic: Requires not_null(family), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails not_found (code: no
    // ic: bundled font family answers that name), unchanged.
    gf_status (GF_CALL* family_id)(gf_utf8_view name, uint32_t* family);
    // ic: Fails not_found (code: no bundled font family has that id), unchanged.
    gf_status (GF_CALL* family_name)(uint32_t family, char* buffer, uint64_t capacity, uint64_t* required);
    // ic: Requires not_null(family) and (not_null(faces) or face_count == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails invalid_argument (code: load_family needs its faces and an output), unchanged.
    gf_status (GF_CALL* load_family)(gf_utf8_view name, const gf_app_font_face* faces, uint64_t face_count,
                                     uint32_t* family);
    // ic: Fails not_found (code: no loaded application font family has that id), unchanged.
    gf_status (GF_CALL* unload_family)(uint32_t family, uint32_t* deferred);
} gf_fonts_table;

/* gf_help_navigator: what help.show_navigator opens (Windows Forms'
 * HelpNavigator): the contents, the keyword index at text, or a search for
 * text. */
typedef uint32_t gf_help_navigator;
#define GF_HELP_NAVIGATOR_CONTENTS 0u
#define GF_HELP_NAVIGATOR_INDEX 1u
#define GF_HELP_NAVIGATOR_SEARCH 2u

/* gf_locale_field (since 1.2): a field of a language file's text record,
 * as help.read_field names it; the values are the C++ LocaleField's. */
typedef uint32_t gf_locale_field;
#define GF_LOCALE_FIELD_TEXT 0u
#define GF_LOCALE_FIELD_TOOL_TIP 1u
#define GF_LOCALE_FIELD_ACCESS_KEY 2u
#define GF_LOCALE_FIELD_TITLE 3u
#define GF_LOCALE_FIELD_PLACEHOLDER 4u
#define GF_LOCALE_FIELD_HEADER 5u
#define GF_LOCALE_FIELD_ACCESSIBLE_NAME 6u
#define GF_LOCALE_FIELD_ACCESSIBLE_DESCRIPTION 7u
#define GF_LOCALE_FIELD_HELP_LINE 8u
#define GF_LOCALE_FIELD_HELP 9u
#define GF_LOCALE_FIELD_KEYWORDS 10u

/* gf_locale_origin (since 1.2): the layer a field read by help.read_field
 * came from: none (no layer gives it), the built-in locale file, a value
 * the program set on a control in code, the translation, or the user's
 * override. */
typedef uint32_t gf_locale_origin;
#define GF_LOCALE_ORIGIN_NONE 0u
#define GF_LOCALE_ORIGIN_BUILT_IN 1u
#define GF_LOCALE_ORIGIN_PROGRAM 2u
#define GF_LOCALE_ORIGIN_TRANSLATION 3u
#define GF_LOCALE_ORIGIN_USER 4u

/*
 * GF_TABLE_HELP (since 1.2; roadmap row 3.7): a help book system's viewer,
 * popup, F1 resolution and What's This mode. system is a help_book_system
 * component (objects.create, schema type "help_book_system"); its records
 * come from a language file's help fields, given whole to load_built_in
 * (the <locale> document the C++ HelpBookSystem::load_built_in reads), so
 * a C program declares its topics as that file declares them. Every
 * function runs on the UI thread; text is borrowed for the call; a system
 * that is not a help_book_system is GF_STATUS_WRONG_HANDLE_KIND, a
 * disposed one GF_STATUS_DISPOSED.
 *
 * - attach makes window (a window object) one the system serves: its F1,
 *   Shift+F1 and help requests reach the system, and the viewer opens over
 *   it. detach undoes it.
 * - load_built_in reads locale_xml as the program's own help text and
 *   merges it over what earlier calls loaded (a field it gives replaces
 *   the earlier one); a file the reader refuses gives
 *   GF_STATUS_INVALID_ARGUMENT and leaves the records as they were.
 * - show, show_navigator and show_popup open the viewer at a record, at
 *   the contents, index or search, or the paper popup at a record beside
 *   anchor (a control in an attached window); shown receives 1 when it
 *   opened, 0 when there was nothing to open (an unknown record, no
 *   attached window).
 * - resolve writes the record F1 on control would open (the control's own,
 *   else the nearest ancestor's, else the home page) into buffer (the
 *   BUFFER_TOO_SMALL rule) and home 1 when it is the home page.
 * - enter_help_mode starts What's This on window (entered 1 when it
 *   started); cancel_help_mode ends it, if active.
 *
 * Since 1.2 as well, the language files (roadmap row 2.13, help H1). A
 * control's help fields are the ordinary text properties help_line, help
 * and keywords (properties.set and get); the program values the attached
 * windows' controls hold join the built-in text.
 * - load_translation reads locale_xml as the shipped translation of its
 *   language, replacing an earlier one (user overrides for another
 *   language go); load_user_overrides reads the user's own file for the
 *   language in force, which wins per field. A refused file, or a user
 *   file for another language, gives GF_STATUS_INVALID_ARGUMENT and changes
 *   nothing. clear_user_overrides drops the user's file.
 * - use_built_in_language is the untranslatable way back: while
 *   built_in_only is 1 only the built-in text shows; 0 restores the rest.
 *   Any other value is GF_STATUS_INVALID_ARGUMENT.
 * - export_template writes the template a translator starts from (every
 *   record's built-in text and, when language is the translation's, its
 *   translations over it; never the user's overrides; an empty language is
 *   the built-in one) as a locale file into buffer (the BUFFER_TOO_SMALL
 *   rule). A language that is not a language tag is
 *   GF_STATUS_INVALID_ARGUMENT; a template past the locale file's bounds
 *   GF_STATUS_LIMIT_EXCEEDED.
 * - read_field writes one field of record stable_id as it shows now into
 *   buffer (the BUFFER_TOO_SMALL rule) and the layer it came from into
 *   origin (GF_LOCALE_ORIGIN_NONE with an empty text when no layer gives
 *   it). A field past GF_LOCALE_FIELD_KEYWORDS is GF_STATUS_INVALID_ARGUMENT.
 */
// ic: gf_abi::help: generated from include/gui_forms/c/gf_abi_help.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. States: alive (initial), disposing, disposed. Observed by gf_objects_table.state():
// ic: alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed GF_OBJECT_STATE_DISPOSED. Thread: ui,
// ic: unless a call says otherwise. Identity: generational; a stale one: returns GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_help_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails wrong_handle_kind
    // ic: (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* attach)(gf_object system, gf_object window);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails wrong_handle_kind
    // ic: (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* detach)(gf_object system, gf_object window);
    // ic: Fails invalid_argument (code: the language file's help text was refused; nothing was loaded), unchanged;
    // ic: wrong_handle_kind (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* load_built_in)(gf_object system, gf_utf8_view locale_xml);
    // ic: Requires not_null(shown), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails wrong_handle_kind (code:
    // ic: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* show)(gf_object system, gf_utf8_view stable_id, uint32_t* shown);
    // ic: Fails invalid_argument (code: show_navigator needs a known navigator and a shown output), unchanged;
    // ic: wrong_handle_kind (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* show_navigator)(gf_object system, gf_help_navigator navigator, gf_utf8_view text,
                                        uint32_t* shown);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(shown),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: show_popup_shown, then the
    // ic: state refusal. Fails wrong_handle_kind (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* show_popup)(gf_object system, gf_utf8_view stable_id, gf_object anchor, uint32_t* shown);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Requires not_null(home),
    // ic: else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply: resolve_home, then the state
    // ic: refusal. Fails wrong_handle_kind (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* resolve)(gf_object system, gf_object control, char* buffer, uint64_t capacity,
                                 uint64_t* required, uint32_t* home);
    // ic: Admitted in alive. In disposing, disposed: returns GF_STATUS_DISPOSED, unchanged. Fails invalid_argument
    // ic: (code: enter_help_mode needs an entered output), unchanged; wrong_handle_kind (code: the object is not a
    // ic: help_book_system), unchanged.
    gf_status (GF_CALL* enter_help_mode)(gf_object system, gf_object window, uint32_t* entered);
    // ic: Fails wrong_handle_kind (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* cancel_help_mode)(gf_object system);
    // ic: Fails invalid_argument (code: the translation was refused; nothing was loaded), unchanged; wrong_handle_kind
    // ic: (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* load_translation)(gf_object system, gf_utf8_view locale_xml);
    // ic: Fails invalid_argument (code: the user's language file was refused or is for another language; nothing was
    // ic: loaded), unchanged; wrong_handle_kind (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* load_user_overrides)(gf_object system, gf_utf8_view locale_xml);
    // ic: Fails wrong_handle_kind (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* clear_user_overrides)(gf_object system);
    // ic: Fails invalid_argument (code: built_in_only must be 0 or 1), unchanged; wrong_handle_kind (code: the object
    // ic: is not a help_book_system), unchanged.
    gf_status (GF_CALL* use_built_in_language)(gf_object system, uint32_t built_in_only);
    // ic: Fails invalid_argument (code: the language is not a language tag), unchanged; limit_exceeded (code: the
    // ic: template passes the locale file's bounds and was not written), unchanged; wrong_handle_kind (code: the object
    // ic: is not a help_book_system), unchanged.
    gf_status (GF_CALL* export_template)(gf_object system, gf_utf8_view language, char* buffer, uint64_t capacity,
                                         uint64_t* required);
    // ic: Fails invalid_argument (code: read_field needs a known field and an origin output), unchanged;
    // ic: wrong_handle_kind (code: the object is not a help_book_system), unchanged.
    gf_status (GF_CALL* read_field)(gf_object system, gf_utf8_view stable_id, gf_locale_field field, char* buffer,
                                    uint64_t capacity, uint64_t* required, gf_locale_origin* origin);
} gf_help_table;

/*
 * GF_TABLE_CONTENT (since 1.4): the content boundary (C++
 * <gui_forms/content.hpp>; docs/user/content-surfaces.md). A content
 * surface (objects.create "ContentSurface") shows finished frames that a
 * consumer renders itself, on a thread of its own with its own engine and
 * fonts, or in a process of its own (hosted content). A content text
 * editor (objects.create "ContentTextEditor") keeps the text, caret,
 * selection, editing keys, input methods, undo, clipboard and
 * accessibility, while the consumer supplies the text layout and the
 * pixels. It is an integrity boundary, not a security boundary: what the
 * content hands over is validated before it is used, and content that
 * stalls, fails or crashes cannot stop or corrupt the interface; content in
 * the process, and a hosted program, are not confined.
 *
 * Threads. The surface, editor and hosted slots are UI-thread slots;
 * hosted_support may be called from any thread. A gf_content_sink is the
 * consumer's end of one channel: any thread may call the sink slots, at
 * any time, until it releases the sink. Once the surface revokes the
 * channel (close_content, new content, the surface leaving its window or
 * its destruction, a termination), every publication reports
 * GF_CONTENT_PUBLISH_REVOKED and reaches nothing the interface reads; the
 * sink still answers until it is released. The events (a surface's
 * status_changed; an editor's layout_requested, text_changed and
 * selection_changed) are subscribed through GF_TABLE_EVENTS. At most
 * GF_CONTENT_MAXIMUM_SINKS sinks are held at once (GF_STATUS_LIMIT_EXCEEDED
 * beyond).
 *
 * Programs. start_program runs a gf_content_program's run callback on a
 * thread the library starts for it, once, with sender zero and the name
 * "run"; inside it, program_sink gives the run's sink, and program_fail
 * names a failure the surface reports when run returns (as status failed).
 * run should return
 * when the sink reports a stop (sink_state, or sink_wait_for_request
 * returning early). terminate and the surface's close never wait longer
 * than the termination grace: a program still running then is abandoned
 * (its channel is revoked and its thread left to finish on its own). The
 * library releases run once, on the program's thread after run returns;
 * when start_program fails the caller keeps it.
 *
 * Hosted content. start_hosted starts a program by absolute path in a
 * process of its own (C++ ContentSurface::start_hosted_content), with its
 * frames through shared memory and its input through a channel; the
 * surface forwards its pointer, keys, text and focus to the process in the
 * owner's input order (manifests/abi/interaction_order.json), and only what
 * no subscriber or rule handled. A crash ends the process only: the
 * surface's status becomes GF_CONTENT_STATUS_FAILED with the signal or exit
 * status as its failure. Where hosted content is not available
 * (hosted_support; GF_HOST_CAPABILITY_HOSTED_CONTENT) start_hosted gives
 * GF_STATUS_UNSUPPORTED_CAPABILITY.
 */

/* A sink: nonzero, never reused; zero is none. */
typedef uint64_t gf_content_sink;

/* gf_content_status: where a surface's content is in its life. */
typedef uint32_t gf_content_status;
/* No content: before open_content, after close_content. */
#define GF_CONTENT_STATUS_DISCONNECTED 0u
/* Connected, no frame yet. */
#define GF_CONTENT_STATUS_WAITING 1u
/* Publishing, or reporting itself alive, within the stale interval. */
#define GF_CONTENT_STATUS_LIVE 2u
/* No frame and no heartbeat for longer than the stale interval: the last
 * good frame stays under a veil until the next frame or heartbeat. */
#define GF_CONTENT_STATUS_STALE 3u
/* A termination was asked for and the content has not finished. */
#define GF_CONTENT_STATUS_TERMINATING 4u
/* The content finished, or honoured a termination. */
#define GF_CONTENT_STATUS_ENDED 5u
/* The content did not finish within the termination grace, or a hosted
 * program was killed. */
#define GF_CONTENT_STATUS_ABANDONED 6u
/* The program failed (program_fail), or a hosted program crashed or broke
 * the protocol (the failure slot says how). */
#define GF_CONTENT_STATUS_FAILED 7u

/* gf_content_publish_result: what one frame publication did. Every result
 * but PUBLISHED leaves the surface showing what it showed before. */
typedef uint32_t gf_content_publish_result;
#define GF_CONTENT_PUBLISH_PUBLISHED 0u
/* Every buffer is held by readers: publish the next frame. */
#define GF_CONTENT_PUBLISH_DROPPED_BUSY 1u
/* The channel is revoked: nothing will be shown again. */
#define GF_CONTENT_PUBLISH_REVOKED 2u
/* Width or height zero. */
#define GF_CONTENT_PUBLISH_REFUSED_EMPTY 3u
/* An axis over GF_CONTENT_MAXIMUM_FRAME_EXTENT, or more pixel bytes than
 * GF_CONTENT_MAXIMUM_FRAME_BYTES. */
#define GF_CONTENT_PUBLISH_REFUSED_TOO_LARGE 4u
/* row_bytes smaller than width * 4. */
#define GF_CONTENT_PUBLISH_REFUSED_ROW_BYTES 5u
/* pixels shorter than the frame it describes. */
#define GF_CONTENT_PUBLISH_REFUSED_EXTENT 6u
/* A pixel format the surface does not know. */
#define GF_CONTENT_PUBLISH_REFUSED_FORMAT 7u
/* A damage rectangle outside the frame. */
#define GF_CONTENT_PUBLISH_REFUSED_DAMAGE 8u
/* The surface could not allocate a buffer of the frame's size. */
#define GF_CONTENT_PUBLISH_FAILED_ALLOCATION 9u

/* gf_content_semantics_result: what one semantic text publication did. */
typedef uint32_t gf_content_semantics_result;
#define GF_CONTENT_SEMANTICS_PUBLISHED 0u
#define GF_CONTENT_SEMANTICS_REVOKED 1u
#define GF_CONTENT_SEMANTICS_REFUSED_INVALID_UTF8 2u
/* The three texts hold more than GF_CONTENT_MAXIMUM_SEMANTIC_TEXT_BYTES. */
#define GF_CONTENT_SEMANTICS_REFUSED_TOO_LONG 3u

/* gf_content_layout_result: what checking an editor's layout found. Every
 * result but ACCEPTED leaves the previous layout in force. */
typedef uint32_t gf_content_layout_result;
#define GF_CONTENT_LAYOUT_ACCEPTED 0u
/* Made for another revision of the text than the current one. */
#define GF_CONTENT_LAYOUT_STALE_REVISION 1u
/* More than GF_CONTENT_MAXIMUM_LAYOUT_ENTRIES lines, clusters or carets. */
#define GF_CONTENT_LAYOUT_REFUSED_TOO_LARGE 2u
/* A range past the text, reversed, out of order or overlapping. */
#define GF_CONTENT_LAYOUT_REFUSED_RANGE 3u
/* A range or caret offset that is not a grapheme boundary. */
#define GF_CONTENT_LAYOUT_REFUSED_BOUNDARY 4u
/* A cluster naming no line, outside its line or empty; or a line its
 * clusters do not cover contiguously. */
#define GF_CONTENT_LAYOUT_REFUSED_CLUSTER 5u
/* A box that is not finite or has a negative size. */
#define GF_CONTENT_LAYOUT_REFUSED_GEOMETRY 6u
/* Caret stops not ascending by offset. */
#define GF_CONTENT_LAYOUT_REFUSED_CARET 7u

/* gf_content_pixel_format: the frame formats a surface accepts. */
typedef uint32_t gf_content_pixel_format;
/* Four bytes a pixel, blue, green, red, alpha, premultiplied, sRGB. */
#define GF_CONTENT_PIXEL_FORMAT_BGRA32_PREMULTIPLIED_SRGB 0u

/* gf_hosted_support: whether hosted content exists on this platform. */
typedef uint32_t gf_hosted_support;
#define GF_HOSTED_SUPPORT_AVAILABLE 0u
/* Not written for this platform yet (Windows). */
#define GF_HOSTED_SUPPORT_NOT_REPORTED 1u

#define GF_CONTENT_MAXIMUM_FRAME_EXTENT 16384u
#define GF_CONTENT_MAXIMUM_FRAME_BYTES 268435456u
#define GF_CONTENT_MAXIMUM_SEMANTIC_TEXT_BYTES 1048576u
#define GF_CONTENT_MAXIMUM_LAYOUT_ENTRIES 1000000u
/* The longest termination grace accepted, in milliseconds (longer ones are
 * lowered). */
#define GF_CONTENT_MAXIMUM_TERMINATION_GRACE_MS 10000u
/* The longest stale interval, wait and hosted timeout accepted, in
 * milliseconds (an hour); longer ones give GF_STATUS_INVALID_ARGUMENT. */
#define GF_CONTENT_MAXIMUM_WAIT_MS 3600000u
/* The most sinks held at once, and the most arguments of a hosted program. */
#define GF_CONTENT_MAXIMUM_SINKS 65536u
#define GF_HOSTED_MAXIMUM_ARGUMENTS 4096u

/* A rectangle of a frame's pixels. */
typedef struct gf_content_pixel_rect {
    uint32_t x;
    uint32_t y;
    uint32_t width;
    uint32_t height;
} gf_content_pixel_rect;

/*
 * One finished frame, an input record: struct_size at least its 1.4 size,
 * reserved zero, opaque 0 or 1. width by height pixels in pixel_format,
 * rows row_bytes apart from pixels.data; pixels must hold the frame's
 * extent, row_bytes * (height - 1) + width * 4 bytes. opaque promises every
 * pixel has alpha 255 (not checked per pixel; a broken promise shows what
 * lies under the surface and corrupts nothing). damage is the part that
 * differs from the previous frame of the same size; all zero for the whole
 * frame. The library copies the pixels before the call returns.
 */
typedef struct gf_content_frame {
    uint32_t struct_size;
    gf_content_pixel_format pixel_format;
    uint32_t width;
    uint32_t height;
    uint64_t row_bytes;
    uint32_t opaque;
    uint32_t reserved;
    gf_content_pixel_rect damage;
    gf_bytes_view pixels;
} gf_content_frame;

/* What the surface asks of its content, an output record (the caller sets
 * struct_size): the device size it shows at, the display scale, and a
 * generation that grows with every change. Zero size before the surface has
 * been arranged in a window. */
typedef struct gf_content_request {
    uint32_t struct_size;
    uint32_t width;
    uint32_t height;
    uint32_t reserved;
    double scale;
    uint64_t generation;
} gf_content_request;

/* The text assistive technology reads for content whose pixels carry none,
 * an input record (struct_size, reserved zero): the accessible name, the
 * text shown (the node's value) and a longer description, UTF-8. */
typedef struct gf_content_semantics {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view name;
    gf_utf8_view text;
    gf_utf8_view description;
} gf_content_semantics;

/* A channel's counters, an output record (the caller sets struct_size).
 * last_refusal is GF_CONTENT_PUBLISH_PUBLISHED while there was none. */
typedef struct gf_content_statistics {
    uint32_t struct_size;
    gf_content_publish_result last_refusal;
    uint64_t frames_published;
    uint64_t frames_refused;
    uint64_t frames_dropped;
    uint64_t heartbeats;
    uint64_t stale_transitions;
    uint64_t recoveries;
    uint64_t semantics_published;
    uint64_t semantics_refused;
    uint64_t after_revocation;
} gf_content_statistics;

/* A content program (start_program), an input record: struct_size at least
 * its 1.4 size, opaque 0 or 1 (the placeholder's promise until the first
 * frame), run as the section above describes. */
typedef struct gf_content_program {
    uint32_t struct_size;
    uint32_t opaque;
    gf_callback run;
} gf_content_program;

/* A hosted program (start_hosted), an input record: struct_size at least
 * its 1.4 size, opaque 0 or 1. executable is the program's absolute path
 * (UTF-8; it is not looked up on PATH), arguments its argument_count
 * arguments after its name (at most GF_HOSTED_MAXIMUM_ARGUMENTS). slot_bytes
 * is each frame slot's size (zero: 32 MiB; at most
 * GF_CONTENT_MAXIMUM_FRAME_BYTES); start_timeout_ms how long the program
 * may take to connect (zero: 5000); stop_grace_ms how long a stopped
 * program may take to exit before it is killed (zero: 100; keep it below
 * the surface's termination grace). Both times at most
 * GF_CONTENT_MAXIMUM_WAIT_MS. */
typedef struct gf_hosted_options {
    uint32_t struct_size;
    uint32_t opaque;
    gf_utf8_view executable;
    const gf_utf8_view* arguments;
    uint64_t argument_count;
    uint64_t slot_bytes;
    uint64_t start_timeout_ms;
    uint64_t stop_grace_ms;
} gf_hosted_options;

/* The input channel to a surface's hosted program, an output record (the
 * caller sets struct_size): connected is 1 while the process runs and is
 * connected; dropped counts the forwarded events it could not take. */
typedef struct gf_hosted_input_report {
    uint32_t struct_size;
    uint32_t connected;
    uint64_t dropped;
} gf_hosted_input_report;

/* One line of an editor's layout: its UTF-8 byte range [start, end) of the
 * text and its box, in the editor's logical coordinates. */
typedef struct gf_content_text_line {
    uint64_t start;
    uint64_t end;
    gf_logical_rect bounds;
} gf_content_text_line;

/* One cluster: a run of whole graphemes on line (an index into the
 * layout's lines), its byte range, right_to_left 0 or 1, and its box. */
typedef struct gf_content_text_cluster {
    uint64_t start;
    uint64_t end;
    uint32_t line;
    uint32_t right_to_left;
    gf_logical_rect bounds;
} gf_content_text_cluster;

/* An explicit caret position: a byte offset on a grapheme boundary and the
 * caret's bar there. */
typedef struct gf_content_caret_stop {
    uint64_t offset;
    gf_logical_rect caret;
} gf_content_caret_stop;

/* An affine map from a layout's own coordinates to the editor's logical
 * coordinates (provisional, see gf_content_text_layout's transformed):
 * x' = xx * x + xy * y + dx and y' = yx * x + yy * y + dy, the shape of a
 * painter transform (rotation, scale, shear and translation). */
typedef struct gf_content_layout_transform {
    double xx;
    double xy;
    double yx;
    double yy;
    double dx;
    double dy;
} gf_content_layout_transform;

/* A layout box mapped to the editor's logical coordinates (provisional): the
 * images of the unmapped box's corners. */
typedef struct gf_content_quad {
    gf_logical_point top_left;
    gf_logical_point top_right;
    gf_logical_point bottom_right;
    gf_logical_point bottom_left;
} gf_content_quad;

/* An editor's layout (editor_submit_layout), an input record (struct_size,
 * reserved zero), made for text_revision: lines in text order, clusters
 * covering each non-empty line, and optional caret stops ascending by
 * offset. The library copies the arrays.
 *
 * transformed and transform are provisional additions to the 1.4 record
 * (64 bytes before them), numbered 1.5 while 1.5 is unreleased and 1.6
 * otherwise: transformed 1 says every box is in the layout's own
 * coordinates and transform maps them to the editor's, so its caret,
 * selection, hit testing and published boxes follow the turned text
 * (C++ ContentTextLayout::transform); 0 (and a record of the 1.4 size)
 * means the boxes are the editor's coordinates. transform_reserved is
 * zero. A transform that is not finite or flattens the plane is refused as
 * GF_CONTENT_LAYOUT_REFUSED_GEOMETRY. */
typedef struct gf_content_text_layout {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t text_revision;
    const gf_content_text_line* lines;
    uint64_t line_count;
    const gf_content_text_cluster* clusters;
    uint64_t cluster_count;
    const gf_content_caret_stop* carets;
    uint64_t caret_count;
    /* Provisional (above). */
    uint32_t transformed;
    uint32_t transform_reserved;
    gf_content_layout_transform transform;
} gf_content_text_layout;

// ic: gf_abi::content: generated from include/gui_forms/c/gf_abi_content.ic.toml by tools/ic_declarations.py; edit the
// ic: declaration, not these lines. States: alive (initial), disposing, disposed. Observed by gf_objects_table.state():
// ic: alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed GF_OBJECT_STATE_DISPOSED. Thread: ui,
// ic: unless a call says otherwise. Identity: generational; a stale one: returns GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_content_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Opens a new channel on surface and gives its sink, revoking any
     * earlier channel (and stopping a running program, bounded). opaque is
     * 0 or 1. The status becomes waiting. A surface that is not a
     * content_surface gives GF_STATUS_WRONG_HANDLE_KIND. */
    // ic: Fails invalid_argument (code: open_content needs an output), unchanged; limit_exceeded (code:
    // ic: GF_CONTENT_MAXIMUM_SINKS sinks are held; release one first), unchanged; wrong_handle_kind (A surface that is
    // ic: not a content_surface gives GF_STATUS_WRONG_HANDLE_KIND.), unchanged. Returns gf_content_sink, owned by the
    // ic: caller.
    gf_status (GF_CALL* open_content)(gf_object surface, uint32_t opaque, gf_content_sink* sink);
    /* Stops a running program (bounded), revokes the channel and clears
     * the region; the status becomes disconnected. */
    gf_status (GF_CALL* close_content)(gf_object surface);
    /* Opens a new channel and runs program on a thread of its own. The
     * library takes program->run only when this returns GF_STATUS_OK. */
    // ic: Fails invalid_argument (code: a gf_content_program needs struct_size of at least 40 and opaque 0 or 1),
    // ic: unchanged.
    gf_status (GF_CALL* start_program)(gf_object surface, const gf_content_program* program);
    /* Starts a hosted program and forwards the surface's input to it (the
     * section above). GF_STATUS_INVALID_ARGUMENT for an executable that is
     * not an absolute path or a slot size over the limit. */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT for an executable that is not an absolute path or a slot
    // ic: size over the limit.), unchanged; limit_exceeded (code: a hosted program takes at most
    // ic: GF_HOSTED_MAXIMUM_ARGUMENTS arguments), unchanged; unsupported_capability (code: hosted content is not
    // ic: available on this platform (GF_HOST_CAPABILITY_HOSTED_CONTENT)), unchanged.
    gf_status (GF_CALL* start_hosted)(gf_object surface, const gf_hosted_options* options);
    /* Asks the content to stop and returns at once. */
    gf_status (GF_CALL* terminate)(gf_object surface);
    /* The bounded wait of a termination, close or replacement (default
     * 250), at most GF_CONTENT_MAXIMUM_TERMINATION_GRACE_MS. */
    gf_status (GF_CALL* set_termination_grace)(gf_object surface, uint64_t milliseconds);
    /* How long content may publish nothing before it is stale (default
     * 1000; zero turns the watchdog off; short ones are raised to the
     * minimum; at most GF_CONTENT_MAXIMUM_WAIT_MS). */
    // ic: Fails invalid_argument (code: a time is at most GF_CONTENT_MAXIMUM_WAIT_MS milliseconds), unchanged.
    gf_status (GF_CALL* set_stale_interval)(gf_object surface, uint64_t milliseconds);
    // ic: Fails invalid_argument (code: status needs an output), unchanged.
    gf_status (GF_CALL* status)(gf_object surface, gf_content_status* status);
    /* Why the content failed, as text; empty unless the status is failed. */
    gf_status (GF_CALL* failure)(gf_object surface, char* buffer, uint64_t capacity, uint64_t* required);
    /* The channel's counters; all zero without content. */
    // ic: Requires not_null(statistics), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires statistics_size
    // ic: >= statistics_size_1_4, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: statistics_statistics, then statistics_statistics_size.
    gf_status (GF_CALL* statistics)(gf_object surface, gf_content_statistics* statistics);
    /* The newest request sent to the content. */
    // ic: Requires not_null(request), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires request_size >=
    // ic: request_size_1_4, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: request_request, then request_request_size.
    gf_status (GF_CALL* request)(gf_object surface, gf_content_request* request);
    /* The veil's caption for status (stale, abandoned and failed have
     * English defaults); empty hides it. */
    // ic: Fails invalid_argument (code: an unknown gf_content_status), unchanged.
    gf_status (GF_CALL* set_status_caption)(gf_object surface, gf_content_status status, gf_utf8_view caption);
    /* Sets the semantic text from the UI thread; GF_STATUS_INVALID_ARGUMENT
     * when it is not UTF-8 or too long (nothing changes then). */
    // ic: Fails invalid_argument (GF_STATUS_INVALID_ARGUMENT when it is not UTF-8 or too long (nothing changes then).),
    // ic: unchanged.
    gf_status (GF_CALL* set_semantics)(gf_object surface, const gf_content_semantics* semantics);
    /* 1 while a program the surface started has not been reaped. */
    // ic: Requires not_null(running), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* program_running)(gf_object surface, uint32_t* running);
    /* The surface's hosted input channel; not connected and zero dropped
     * without hosted content. */
    // ic: Requires not_null(report), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires report_size >=
    // ic: hosted_input_report_size_1_4, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals
    // ic: apply: hosted_input_report, then hosted_input_report_size.
    gf_status (GF_CALL* hosted_input)(gf_object surface, gf_hosted_input_report* report);
    /* Whether hosted content exists here. Any thread. */
    // ic: Requires not_null(support), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Thread: any.
    gf_status (GF_CALL* hosted_support)(gf_hosted_support* support);
    /* Validates frame, then copies it into the surface (result says what
     * happened; a refusal is not a failed call). Any thread. */
    // ic: Fails invalid_argument (code: a gf_content_frame needs struct_size of at least 64, reserved zero, opaque 0 or
    // ic: 1 and a ), unchanged. Thread: any.
    gf_status (GF_CALL* sink_publish)(gf_content_sink sink, const gf_content_frame* frame,
                                      gf_content_publish_result* result);
    /* Reports the content alive without a new frame. Any thread. */
    // ic: Thread: any.
    gf_status (GF_CALL* sink_heartbeat)(gf_content_sink sink);
    /* Replaces the surface's semantic text; the newest wins. Any thread. */
    // ic: Requires not_null(result), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Thread: any.
    gf_status (GF_CALL* sink_publish_semantics)(gf_content_sink sink, const gf_content_semantics* semantics,
                                                gf_content_semantics_result* result);
    /* The newest request. Any thread. */
    // ic: Requires not_null(request), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires request_size >=
    // ic: request_size_1_4, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: sink_request_request, then sink_request_request_size. Thread: any.
    gf_status (GF_CALL* sink_request)(gf_content_sink sink, gf_content_request* request);
    /* Blocks the calling (content) thread until the request's generation
     * passes seen_generation, the channel is revoked, a stop is asked for,
     * or timeout_ms passes (at most GF_CONTENT_MAXIMUM_WAIT_MS); gives the
     * request then. The UI thread must not call it: it would block the
     * interface. */
    // ic: Requires not_null(request), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires request_size >=
    // ic: request_size_1_4, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: sink_wait_for_request_request, then sink_wait_for_request_request_size.
    gf_status (GF_CALL* sink_wait_for_request)(gf_content_sink sink, uint64_t seen_generation, uint64_t timeout_ms,
                                               gf_content_request* request);
    /* revoked is 1 once the channel is revoked; stop_requested 1 once the
     * surface asked the content to stop. Any thread. */
    // ic: Fails invalid_argument (code: sink_state needs both outputs), unchanged. Thread: any.
    gf_status (GF_CALL* sink_state)(gf_content_sink sink, uint32_t* revoked, uint32_t* stop_requested);
    /* The channel's counters. Any thread. */
    // ic: Requires not_null(statistics), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Requires statistics_size
    // ic: >= statistics_size_1_4, else returns GF_STATUS_INVALID_ARGUMENT, unchanged. When several refusals apply:
    // ic: sink_statistics_statistics, then sink_statistics_statistics_size. Thread: any.
    gf_status (GF_CALL* sink_statistics)(gf_content_sink sink, gf_content_statistics* statistics);
    /* Ends the caller's handle; the channel itself lives on while the
     * surface or another handle holds it. A released or unknown sink gives
     * GF_STATUS_STALE_HANDLE. Any thread. */
    // ic: Thread: any.
    gf_status (GF_CALL* sink_release)(gf_content_sink sink);
    /* Inside a program's run callback: a new handle to its sink, which the
     * caller releases. Elsewhere GF_STATUS_INVALID_STATE. */
    // ic: Fails invalid_argument (code: program_sink needs an output), unchanged; invalid_state (Elsewhere
    // ic: GF_STATUS_INVALID_STATE.), unchanged; limit_exceeded (code: GF_CONTENT_MAXIMUM_SINKS sinks are held; release
    // ic: one first), unchanged. Returns gf_content_sink, owned by the caller.
    gf_status (GF_CALL* program_sink)(gf_content_sink* sink);
    /* Inside a program's run callback: when run returns, the program ends
     * as failed with message (UTF-8) as its failure. The last call wins.
     * Elsewhere GF_STATUS_INVALID_STATE. */
    // ic: Fails invalid_state (Elsewhere GF_STATUS_INVALID_STATE.), unchanged.
    gf_status (GF_CALL* program_fail)(gf_utf8_view message);
    /* The editor's text revision, which grows with every change of the
     * text. A content_text_editor only, else GF_STATUS_WRONG_HANDLE_KIND. */
    // ic: Requires not_null(revision), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails wrong_handle_kind (A
    // ic: content_text_editor only, else GF_STATUS_WRONG_HANDLE_KIND.), unchanged.
    gf_status (GF_CALL* editor_text_revision)(gf_object editor, uint64_t* revision);
    /* The editor's text (UTF-8). */
    gf_status (GF_CALL* editor_text)(gf_object editor, char* buffer, uint64_t capacity, uint64_t* required);
    /* Replaces the text without undo history; the caret goes to the end. */
    gf_status (GF_CALL* editor_set_text)(gf_object editor, gf_utf8_view text);
    /* Validates and adopts layout for the current revision; result says
     * why it was refused otherwise (a refusal is not a failed call). */
    // ic: Fails invalid_argument (code: a gf_content_text_layout needs struct_size of at least 64, reserved zero and a
    // ic: result), unchanged.
    gf_status (GF_CALL* editor_submit_layout)(gf_object editor, const gf_content_text_layout* layout,
                                              gf_content_layout_result* result);
    /* 1 when the adopted layout is for the current revision. */
    // ic: Requires not_null(current), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* editor_layout_current)(gf_object editor, uint32_t* current);
    /* Opens the channel the editor's frames are published through and gives
     * its sink, revoking any earlier one. opaque is 0 or 1. */
    // ic: Requires not_null(sink), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails limit_exceeded (code:
    // ic: GF_CONTENT_MAXIMUM_SINKS sinks are held; release one first), unchanged. Returns gf_content_sink, owned by the
    // ic: caller.
    gf_status (GF_CALL* editor_open_frames)(gf_object editor, uint32_t opaque, gf_content_sink* sink);
    /* The caret's bar from the current layout: present is 0 without one
     * (rectangle is then left as it was). */
    // ic: Fails invalid_argument (code: editor_caret_rectangle needs both outputs), unchanged.
    gf_status (GF_CALL* editor_caret_rectangle)(gf_object editor, uint32_t* present, gf_logical_rect* rectangle);
    /* The selection's boxes from the current layout, in order: required
     * receives their number; a buffer of fewer than that many gives
     * GF_STATUS_BUFFER_TOO_SMALL and writes nothing. */
    // ic: Requires not_null(required) and (not_null(buffer) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (The selection's boxes from the current layout, in order: required receives
    // ic: their number; a buffer of fewer than that many gives GF_STATUS_BUFFER_TOO_SMALL and writes nothing.),
    // ic: unchanged.
    gf_status (GF_CALL* editor_selection_rectangles)(gf_object editor, gf_logical_rect* buffer, uint64_t capacity,
                                                     uint64_t* required);
    /* The text offset nearest point by the current layout (the end of the
     * text without one). */
    // ic: Requires not_null(offset), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* editor_position_at)(gf_object editor, gf_logical_point point, uint64_t* offset);
    /* 1 (the default): the editor draws the selection and caret over the
     * frame; 0 leaves both to the consumer. */
    // ic: Fails invalid_argument (code: a flag is 0 or 1), unchanged.
    gf_status (GF_CALL* editor_set_draws_selection)(gf_object editor, uint32_t draws);
    /* 1: Enter inserts a line break and Up, Down, Home and End move by the
     * layout's lines; 0 (the default): Enter passes on. */
    // ic: Fails invalid_argument (code: a flag is 0 or 1), unchanged.
    gf_status (GF_CALL* editor_set_multiline)(gf_object editor, uint32_t multiline);
    // ic: Fails invalid_argument (code: a flag is 0 or 1), unchanged.
    gf_status (GF_CALL* editor_set_read_only)(gf_object editor, uint32_t read_only);
    /*
     * The slots below are provisional additions (numbered 1.5 while 1.5 is
     * unreleased, else 1.6); a table whose struct_size ends before one does
     * not have it.
     */
    /* 1: a multiline editor inserts a tab for Tab (C++ set_accepts_tab);
     * 0 (the default): Tab goes on to focus navigation. */
    // ic: Fails invalid_argument (code: a flag is 0 or 1), unchanged.
    gf_status (GF_CALL* editor_set_accepts_tab)(gf_object editor, uint32_t accepts);
    /* Puts replacement (UTF-8) in place of the selection, or inserts it at
     * the caret, as one undo step through the read-only state and the edit
     * filter; applied is 1 when the edit was made. GF_STATUS_INVALID_ARGUMENT
     * for text that is not UTF-8. */
    // ic: Requires not_null(applied), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails invalid_argument
    // ic: (GF_STATUS_INVALID_ARGUMENT for text that is not UTF-8.), unchanged.
    gf_status (GF_CALL* editor_replace_selection)(gf_object editor, gf_utf8_view replacement, uint32_t* applied);
    /* Removes a non-empty selection; applied is 0 when there was nothing to
     * remove or the edit was refused. */
    // ic: Requires not_null(applied), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* editor_delete_selection)(gf_object editor, uint32_t* applied);
    /* The input method's unfinished composition as a byte range [start,
     * end) of the text: present is 0 without one (start and end are then
     * left as they were). */
    // ic: Fails invalid_argument (code: editor_composition_range needs all three outputs), unchanged.
    gf_status (GF_CALL* editor_composition_range)(gf_object editor, uint32_t* present, uint64_t* start,
                                                  uint64_t* end);
    /* The caret's bar as a quad mapped by the layout's transform: present is
     * 0 without a current layout (quad is then left as it was). */
    // ic: Fails invalid_argument (code: editor_caret_quad needs both outputs), unchanged.
    gf_status (GF_CALL* editor_caret_quad)(gf_object editor, uint32_t* present, gf_content_quad* quad);
    /* The selection's, and the composition's, boxes as mapped quads, in
     * order, as editor_selection_rectangles: required receives their number;
     * a buffer of fewer gives GF_STATUS_BUFFER_TOO_SMALL and writes nothing. */
    // ic: Requires not_null(required) and (not_null(buffer) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (The selection's, and the composition's, boxes as mapped quads, in order,
    // ic: as editor_selection_rectangles: required receives their number; a buffer of fewer gives
    // ic: GF_STATUS_BUFFER_TOO_SMALL and writes nothing.), unchanged.
    gf_status (GF_CALL* editor_selection_quads)(gf_object editor, gf_content_quad* buffer, uint64_t capacity,
                                                uint64_t* required);
    // ic: Requires not_null(required) and (not_null(buffer) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (code: the buffer holds fewer quads than there are), unchanged.
    gf_status (GF_CALL* editor_composition_quads)(gf_object editor, gf_content_quad* buffer, uint64_t capacity,
                                                  uint64_t* required);
} gf_content_table;

/*
 * GF_TABLE_LIST_VIEW (since 1.5): a list_view's columns, items, groups,
 * selection, focus and sort glyph (C++ ListView, Windows Forms' ListView).
 * Its events (selection_changed, item_activated, column_click,
 * item_checking, item_checked, column_width_changed, before_label_edit,
 * after_label_edit and the others the schema lists) are interactions:
 * events.subscribe takes their names on the view. Its settings (view,
 * activation, header_style, check_boxes, multi_select, full_row_select,
 * show_groups, default_group_header, label_edit, tile_size, font) are
 * properties (GF_TABLE_PROPERTIES). Another control gives
 * GF_STATUS_WRONG_HANDLE_KIND. UI thread only.
 *
 * Columns, items and groups are numbered by their index in the view's
 * model, which grouping and the shown order do not change. The item
 * functions read and replace the view's own items; a view in virtual mode
 * (a C++ item source) counts its source's items, and item_at, item_sub_item,
 * set_items and set_item_checked give GF_STATUS_INVALID_STATE there.
 * Replacing the items carries the selection, the focused item and the
 * anchor over by stable ID.
 *
 * Arrays of records the caller passes have the stride of their first
 * element's struct_size, the caller's sizeof, which every element repeats;
 * each element is at least its 1.5 size. A record and what it references
 * are read during the call; texts are copied.
 */

/* gf_list_view_alignment: how a column's text, or a group's header, sits. */
typedef uint32_t gf_list_view_alignment;
#define GF_LIST_VIEW_ALIGNMENT_LEFT 0u
#define GF_LIST_VIEW_ALIGNMENT_CENTER 1u
#define GF_LIST_VIEW_ALIGNMENT_RIGHT 2u

/* gf_list_view_sort_order: the order a column header's sort glyph shows. */
typedef uint32_t gf_list_view_sort_order;
#define GF_LIST_VIEW_SORT_ORDER_NONE 0u
#define GF_LIST_VIEW_SORT_ORDER_ASCENDING 1u
#define GF_LIST_VIEW_SORT_ORDER_DESCENDING 2u

/* The most columns, items and groups a view holds, and the most sub-items
 * an item given to set_items carries. */
#define GF_LIST_VIEW_MAXIMUM_COLUMNS 64u
#define GF_LIST_VIEW_MAXIMUM_ITEMS 10000000u
#define GF_LIST_VIEW_MAXIMUM_GROUPS 4096u
#define GF_LIST_VIEW_MAXIMUM_SUB_ITEMS 64u

/*
 * A details-view column. As input (set_columns): stable_id is its identity
 * (unique in the view, not empty), text the header's text, width in
 * logical units at text scale 1 (zero takes the default, 120; otherwise
 * from 20 to 4096), alignment its cells' text. As output (column_at) the
 * record is filled and its texts copied into the caller's buffer.
 */
typedef struct gf_list_view_column {
    uint32_t struct_size;
    gf_list_view_alignment alignment;
    double width;
    gf_utf8_view stable_id;
    gf_utf8_view text;
} gf_list_view_column;

/*
 * An item. As input (set_items): stable_id is its identity (unique in the
 * view, not empty); text is shown in the first column and as the label in
 * the other views; sub_items (sub_item_count of them, at most
 * GF_LIST_VIEW_MAXIMUM_SUB_ITEMS; null when none) in the later columns of
 * the details view; image_key names an image in the view's image lists
 * (empty for none); group names the group it is shown in (empty or unknown
 * for the default group); checked 0 or 1. As output (item_at) the record is
 * filled, its four texts copied into the caller's buffer, sub_items null
 * and sub_item_count the number of sub-items (item_sub_item reads them).
 */
typedef struct gf_list_view_item {
    uint32_t struct_size;
    uint32_t checked;
    gf_utf8_view stable_id;
    gf_utf8_view text;
    gf_utf8_view image_key;
    gf_utf8_view group;
    const gf_utf8_view* sub_items;
    uint64_t sub_item_count;
} gf_list_view_item;

/*
 * A group: stable_id its identity (unique in the view, not empty), header
 * its header's text, header_alignment where the header sits. Input to
 * set_groups and output of group_at, as the column record is.
 */
typedef struct gf_list_view_group {
    uint32_t struct_size;
    gf_list_view_alignment header_alignment;
    gf_utf8_view stable_id;
    gf_utf8_view header;
} gf_list_view_group;

// ic: gf_abi::list_view: generated from include/gui_forms/c/gf_abi_list_view.ic.toml by tools/ic_declarations.py; edit
// ic: the declaration, not these lines. States: alive (initial), disposing, disposed. Observed by
// ic: gf_objects_table.state(): alive GF_OBJECT_STATE_ALIVE, disposing GF_OBJECT_STATE_DISPOSING, disposed
// ic: GF_OBJECT_STATE_DISPOSED. Thread: ui, unless a call says otherwise. Identity: generational; a stale one: returns
// ic: GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_list_view_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* The number of columns. */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* column_count)(gf_object view, uint64_t* count);
    /* Column index (below the count) into column, an output record of at
     * least the 1.5 size; its stable id and text are copied into
     * text_buffer, one after the other, and the views point there.
     * text_required receives their total size, also with
     * GF_STATUS_BUFFER_TOO_SMALL, which leaves the record unwritten.
     * GF_STATUS_NOT_FOUND past the columns. */
    // ic: Fails buffer_too_small (Column index (below the count) into column, an output record of at least the 1.5
    // ic: size; its stable id and text are copied into text_buffer, one after the other, and the views point there.
    // ic: text_required receives their total size, also with GF_STATUS_BUFFER_TOO_SMALL, which leaves the record
    // ic: unwritten.), unchanged; invalid_argument (code: an output record of at least the 1.5 size, a text size
    // ic: output, and a buffer unless its ), unchanged; not_found (GF_STATUS_NOT_FOUND past the columns.), unchanged.
    gf_status (GF_CALL* column_at)(gf_object view, uint64_t index, gf_list_view_column* column, char* text_buffer,
                                   uint64_t text_capacity, uint64_t* text_required);
    /* Replaces the columns with count records (columns may be null when
     * count is zero), all or nothing. Clears the sort glyph when its column
     * is gone. A malformed record, a repeated or empty stable id or a width
     * out of range gives GF_STATUS_INVALID_ARGUMENT, more than
     * GF_LIST_VIEW_MAXIMUM_COLUMNS GF_STATUS_LIMIT_EXCEEDED, changing
     * nothing. */
    // ic: Fails invalid_argument (A malformed record, a repeated or empty stable id or a width out of range gives
    // ic: GF_STATUS_INVALID_ARGUMENT, more than GF_LIST_VIEW_MAXIMUM_COLUMNS GF_STATUS_LIMIT_EXCEEDED, changing
    // ic: nothing.), unchanged; limit_exceeded (A malformed record, a repeated or empty stable id or a width out of
    // ic: range gives GF_STATUS_INVALID_ARGUMENT, more than GF_LIST_VIEW_MAXIMUM_COLUMNS GF_STATUS_LIMIT_EXCEEDED,
    // ic: changing nothing.), unchanged.
    gf_status (GF_CALL* set_columns)(gf_object view, const gf_list_view_column* columns, uint64_t count);
    /* Sets column's width (20 to 4096 logical units), raising
     * column_width_changed. GF_STATUS_NOT_FOUND past the columns. */
    // ic: Fails invalid_argument (code: a column's width is finite), unchanged; not_found (GF_STATUS_NOT_FOUND past the
    // ic: columns.), unchanged.
    gf_status (GF_CALL* set_column_width)(gf_object view, uint64_t column, double width);
    /* The number of items (the source's in virtual mode). */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* item_count)(gf_object view, uint64_t* count);
    /* Item index (below the count) into item, an output record of at least
     * the 1.5 size; its stable id, text, image key and group are copied
     * into text_buffer one after another, as column_at copies. */
    // ic: Fails buffer_too_small (code: the text buffer is smaller than the record's text), unchanged; invalid_argument
    // ic: (code: an output record of at least the 1.5 size, a text size output, and a buffer unless its ), unchanged;
    // ic: invalid_state (code: a virtual list view's items belong to its source), unchanged; not_found (code: the item
    // ic: is past the view's items), unchanged.
    gf_status (GF_CALL* item_at)(gf_object view, uint64_t index, gf_list_view_item* item, char* text_buffer,
                                 uint64_t text_capacity, uint64_t* text_required);
    /* Item index's sub-item (below its sub_item_count) as text, into buffer
     * (required receives its size; a short buffer gives
     * GF_STATUS_BUFFER_TOO_SMALL). GF_STATUS_NOT_FOUND past the items or
     * the sub-items. */
    // ic: Fails buffer_too_small (Item index's sub-item (below its sub_item_count) as text, into buffer (required
    // ic: receives its size; a short buffer gives GF_STATUS_BUFFER_TOO_SMALL).), unchanged; invalid_state (code: a
    // ic: virtual list view's items belong to its source), unchanged; not_found (GF_STATUS_NOT_FOUND past the items or
    // ic: the sub-items.), unchanged.
    gf_status (GF_CALL* item_sub_item)(gf_object view, uint64_t index, uint64_t sub_item, char* buffer,
                                       uint64_t capacity, uint64_t* required);
    /* Replaces the items with count records (items may be null when count
     * is zero), all or nothing; the selection, focus and anchor follow
     * their stable ids. A malformed record or a repeated or empty stable id
     * gives GF_STATUS_INVALID_ARGUMENT, more than GF_LIST_VIEW_MAXIMUM_ITEMS
     * items or GF_LIST_VIEW_MAXIMUM_SUB_ITEMS sub-items
     * GF_STATUS_LIMIT_EXCEEDED, changing nothing. */
    // ic: Fails invalid_argument (A malformed record or a repeated or empty stable id gives GF_STATUS_INVALID_ARGUMENT,
    // ic: more than GF_LIST_VIEW_MAXIMUM_ITEMS items or GF_LIST_VIEW_MAXIMUM_SUB_ITEMS sub-items
    // ic: GF_STATUS_LIMIT_EXCEEDED, changing nothing.), unchanged; invalid_state (code: a virtual list view's items
    // ic: belong to its source), unchanged; limit_exceeded (A malformed record or a repeated or empty stable id gives
    // ic: GF_STATUS_INVALID_ARGUMENT, more than GF_LIST_VIEW_MAXIMUM_ITEMS items or GF_LIST_VIEW_MAXIMUM_SUB_ITEMS
    // ic: sub-items GF_STATUS_LIMIT_EXCEEDED, changing nothing.), unchanged.
    gf_status (GF_CALL* set_items)(gf_object view, const gf_list_view_item* items, uint64_t count);
    /* The index of the item with stable id item_id; GF_STATUS_NOT_FOUND
     * when no item has it (and always in virtual mode). */
    // ic: Requires not_null(index), else returns GF_STATUS_INVALID_ARGUMENT, unchanged. Fails not_found
    // ic: (GF_STATUS_NOT_FOUND when no item has it (and always in virtual mode).), unchanged.
    gf_status (GF_CALL* index_of)(gf_object view, gf_utf8_view item_id, uint64_t* index);
    /* Sets item index's checked state (0 or 1), raising item_checked when
     * it changes. GF_STATUS_NOT_FOUND past the items. */
    // ic: Fails invalid_argument (code: checked is 0 or 1), unchanged; invalid_state (code: a virtual list view's items
    // ic: belong to its source), unchanged; not_found (GF_STATUS_NOT_FOUND past the items.), unchanged.
    gf_status (GF_CALL* set_item_checked)(gf_object view, uint64_t index, uint32_t checked);
    /* The number of groups. */
    // ic: Requires not_null(count), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* group_count)(gf_object view, uint64_t* count);
    /* Group index (below the count) into group, an output record of at
     * least the 1.5 size; its stable id and header are copied into
     * text_buffer, as column_at copies. */
    // ic: Fails buffer_too_small (code: the text buffer is smaller than the record's text), unchanged; invalid_argument
    // ic: (code: an output record of at least the 1.5 size, a text size output, and a buffer unless its ), unchanged;
    // ic: not_found (code: the group is past the view's groups), unchanged.
    gf_status (GF_CALL* group_at)(gf_object view, uint64_t index, gf_list_view_group* group, char* text_buffer,
                                  uint64_t text_capacity, uint64_t* text_required);
    /* Replaces the groups with count records, all or nothing, as
     * set_columns replaces the columns (at most
     * GF_LIST_VIEW_MAXIMUM_GROUPS). */
    // ic: Fails limit_exceeded (code: the array holds more records than the view does), unchanged.
    gf_status (GF_CALL* set_groups)(gf_object view, const gf_list_view_group* groups, uint64_t count);
    /* The selected items' indices in ascending order into indices
     * (capacity elements; null when the capacity is zero). count receives
     * the number selected, also with GF_STATUS_BUFFER_TOO_SMALL, which
     * writes no index. */
    // ic: Requires not_null(count) and (not_null(indices) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (The selected items' indices in ascending order into indices (capacity
    // ic: elements; null when the capacity is zero). count receives the number selected, also with
    // ic: GF_STATUS_BUFFER_TOO_SMALL, which writes no index.), unchanged.
    gf_status (GF_CALL* selected_items)(gf_object view, uint64_t* indices, uint64_t capacity, uint64_t* count);
    /* Selects (1) or deselects (0) item index, keeping the others (without
     * multi_select, selecting one deselects the rest); raises
     * selection_changed when the selection changes. GF_STATUS_NOT_FOUND past
     * the items. */
    // ic: Fails invalid_argument (code: selected is 0 or 1), unchanged; not_found (GF_STATUS_NOT_FOUND past the
    // ic: items.), unchanged.
    gf_status (GF_CALL* set_selected)(gf_object view, uint64_t index, uint32_t selected);
    /* Selects every item (with multi_select), raising selection_changed. */
    gf_status (GF_CALL* select_all)(gf_object view);
    /* Deselects every item, raising selection_changed when any was. */
    gf_status (GF_CALL* clear_selection)(gf_object view);
    /* The item with the focus cue and the keyboard's position, or -1. */
    // ic: Requires not_null(index), else returns GF_STATUS_INVALID_ARGUMENT, unchanged.
    gf_status (GF_CALL* focused_item)(gf_object view, int64_t* index);
    /* Moves the focus cue to item index, or removes it with -1.
     * GF_STATUS_NOT_FOUND past the items. */
    // ic: Fails invalid_argument (code: the focused item is an index or -1), unchanged; not_found (GF_STATUS_NOT_FOUND
    // ic: past the items.), unchanged.
    gf_status (GF_CALL* set_focused_item)(gf_object view, int64_t index);
    /* Scrolls so item index is fully visible. GF_STATUS_NOT_FOUND past the
     * items. */
    // ic: Fails not_found (GF_STATUS_NOT_FOUND past the items.), unchanged.
    gf_status (GF_CALL* ensure_visible)(gf_object view, uint64_t index);
    /* Shows the sort glyph order on column, or on no column with -1 (the
     * order is then ignored). The view does not reorder its items for it.
     * GF_STATUS_NOT_FOUND past the columns. */
    // ic: Fails invalid_argument (code: the sorted column is an index or -1), unchanged; not_found (GF_STATUS_NOT_FOUND
    // ic: past the columns.), unchanged.
    gf_status (GF_CALL* set_sort_indicator)(gf_object view, int64_t column, gf_list_view_sort_order order);
    /* Stable-sorts the items by column's text (ASCII case folded, then byte
     * order) in order and shows the glyph; the selection and focus follow
     * their items. GF_STATUS_NOT_FOUND past the columns. */
    // ic: Fails invalid_argument (code: the sort order is NONE, ASCENDING or DESCENDING), unchanged; invalid_state
    // ic: (code: a virtual list view's items belong to its source), unchanged; not_found (GF_STATUS_NOT_FOUND past the
    // ic: columns.), unchanged.
    gf_status (GF_CALL* sort_items)(gf_object view, uint64_t column, gf_list_view_sort_order order);
} gf_list_view_table;

/*
 * GF_TABLE_AUTOMATION (since 1.6): the marks a program sets for the
 * library's MCP automation (C++ gui_forms/automation/marks/
 * automation_marks.hpp; notes:planning/studies/MCP_AUTOMATION_STUDY.md).
 * A program built with the automation serves its windows to a model over
 * MCP when started with --mcp; these marks say what a model may do there.
 * A build without the automation (a high-integrity build, or one configured
 * with GUI_FORMS_MCP off) keeps the marks and serves nothing:
 * capability_built answers 0. Any thread may set the stable-ID marks; the
 * window marks are UI thread only.
 *
 * A program also declares its own model tools and inspect sections here
 * (C++ gui_forms/automation/tools/model_tools.hpp, the same registry the
 * library's own extensions use). A tool is a gf_automation_tool record: its
 * name, texts, access and flags, and its callbacks with their context,
 * like a class table's. The safety rules hold for every tool: a
 * destructive one asks the user's one question of the launch first and is
 * refused after Don't Allow; an acting one runs on the UI thread and is
 * refused while a dialog the program did not instrument is open; the
 * program answers for what its replies contain and must not put a password
 * field's text in one.
 *
 * Actions are numbered as gf_automation_action. Stable IDs and texts are
 * read during the call and copied.
 */

/* gf_automation_action: a semantic action a mark names (the C++
 * SemanticAction's values). */
typedef uint32_t gf_automation_action;
#define GF_AUTOMATION_ACTION_FOCUS 0u
#define GF_AUTOMATION_ACTION_PRESS 1u
#define GF_AUTOMATION_ACTION_SELECT 2u
#define GF_AUTOMATION_ACTION_INCREMENT 3u
#define GF_AUTOMATION_ACTION_DECREMENT 4u
#define GF_AUTOMATION_ACTION_EXPAND 5u
#define GF_AUTOMATION_ACTION_COLLAPSE 6u
#define GF_AUTOMATION_ACTION_SHOW_MENU 7u
#define GF_AUTOMATION_ACTION_SET_VALUE 8u
#define GF_AUTOMATION_ACTION_CLOSE 9u

/* gf_automation_tool_access: what a tool may do, as MCP's annotations tell
 * the client. */
typedef uint32_t gf_automation_tool_access;
#define GF_AUTOMATION_TOOL_READ_ONLY 0u
#define GF_AUTOMATION_TOOL_ACTING 1u

/* gf_automation_tool_flags: a tool's marks. DESTRUCTIVE: every call
 * changes something the user cannot take back (the launch's question is
 * asked first). ANY_THREAD: the call runs off the UI thread and touches no
 * control (read-only tools only). SETTLES: an acting tool's call runs as an
 * act, posted and waited on until the window is idle, its reply followed by
 * what changed. */
typedef uint32_t gf_automation_tool_flags;
#define GF_AUTOMATION_TOOL_DESTRUCTIVE 0x1u
#define GF_AUTOMATION_TOOL_ANY_THREAD 0x2u
#define GF_AUTOMATION_TOOL_SETTLES 0x4u

/* gf_automation_tool_error: the named failure a tool answers (the C++
 * ModelToolError's values), reaching the model as its name. */
typedef uint32_t gf_automation_tool_error;
#define GF_AUTOMATION_TOOL_ERROR_NOT_FOUND 0u
#define GF_AUTOMATION_TOOL_ERROR_STALE_IDENTITY 1u
#define GF_AUTOMATION_TOOL_ERROR_AMBIGUOUS 2u
#define GF_AUTOMATION_TOOL_ERROR_DISABLED 3u
#define GF_AUTOMATION_TOOL_ERROR_NOT_VISIBLE 4u
#define GF_AUTOMATION_TOOL_ERROR_UNSUPPORTED_ACTION 5u
#define GF_AUTOMATION_TOOL_ERROR_DIALOG_OPEN 6u
#define GF_AUTOMATION_TOOL_ERROR_NEEDS_USER 7u
#define GF_AUTOMATION_TOOL_ERROR_USER_TOOK_CONTROL 8u
#define GF_AUTOMATION_TOOL_ERROR_DECLINED 9u
#define GF_AUTOMATION_TOOL_ERROR_PROTECTED_CONTENT 10u
#define GF_AUTOMATION_TOOL_ERROR_TIMEOUT 11u
#define GF_AUTOMATION_TOOL_ERROR_NOT_IN_THIS_BUILD 12u
#define GF_AUTOMATION_TOOL_ERROR_INVALID_ARGUMENTS 13u
#define GF_AUTOMATION_TOOL_ERROR_UNKNOWN_TOOL 14u
#define GF_AUTOMATION_TOOL_ERROR_WINDOW_GONE 15u
#define GF_AUTOMATION_TOOL_ERROR_CANCELLED 16u
#define GF_AUTOMATION_TOOL_ERROR_BUSY 17u
#define GF_AUTOMATION_TOOL_ERROR_INTERNAL 18u

/* One call of a tool, lent to its call callback: the arguments (one JSON
 * object), who is calling as the client states it, the stable ID of the
 * window the call names (empty for an ANY_THREAD tool), and reply, the
 * token the reply slots fill, valid until the callback returns. */
typedef struct gf_automation_tool_call {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view arguments_json;
    gf_utf8_view client_name;
    gf_utf8_view model_name;
    gf_utf8_view window_id;
    void* reply;
} gf_automation_tool_call;

/*
 * A tool's declaration (since 1.6): name is 1 to 64 bytes of ASCII
 * letters, digits, '_', '-' and '.', starting with a letter, and not a
 * built-in tool's or a command's (command.<id>); title may be empty;
 * description, at most 4096 bytes, tells the model what it does;
 * input_schema is the arguments' JSON Schema, one object of at most 16384
 * bytes, or empty for any object. The texts are copied. call is required
 * and runs on the UI thread of the program's first window (off it for an
 * ANY_THREAD tool); it fills its reply through reply_text, reply_structured,
 * reply_image and reply_fail, and a reply left empty succeeds with no text.
 * release, which may be null, is called once with context when the tool is
 * unregistered, after its last call returned. reserved is zero.
 */
typedef struct gf_automation_tool {
    uint32_t struct_size;
    gf_automation_tool_access access;
    gf_automation_tool_flags flags;
    uint32_t reserved;
    gf_utf8_view name;
    gf_utf8_view title;
    gf_utf8_view description;
    gf_utf8_view input_schema;
    void* context;
    void (GF_CALL* call)(void* context, const gf_automation_tool_call* call);
    void (GF_CALL* release)(void* context);
} gf_automation_tool;

/* One inspect request, lent to a section's inspect callback (UI thread):
 * the window's stable ID, the stable ID of the node the call named (empty
 * for none), the call's arguments, and text, the token inspect_append
 * fills, valid until the callback returns. */
typedef struct gf_automation_inspect_call {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view window_id;
    gf_utf8_view node_id;
    gf_utf8_view arguments_json;
    void* text;
} gf_automation_inspect_call;

/* An inspect section's declaration (since 1.6): name follows the tool name
 * rule and is inspect's `what`; description tells the model what it shows.
 * inspect is required; release, which may be null, is called once with
 * context when the section is unregistered. reserved is zero. */
typedef struct gf_automation_inspect_section {
    uint32_t struct_size;
    uint32_t reserved;
    gf_utf8_view name;
    gf_utf8_view description;
    void* context;
    void (GF_CALL* inspect)(void* context, const gf_automation_inspect_call* call);
    void (GF_CALL* release)(void* context);
} gf_automation_inspect_section;

typedef struct gf_automation_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* 1 into built when this library serves MCP automation, else 0. */
    gf_status (GF_CALL* capability_built)(uint32_t* built);
    /* Whether a model may drive window while it is a dialog (study 9.2): a
     * dialog refuses the model unless its program instrumented it. window
     * is a windows.create window; GF_STATUS_WRONG_HANDLE_KIND for another
     * object. */
    gf_status (GF_CALL* set_window_drivable)(gf_object window, uint32_t drivable);
    /* Whether every act in window is destructive: the model's first one
     * asks the user once for the launch. */
    gf_status (GF_CALL* set_window_destructive)(gf_object window, uint32_t destructive);
    /* Whether action on the node stable_id (a control's or a virtual
     * child's) is destructive. GF_STATUS_INVALID_ARGUMENT for an empty ID,
     * one past 4096 bytes, or an unknown action. */
    gf_status (GF_CALL* set_action_destructive)(gf_utf8_view stable_id, gf_automation_action action,
                                                uint32_t destructive);
    /* Hides the command stable_id from the model's tool list, or shows it
     * again. */
    gf_status (GF_CALL* set_command_hidden)(gf_utf8_view stable_id, uint32_t hidden);
    /* Marks a step only the user may do on stable_id (a sign-in, a
     * payment): the model is told what and waits for it (study 9.3). what
     * is shown to the user; both 1 to 4096 bytes. */
    gf_status (GF_CALL* require_user_step)(gf_utf8_view stable_id, gf_utf8_view what);
    /* The user did the step on stable_id; nothing for an ID with none. */
    gf_status (GF_CALL* complete_user_step)(gf_utf8_view stable_id);
    /* 1 into pending while a step on stable_id waits for the user. */
    gf_status (GF_CALL* user_step_pending)(gf_utf8_view stable_id, uint32_t* pending);
    /* Whether the model may read the clipboard (off by default). */
    gf_status (GF_CALL* set_clipboard_readable)(uint32_t readable);
    /* The server identity (app.<vendor>.<product> by default): 3 to 128
     * bytes of lowercase ASCII letters, digits, '.', '-' and '_', starting
     * with a letter, with at least one '.'. GF_STATUS_INVALID_ARGUMENT,
     * keeping the identity, otherwise. */
    gf_status (GF_CALL* set_identity)(gf_utf8_view identity);
    /* Declares a tool (see gf_automation_tool); any thread.
     * GF_STATUS_INVALID_ARGUMENT for a record that breaks its rules (an
     * acting tool with ANY_THREAD among them), GF_STATUS_INVALID_STATE for a
     * name taken, GF_STATUS_LIMIT_EXCEEDED past 256 tools. On a refusal
     * release is not called. */
    gf_status (GF_CALL* register_tool)(const gf_automation_tool* tool);
    /* Withdraws the tool name; GF_STATUS_NOT_FOUND when none has it. */
    gf_status (GF_CALL* unregister_tool)(gf_utf8_view name);
    /* Appends UTF-8 text to a call's reply (at most 4 MiB in all).
     * GF_STATUS_INVALID_ARGUMENT for text that is not UTF-8, too much text,
     * or a reply already failed. */
    gf_status (GF_CALL* reply_text)(void* reply, gf_utf8_view text);
    /* Sets the reply's structured content, one JSON object matching the
     * tool's declared output. */
    gf_status (GF_CALL* reply_structured)(void* reply, gf_utf8_view json);
    /* Adds an image, count bytes (at most 16 MiB) of mime_type (empty for
     * image/png; image/ types only). */
    gf_status (GF_CALL* reply_image)(void* reply, const uint8_t* bytes, uint64_t count, gf_utf8_view mime_type);
    /* Fails the call with error, a message and a hint (either may be
     * empty); what the reply held is dropped and later fills are refused. */
    gf_status (GF_CALL* reply_fail)(void* reply, gf_automation_tool_error error, gf_utf8_view message,
                                    gf_utf8_view hint);
    /* Declares an inspect section (see gf_automation_inspect_section);
     * statuses as register_tool's, the limit 64 sections. */
    gf_status (GF_CALL* register_inspect_section)(const gf_automation_inspect_section* section);
    gf_status (GF_CALL* unregister_inspect_section)(gf_utf8_view name);
    /* Appends UTF-8 text to an inspect request's answer. */
    gf_status (GF_CALL* inspect_append)(void* text, gf_utf8_view utf8);
} gf_automation_table;

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

// ic: gf_abi::windows_paint_endpoint: generated from include/gui_forms/c/gf_abi_windows_paint_endpoint.ic.toml by
// ic: tools/ic_declarations.py; edit the declaration, not these lines. Thread: ui, unless a call says otherwise.
// ic: Identity: generational; a stale one: returns GF_STATUS_STALE_HANDLE, unchanged.
typedef struct gf_windows_paint_endpoint_table {
    uint32_t struct_size;
    uint32_t abi_minor;
    /* Makes an endpoint of width by height pixels (1 to
     * GF_PAINT_ENDPOINT_MAXIMUM_SIDE each, at most 2^28 pixels) bound to
     * control, a custom control (else GF_STATUS_WRONG_HANDLE_KIND), and
     * gives its token and compatibility handle. */
    // ic: Fails invalid_argument (code: acquire needs outputs and 1 to 32768 pixels a side, at most 2^28 in all),
    // ic: unchanged; wrong_handle_kind (Makes an endpoint of width by height pixels (1 to
    // ic: GF_PAINT_ENDPOINT_MAXIMUM_SIDE each, at most 2^28 pixels) bound to control, a custom control (else
    // ic: GF_STATUS_WRONG_HANDLE_KIND), and gives its token and compatibility handle.), unchanged. Returns
    // ic: gf_paint_endpoint, owned by the caller.
    gf_status (GF_CALL* acquire)(gf_object control, uint32_t width, uint32_t height, gf_paint_endpoint* endpoint,
                                 uint64_t* compatibility_handle);
    /* Resizes the pixels; the token and handle stay. */
    // ic: Fails invalid_argument (code: configure needs 1 to 32768 pixels a side, at most 2^28 in all), unchanged.
    gf_status (GF_CALL* configure)(gf_paint_endpoint endpoint, uint32_t width, uint32_t height);
    /* Marks the pixels changed; explicit_boundary (0 or 1) ends a frame now
     * rather than at the endpoint's next quiet point. */
    // ic: Fails invalid_argument (code: explicit_boundary is 0 or 1), unchanged.
    gf_status (GF_CALL* touch)(gf_paint_endpoint endpoint, uint32_t explicit_boundary);
    /* Publishes what the endpoint holds now. */
    gf_status (GF_CALL* drain)(gf_paint_endpoint endpoint);
    /* The endpoint's state as text, for diagnostics; required receives its
     * size, a short buffer gives GF_STATUS_BUFFER_TOO_SMALL. */
    // ic: Requires not_null(required) and (not_null(buffer) or capacity == 0), else returns GF_STATUS_INVALID_ARGUMENT,
    // ic: unchanged. Fails buffer_too_small (The endpoint's state as text, for diagnostics; required receives its size,
    // ic: a short buffer gives GF_STATUS_BUFFER_TOO_SMALL.), unchanged.
    gf_status (GF_CALL* snapshot)(gf_paint_endpoint endpoint, char* buffer, uint64_t capacity, uint64_t* required);
    /* Publishes width by height premultiplied BGRA pixels, row_bytes apart
     * (at least width * 4), to the endpoint behind compatibility_handle. */
    // ic: Fails invalid_argument (code: submit_bgra needs a handle and bounded pixels with rows of at least width * 4
    // ic: bytes), unchanged.
    gf_status (GF_CALL* submit_bgra)(uint64_t compatibility_handle, uint32_t width, uint32_t height,
                                     uint64_t row_bytes, const uint8_t* pixels);
    /* Ends the endpoint; its token and handle stop working. */
    gf_status (GF_CALL* release)(gf_paint_endpoint endpoint);
    /* GetDC and ReleaseDC for the shim: the endpoint's memory DC. */
    // ic: Fails invalid_argument (code: get_dc needs a handle and an output), unchanged.
    gf_status (GF_CALL* get_dc)(uint64_t compatibility_handle, uint64_t* device_context);
    // ic: Fails invalid_argument (code: release_dc needs a handle and a device context), unchanged.
    gf_status (GF_CALL* release_dc)(uint64_t compatibility_handle, uint64_t device_context);
    /* Publishes what was drawn into device_context. */
    // ic: Fails invalid_argument (code: publish_dc needs a device context), unchanged.
    gf_status (GF_CALL* publish_dc)(uint64_t device_context);
    /* A short lease that serializes one write into device_context against
     * resizing and release; end_write publishes (1) or not (0). */
    // ic: Fails invalid_argument (code: begin_write needs a device context and an output), unchanged. Returns
    // ic: gf_paint_write_lease, owned by the caller.
    gf_status (GF_CALL* begin_write)(uint64_t device_context, gf_paint_write_lease* lease);
    // ic: Fails invalid_argument (code: end_write needs a lease and publish 0 or 1), unchanged.
    gf_status (GF_CALL* end_write)(gf_paint_write_lease lease, uint32_t publish);
} gf_windows_paint_endpoint_table;

/* The class tables (since 1.3): Windows Forms' protected On* hooks as
 * per-class C tables, and gf_classes_table. Generated by
 * tools/abi_class_tables.py; read as part of this header. */
#include "gui_forms/c/gf_abi_classes.h"

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

#include "gui_forms/c/gf_c_shield_end.h"
#endif
