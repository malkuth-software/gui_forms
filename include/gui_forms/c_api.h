#ifndef GUI_FORMS_C_API_H
#define GUI_FORMS_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(GF_C_API_BUILD)
#define GF_C_API_EXPORT __declspec(dllexport)
#else
#define GF_C_API_EXPORT __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GF_C_API_EXPORT __attribute__((visibility("default")))
#else
#define GF_C_API_EXPORT
#endif

/* Experimental 0.x ABI. Nothing in this header is a GUI.Forms 1.0 promise. */
#define GF_ABI_VERSION_0_1 UINT32_C(0x00000001)
#define GF_ABI_VERSION_0_2 UINT32_C(0x00000002)
#define GF_ABI_VERSION_0_3 UINT32_C(0x00000003)
#define GF_ABI_VERSION_0_4 UINT32_C(0x00000004)
#define GF_ABI_VERSION_0_5 UINT32_C(0x00000005)
#define GF_ABI_VERSION_0_6 UINT32_C(0x00000006)
#define GF_ABI_VERSION_0_7 UINT32_C(0x00000007)
#define GF_ABI_VERSION_0_8 UINT32_C(0x00000008)
#define GF_ABI_VERSION_0_9 UINT32_C(0x00000009)
#define GF_ABI_VERSION_0_10 UINT32_C(0x0000000a)
#define GF_ABI_VERSION_0_11 UINT32_C(0x0000000b)
#define GF_ABI_VERSION_0_12 UINT32_C(0x0000000c)
#define GF_ABI_VERSION_0_13 UINT32_C(0x0000000d)
#define GF_ABI_VERSION_0_14 UINT32_C(0x0000000e)
#define GF_ABI_VERSION_0_15 UINT32_C(0x0000000f)
#define GF_ABI_VERSION_0_16 UINT32_C(0x00000010)
#define GF_ABI_VERSION_0_17 UINT32_C(0x00000011)
#define GF_ABI_VERSION_0_18 UINT32_C(0x00000012)
#define GF_ABI_VERSION_0_19 UINT32_C(0x00000013)
#define GF_ABI_VERSION_0_20 UINT32_C(0x00000014)
#define GF_ABI_VERSION_0_21 UINT32_C(0x00000015)
#define GF_ABI_VERSION_0_22 UINT32_C(0x00000016)
#define GF_ABI_VERSION_0_23 UINT32_C(0x00000017)
#define GF_ABI_VERSION_0_24 UINT32_C(0x00000018)
#define GF_ABI_VERSION_0_25 UINT32_C(0x00000019)
#define GF_ABI_VERSION_0_26 UINT32_C(0x0000001a)

#define GF_PIXEL_FORMAT_BGRA32_PREMULTIPLIED UINT32_C(1)

typedef struct gf_handle {
    uint32_t slot;
    uint32_t generation;
} gf_handle;

typedef gf_handle gf_event_token;

typedef struct gf_string_view {
    const char* data;
    uint64_t size;
} gf_string_view;

typedef struct gf_rect {
    double x;
    double y;
    double width;
    double height;
} gf_rect;

typedef struct gf_point {
    double x;
    double y;
} gf_point;

typedef struct gf_size {
    double width;
    double height;
} gf_size;

typedef struct gf_scroll_axis_state {
    uint32_t enabled;
    uint32_t visible;
    double minimum;
    double maximum;
    double large_change;
    double small_change;
    double value;
} gf_scroll_axis_state;

typedef struct gf_scroll_state {
    uint32_t auto_scroll;
    gf_point position;
    gf_size margin;
    gf_size minimum_content_size;
    gf_rect display_rectangle;
    gf_rect viewport_rectangle;
    gf_scroll_axis_state horizontal;
    gf_scroll_axis_state vertical;
    uint64_t event_revision;
    uint32_t event_type;
    uint32_t event_orientation;
    double event_old_value;
    double event_new_value;
} gf_scroll_state;

typedef struct gf_layout_state {
    uint32_t suspend_depth;
    uint32_t deferred;
    uint64_t requested_revision;
    uint64_t committed_revision;
} gf_layout_state;

/*
 * ABI 0.23 property values are synchronous snapshots. Text output callbacks
 * write into caller-owned buffers; no managed or foreign pointer is retained.
 */
typedef struct gf_property_value {
    uint32_t kind;
    uint32_t boolean_value;
    int64_t signed_value;
    uint64_t unsigned_value;
    double number_value;
    uint32_t color_argb;
    gf_string_view text_value;
} gf_property_value;

typedef struct gf_property_enum_choice {
    gf_string_view name;
    int64_t value;
} gf_property_enum_choice;

typedef enum gf_property_value_kind {
    GF_PROPERTY_NULL = 0,
    GF_PROPERTY_BOOLEAN = 1,
    GF_PROPERTY_SIGNED_INTEGER = 2,
    GF_PROPERTY_UNSIGNED_INTEGER = 3,
    GF_PROPERTY_NUMBER = 4,
    GF_PROPERTY_TEXT = 5,
    GF_PROPERTY_COLOR = 10,
    GF_PROPERTY_ENUMERATION = 13
} gf_property_value_kind;

typedef enum gf_property_descriptor_flag {
    GF_PROPERTY_READABLE = 1 << 0,
    GF_PROPERTY_WRITABLE = 1 << 1,
    GF_PROPERTY_BROWSABLE = 1 << 2,
    GF_PROPERTY_NULLABLE = 1 << 3,
    GF_PROPERTY_STANDARD_VALUES_EXCLUSIVE = 1 << 4,
    GF_PROPERTY_RESETTABLE = 1 << 5,
    GF_PROPERTY_CHANGE_NOTIFICATIONS = 1 << 6,
    GF_PROPERTY_ENUM_FLAGS = 1 << 7
} gf_property_descriptor_flag;

typedef struct gf_property_descriptor_v1 {
    uint32_t struct_size;
    uint32_t kind;
    uint32_t flags;
    uint32_t reserved;
    gf_string_view name;
    gf_string_view category;
    gf_string_view description;
    gf_string_view enum_type_name;
    const gf_property_enum_choice* enum_choices;
    uint64_t enum_choice_count;
    const gf_property_value* standard_values;
    uint64_t standard_value_count;
    gf_string_view converter_name;
    gf_string_view editor_name;
} gf_property_descriptor_v1;

typedef uint32_t (*gf_property_get_callback)(
    void* context,
    gf_property_value* value,
    char* text_buffer,
    uint64_t text_capacity,
    uint64_t* required_text_size);
typedef uint32_t (*gf_property_set_callback)(
    void* context, const gf_property_value* value);
typedef uint32_t (*gf_property_reset_callback)(void* context);
typedef uint32_t (*gf_property_should_serialize_callback)(
    void* context, uint32_t* should_serialize);
typedef uint32_t (*gf_property_format_callback)(
    void* context,
    const gf_property_value* value,
    char* text_buffer,
    uint64_t text_capacity,
    uint64_t* required_text_size);
typedef uint32_t (*gf_property_parse_callback)(
    void* context,
    gf_string_view text,
    gf_property_value* value,
    char* value_text_buffer,
    uint64_t value_text_capacity,
    uint64_t* required_value_text_size);
typedef uint32_t (*gf_property_edit_callback)(
    void* context,
    const gf_property_value* current_value,
    gf_property_value* edited_value,
    char* edited_text_buffer,
    uint64_t edited_text_capacity,
    uint64_t* required_edited_text_size);

typedef struct gf_property_callbacks_v1 {
    uint32_t struct_size;
    uint32_t reserved;
    void* context;
    gf_property_get_callback get;
    gf_property_set_callback set;
    gf_property_reset_callback reset;
    gf_property_should_serialize_callback should_serialize;
    gf_property_format_callback format;
    gf_property_parse_callback parse;
    /* ABI 0.24 optional tail; inspect struct_size before reading it. */
    gf_property_edit_callback edit;
} gf_property_callbacks_v1;

typedef struct gf_field_edit_result {
    uint64_t anchor_utf8;
    uint64_t caret_utf8;
    uint64_t revision;
    uint32_t changed;
    uint32_t can_undo;
    uint32_t can_redo;
} gf_field_edit_result;

typedef enum gf_result {
    GF_OK = 0,
    GF_ERROR_INVALID_ARGUMENT = 1,
    GF_ERROR_STALE_HANDLE = 2,
    GF_ERROR_WRONG_HANDLE_KIND = 3,
    GF_ERROR_WRONG_THREAD = 4,
    GF_ERROR_DISPOSED = 5,
    GF_ERROR_BUFFER_TOO_SMALL = 6,
    GF_ERROR_UNSUPPORTED_VERSION = 7,
    GF_ERROR_INTERNAL = 8
} gf_result;

typedef enum gf_component_state {
    GF_COMPONENT_ALIVE = 0,
    GF_COMPONENT_DISPOSING = 1,
    GF_COMPONENT_DISPOSED = 2
} gf_component_state;

typedef enum gf_event_kind {
    GF_EVENT_STATE_CHANGED = 1,
    GF_EVENT_CLICKED = 2,
    GF_EVENT_FORM_CLOSING = 3,
    GF_EVENT_FORM_CLOSED = 4,
    GF_EVENT_MOUSE_MOVE = 5,
    GF_EVENT_MOUSE_DOWN = 6,
    GF_EVENT_MOUSE_UP = 7,
    GF_EVENT_MOUSE_WHEEL = 8,
    GF_EVENT_MOUSE_ENTER = 9,
    GF_EVENT_MOUSE_LEAVE = 10,
    GF_EVENT_KEY_DOWN = 11,
    GF_EVENT_KEY_UP = 12,
    GF_EVENT_TEXT_INPUT = 13,
    GF_EVENT_RANGE_VALUE_CHANGED = 14,
    GF_EVENT_RANGE_SCROLL = 15,
    GF_EVENT_BOUNDS_CHANGED = 16,
    GF_EVENT_SCROLL = 17
} gf_event_kind;

typedef enum gf_event_callback_result {
    GF_EVENT_CALLBACK_CONTINUE = 0,
    GF_EVENT_CALLBACK_CANCEL = 1,
    GF_EVENT_CALLBACK_FAULTED = 2
} gf_event_callback_result;

typedef enum gf_control_kind {
    GF_CONTROL_GENERIC = 0,
    GF_CONTROL_FORM = 1,
    GF_CONTROL_USER_CONTROL = 2,
    GF_CONTROL_PANEL = 3,
    GF_CONTROL_BUTTON = 4,
    GF_CONTROL_CHECK_BOX = 5,
    GF_CONTROL_COMBO_BOX = 6,
    GF_CONTROL_LABEL = 7,
    GF_CONTROL_LIST_BOX = 8,
    GF_CONTROL_TEXT_BOX = 9,
    GF_CONTROL_TRACK_BAR = 10,
    GF_CONTROL_RADIO_BUTTON = 11,
    GF_CONTROL_GROUP_BOX = 12,
    GF_CONTROL_PROGRESS_BAR = 13,
    GF_CONTROL_LINK_LABEL = 14,
    GF_CONTROL_PICTURE_BOX = 15,
    GF_CONTROL_DATA_GRID_VIEW = 16,
    GF_CONTROL_TOOL_STRIP = 17,
    GF_CONTROL_NUMERIC_UP_DOWN = 18,
    /* Retained layout participant which never becomes a pointer target. */
    GF_CONTROL_INPUT_TRANSPARENT = 19,
    /* Owner-painted retained surface which remains pointer transparent. */
    GF_CONTROL_INPUT_TRANSPARENT_CUSTOM = 20,
    GF_CONTROL_PROPERTY_GRID = 21,
    /* Nonvisual retained owner for foreign TypeDescriptor projections. */
    GF_CONTROL_PROPERTY_OBJECT_PROXY = 22,
    /* Owner-painted surface composited above ordinary retained and live content. */
    GF_CONTROL_OVERLAY_CUSTOM = 23,
    GF_CONTROL_CUSTOM = 0x7fffffff
} gf_control_kind;

typedef enum gf_cursor_kind {
    GF_CURSOR_INHERIT = 0,
    GF_CURSOR_ARROW = 1,
    GF_CURSOR_TEXT = 2,
    GF_CURSOR_HAND = 3,
    GF_CURSOR_CROSSHAIR = 4,
    GF_CURSOR_RESIZE_HORIZONTAL = 5,
    GF_CURSOR_RESIZE_VERTICAL = 6,
    GF_CURSOR_WAIT = 7,
    GF_CURSOR_FORBIDDEN = 8
} gf_cursor_kind;

typedef enum gf_window_run_flag {
    GF_WINDOW_RUN_DEFAULT = 0,
    GF_WINDOW_RUN_AUTOMATION_CLOSE = 1 << 0,
    GF_WINDOW_RUN_FORCE_HEADLESS = 1 << 1,
    GF_WINDOW_RUN_AUTOMATION_ACTIVATE = 1 << 2,
    /* Run an unattached custom surface as a transient retained overlay. */
    GF_WINDOW_RUN_POPUP = 1 << 3
} gf_window_run_flag;

typedef enum gf_path_dialog_kind {
    GF_PATH_DIALOG_OPEN_FILE = 1,
    GF_PATH_DIALOG_SAVE_FILE = 2,
    GF_PATH_DIALOG_SELECT_FOLDER = 3
} gf_path_dialog_kind;

typedef enum gf_path_dialog_flag {
    GF_PATH_DIALOG_DEFAULT = 0,
    GF_PATH_DIALOG_ALLOW_MULTIPLE = 1 << 0,
    GF_PATH_DIALOG_CONFIRM_OVERWRITE = 1 << 1
} gf_path_dialog_flag;

typedef void (*gf_event_callback)(gf_handle sender, uint32_t event_kind, void* context);
typedef uint32_t (*gf_event_callback_v2)(gf_handle sender,
                                         uint32_t event_kind,
                                         void* context);
typedef uint32_t (*gf_dispatch_callback)(void* context, uint32_t cancelled);
typedef uint32_t (*gf_pointer_callback)(gf_handle sender,
                                        uint32_t event_kind,
                                        double x,
                                        double y,
                                        double wheel_delta,
                                        uint32_t button,
                                        void* context);
typedef uint32_t (*gf_key_callback)(gf_handle sender,
                                    uint32_t event_kind,
                                    uint32_t physical_key,
                                    uint32_t modifiers,
                                    uint32_t repeat,
                                    void* context);
typedef uint32_t (*gf_text_callback)(gf_handle sender,
                                     gf_string_view text,
                                     uint32_t composing,
                                     int32_t replacement_start,
                                     int32_t replacement_length,
                                     void* context);

typedef struct gf_error_view {
    uint32_t code;
    gf_string_view message;
} gf_error_view;

typedef struct gf_api_v0 {
    uint32_t struct_size;
    uint32_t abi_version;

    gf_result (*last_error)(gf_error_view* error);
    gf_result (*control_create)(gf_string_view stable_id, gf_handle* control);
    gf_result (*retain)(gf_handle handle);
    gf_result (*release)(gf_handle handle);
    gf_result (*dispose)(gf_handle handle);
    gf_result (*component_state)(gf_handle handle, uint32_t* state);
    gf_result (*stable_id)(gf_handle handle,
                           char* buffer,
                           uint64_t capacity,
                           uint64_t* required_size);
    gf_result (*set_visible)(gf_handle handle, uint32_t visible);
    gf_result (*get_visible)(gf_handle handle, uint32_t* visible);
    gf_result (*set_bounds)(gf_handle handle, gf_rect bounds);
    gf_result (*get_bounds)(gf_handle handle, gf_rect* bounds);
    gf_result (*add_child)(gf_handle parent, gf_handle child);
    gf_result (*remove_child)(gf_handle parent, gf_handle child);
    gf_result (*subscribe)(gf_handle sender,
                           uint32_t event_kind,
                           gf_event_callback callback,
                           void* context,
                           gf_event_token* token);
    gf_result (*disconnect)(gf_event_token token);

    /* ABI 0.2 additions. The 0.1 table is the prefix ending at disconnect. */
    gf_result (*control_create_kind)(uint32_t kind,
                                     gf_string_view stable_id,
                                     gf_handle* control);
    gf_result (*set_name)(gf_handle handle, gf_string_view name);
    gf_result (*get_name)(gf_handle handle,
                          char* buffer,
                          uint64_t capacity,
                          uint64_t* required_size);
    gf_result (*set_text)(gf_handle handle, gf_string_view text);
    gf_result (*get_text)(gf_handle handle,
                          char* buffer,
                          uint64_t capacity,
                          uint64_t* required_size);
    gf_result (*set_enabled)(gf_handle handle, uint32_t enabled);
    gf_result (*get_enabled)(gf_handle handle, uint32_t* enabled);

    /* ABI 0.3 additions. */
    gf_result (*run_window)(gf_handle form, uint32_t flags);
    gf_result (*last_host_trace)(gf_handle form,
                                 char* buffer,
                                 uint64_t capacity,
                                 uint64_t* required_size);

    /* ABI 0.4 additions. */
    gf_result (*subscribe_v2)(gf_handle sender,
                              uint32_t event_kind,
                              gf_event_callback_v2 callback,
                              void* context,
                              gf_event_token* token);
    gf_result (*begin_invoke)(gf_handle control,
                              gf_dispatch_callback callback,
                              void* context);
    gf_result (*request_close)(gf_handle form);
    gf_result (*callback_fault_count)(gf_handle control, uint64_t* count);

    /* ABI 0.5 addition: retained owner-draw compatibility raster. */
    gf_result (*set_control_png)(gf_handle control,
                                 const uint8_t* encoded_png,
                                 uint64_t encoded_size);
    gf_result (*set_child_index)(gf_handle parent,
                                 gf_handle child,
                                 uint64_t index);
    gf_result (*set_control_colors)(gf_handle control,
                                    uint32_t foreground_argb,
                                    uint32_t background_argb);

    /* ABI 0.6 addition: pointer delivery for retained owner-draw controls. */
    gf_result (*subscribe_pointer)(gf_handle sender,
                                   gf_pointer_callback callback,
                                   void* context,
                                   gf_event_token* token);

    /* ABI 0.7 additions: native/managed checked-state projection. */
    gf_result (*set_check_state)(gf_handle control, uint32_t check_state);
    gf_result (*get_check_state)(gf_handle control, uint32_t* check_state);

    /* ABI 0.8 additions: focused key and composed text delivery. */
    gf_result (*subscribe_key)(gf_handle sender,
                               gf_key_callback callback,
                               void* context,
                               gf_event_token* token);
    gf_result (*subscribe_text)(gf_handle sender,
                                gf_text_callback callback,
                                void* context,
                                gf_event_token* token);

    /* ABI 0.9 additions: retained range state and explicit pointer capture. */
    gf_result (*set_range)(gf_handle control, double minimum, double maximum);
    gf_result (*get_range)(gf_handle control, double* minimum, double* maximum);
    gf_result (*set_range_value)(gf_handle control, double value);
    gf_result (*get_range_value)(gf_handle control, double* value);
    gf_result (*set_pointer_capture)(gf_handle control, uint32_t captured);
    gf_result (*get_pointer_capture)(gf_handle control, uint32_t* captured);

    /* ABI 0.10 additions: portable common-dialog and tooltip host adapters. */
    gf_result (*show_path_dialog)(gf_handle owner,
                                  uint32_t kind,
                                  gf_string_view title,
                                  gf_string_view initial_directory,
                                  gf_string_view suggested_name,
                                  gf_string_view default_extension,
                                  gf_string_view filter,
                                  uint32_t flags,
                                  uint32_t* accepted);
    gf_result (*last_dialog_path)(gf_handle owner,
                                  char* buffer,
                                  uint64_t capacity,
                                  uint64_t* required_size);
    gf_result (*show_tooltip)(gf_handle owner,
                              gf_string_view text,
                              double x,
                              double y,
                              uint32_t duration_milliseconds);
    gf_result (*hide_tooltip)(gf_handle owner);

    /* ABI 0.11 addition: retained field selection and caret projection. */
    gf_result (*set_field_selection)(gf_handle control,
                                     uint64_t selection_start_utf8,
                                     uint64_t selection_length_utf8,
                                     uint32_t caret_visible);

    /* ABI 0.12 additions: directional edit state and renderer-owned hit test. */
    gf_result (*set_field_edit_state)(gf_handle control,
                                      uint64_t anchor_utf8,
                                      uint64_t caret_utf8,
                                      uint32_t caret_visible);
    gf_result (*field_position_from_point)(gf_handle control,
                                           double local_x,
                                           uint64_t* position_utf8);

    /* ABI 0.13 additions: bounded host clipboard text transport. */
    gf_result (*write_clipboard_text)(gf_handle owner, gf_string_view text);
    gf_result (*read_clipboard_text)(gf_handle owner,
                                     char* buffer,
                                     uint64_t capacity,
                                     uint64_t* required_size,
                                     uint32_t* has_text);

    /* ABI 0.14 addition: grapheme-safe field navigation. Direction is -1/+1. */
    gf_result (*field_navigate)(gf_handle control,
                                uint64_t position_utf8,
                                int32_t direction,
                                uint64_t* result_utf8);

    /* ABI 0.15 additions: native field mutation and deterministic history. */
    gf_result (*field_replace)(gf_handle control,
                               uint64_t start_utf8,
                               uint64_t length_utf8,
                               gf_string_view replacement,
                               gf_field_edit_result* result);
    gf_result (*field_history)(gf_handle control,
                               int32_t direction,
                               gf_field_edit_result* result);
    gf_result (*field_clear_history)(gf_handle control);

    /* ABI 0.16 addition: owned, unencoded renderer-neutral paint surface. */
    gf_result (*set_control_pixels)(gf_handle control,
                                    const uint8_t* pixels,
                                    uint32_t width,
                                    uint32_t height,
                                    uint64_t row_bytes,
                                    uint32_t pixel_format);

    /* ABI 0.17 addition: renderer-authoritative bounds in root client space. */
    gf_result (*get_control_absolute_bounds)(gf_handle control,
                                             gf_rect* bounds);

    /*
     * ABI 0.18 addition: form-level preview for dialog/focus-scope keys.
     * Returning GF_EVENT_CALLBACK_CANCEL marks the key handled before it
     * reaches the focused descendant; CONTINUE preserves ordinary routing.
     */
    gf_result (*subscribe_key_preview)(gf_handle form,
                                       gf_key_callback callback,
                                       void* context,
                                       gf_event_token* token);

    /* ABI 0.19 additions: inherited retained cursor projection. */
    gf_result (*set_cursor)(gf_handle control, uint32_t cursor_kind);
    gf_result (*get_cursor)(gf_handle control, uint32_t* cursor_kind);

    /* ABI 0.20 additions: retained two-axis ScrollableControl projection. */
    gf_result (*set_auto_scroll_offset)(gf_handle control, gf_point offset);
    gf_result (*set_auto_scroll)(gf_handle control, uint32_t enabled);
    gf_result (*set_auto_scroll_margin)(gf_handle control, gf_size margin);
    gf_result (*set_auto_scroll_min_size)(gf_handle control, gf_size size);
    gf_result (*set_auto_scroll_position)(gf_handle control, gf_point position);
    gf_result (*get_scroll_state)(gf_handle control, gf_scroll_state* state);
    gf_result (*set_scroll_axis_state)(gf_handle control,
                                       uint32_t orientation,
                                       gf_scroll_axis_state state);
    gf_result (*scroll_control_into_view)(gf_handle control, gf_handle child);

    /* ABI 0.21 additions: retained per-control layout transactions. */
    gf_result (*suspend_layout)(gf_handle control);
    gf_result (*resume_layout)(gf_handle control, uint32_t perform_layout);
    gf_result (*perform_control_layout)(gf_handle control);
    gf_result (*get_layout_state)(gf_handle control, gf_layout_state* state);

    /* ABI 0.22 additions: native PropertyGrid managed-facade projection. */
    gf_result (*property_grid_set_selected_controls)(
        gf_handle property_grid,
        const gf_handle* controls,
        uint64_t count);
    gf_result (*property_grid_set_sort)(gf_handle property_grid,
                                        uint32_t property_sort);
    gf_result (*property_grid_get_sort)(gf_handle property_grid,
                                        uint32_t* property_sort);
    gf_result (*property_grid_refresh)(gf_handle property_grid);

    /*
     * ABI 0.23 additions: bounded foreign-object property metadata and typed
     * callbacks. Definitions are deep-copied during this call. Callback
     * context remains caller-owned and must outlive the proxy handle.
     */
    gf_result (*property_object_define)(
        gf_handle property_object,
        const gf_property_descriptor_v1* descriptor,
        const gf_property_callbacks_v1* callbacks);
    gf_result (*property_object_notify_changed)(
        gf_handle property_object,
        gf_string_view property_name);
    gf_result (*property_grid_try_set_text)(
        gf_handle property_grid,
        gf_string_view property_name,
        gf_string_view text,
        uint32_t* committed);
    gf_result (*property_grid_reset_property)(
        gf_handle property_grid,
        gf_string_view property_name,
        uint32_t* committed);

    /* ABI 0.24: activate the retained editor for one projected property. */
    gf_result (*property_grid_activate_editor)(
        gf_handle property_grid,
        gf_string_view property_name,
        uint32_t* activated);

    /* ABI 0.25: retained presentation properties used by facade controls. */
    gf_result (*set_control_text_alignment)(gf_handle control,
                                            uint32_t content_alignment);
    gf_result (*set_button_appearance)(gf_handle button,
                                       uint32_t visual_style,
                                       double flat_border_width);
    gf_result (*set_panel_border_style)(gf_handle panel,
                                        uint32_t border_style);

    /*
     * ABI 0.26: attach a detached retained root to the window-owned overlay
     * composition lane. The owner remains the lifetime/input authority; the
     * overlay is laid out in root-client coordinates and is not a layout child.
     */
    gf_result (*attach_overlay)(gf_handle owner, gf_handle overlay);
    gf_result (*detach_overlay)(gf_handle overlay);
} gf_api_v0;

/*
 * The caller sets table->struct_size before entry. The implementation copies
 * only the common prefix and reports its complete table size on success.
 */
GF_C_API_EXPORT gf_result gf_get_api_v0(uint32_t requested_version,
                                        gf_api_v0* table);

/*
 * Windows-only compatibility paint endpoint extension. This is intentionally
 * outside the portable retained-control ABI table: the returned value is a
 * virtual WinForms handle understood by the compatibility shim, not an HWND or
 * a GUI.Forms visual/input surface. Width/height changes preserve the token.
 */
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_acquire_v1(
    gf_handle control,
    uint32_t width,
    uint32_t height,
    uint64_t* endpoint,
    uintptr_t* compatibility_handle);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_configure_v1(
    uint64_t endpoint,
    uint32_t width,
    uint32_t height);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_touch_v1(
    uint64_t endpoint,
    uint32_t explicit_boundary);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_drain_v1(
    uint64_t endpoint);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_snapshot_v1(
    uint64_t endpoint,
    char* buffer,
    uint64_t capacity,
    uint64_t* required_size);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_submit_bgra_v1(
    uintptr_t compatibility_handle,
    uint32_t width,
    uint32_t height,
    uint64_t row_bytes,
    const void* pixels);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_release_v1(
    uint64_t endpoint);
/* Internal compatibility-shim bridge. Unknown handles return STALE_HANDLE so
 * the shim can fall through to the operating system implementation. */
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_get_dc_v1(
    uintptr_t compatibility_handle,
    uintptr_t* device_context);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_release_dc_v1(
    uintptr_t compatibility_handle,
    uintptr_t device_context);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_publish_dc_v1(
    uintptr_t device_context);
/* Short operation lease for compatibility destination writes. The lease owns
 * the endpoint and serializes only the final GDI write/publish against resize
 * and release; callers must end it synchronously on the acquiring thread. */
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_begin_write_v1(
    uintptr_t device_context,
    uint64_t* write_lease);
GF_C_API_EXPORT gf_result gf_windows_paint_endpoint_end_write_v1(
    uint64_t write_lease,
    uint32_t publish);


#ifdef __cplusplus
}
#endif

#endif
