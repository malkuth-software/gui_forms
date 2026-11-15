#ifndef GUI_FORMS_DRAWING_C_API_H
#define GUI_FORMS_DRAWING_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The drawing ABI is part of the C ABI library (gui_forms_abi), the one
 * self-contained shared library a C or C# program loads. GD_C_API_BUILD is
 * defined while compiling it; GD_C_API_STATIC by a program that links the
 * static test archive (gui_drawing_c_api), which imports nothing. */
#if defined(_WIN32)
#if defined(GD_C_API_BUILD)
#define GD_C_API_EXPORT __declspec(dllexport)
#elif defined(GD_C_API_STATIC)
#define GD_C_API_EXPORT
#else
#define GD_C_API_EXPORT __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GD_C_API_EXPORT __attribute__((visibility("default")))
#else
#define GD_C_API_EXPORT
#endif

/*
 * GUI.Forms.Drawing, the gd_* ABI: GUI.Forms' drawing library, in the role
 * WinForms' drawing library plays for WinForms. Sealed at 1.0
 * (GD_ABI_VERSION_1_0).
 *
 * Versions and the table. gd_get_api_v0 is the one exported function. The
 * caller sets table->struct_size to the size it declares (at least the two
 * leading fields) and asks for one version. The library copies the common
 * prefix of the caller's table and that version's table, then writes the
 * version's full table size to struct_size and the version to abi_version.
 * The table only ever grows at its end: an entry's slot never moves or
 * changes meaning, and each version's table is a prefix of the next one's.
 * A version the library does not have fails GD_ERROR_UNSUPPORTED_VERSION; a
 * caller built against a newer header retries with an older version. From
 * 1.0 a version is major << 16 | minor; a new minor only appends, and a new
 * major would be a new entry point (gd_get_api_v<n>). The
 * 0.x versions stay accepted with their old prefixes.
 *
 * Results. Every entry returns a gd_result. A caller treats any code other
 * than GD_OK, including one this header does not name, as a failure.
 * last_error describes the calling thread's most recent failure and is
 * meaningful only right after a call that failed: a success does not clear
 * it. A refused call changes nothing unless its entry says otherwise.
 *
 * Handles. A gd_handle names one object: {0, 0} is the null handle, accepted
 * only where an entry says so (a format handle meaning defaults), and every
 * other use of it fails GD_ERROR_STALE_HANDLE. A create entry returns a
 * handle holding one reference; retain adds one and release removes one, and
 * at zero the object ends and its handle goes stale. dispose ends the object
 * at once while the handle stays valid until its last release: later calls
 * on it fail GD_ERROR_DISPOSED, and object_state reports DISPOSED. A stale
 * handle fails GD_ERROR_STALE_HANDLE and a handle of another kind
 * GD_ERROR_WRONG_HANDLE_KIND. A released slot is reused with its generation
 * advanced (skipping 0), so a stale handle can name a new object only after
 * its slot has been reused 2^32 times. Recorded commands hold copies of the
 * pens, brushes, fonts, formats, paths and images they name, taken at record
 * time: releasing or changing those objects afterwards does not change a
 * recording.
 *
 * Threads. Any thread may call. Calls on one object are serialized: each
 * holds the object for the call. An entry that reads or changes an object
 * (retain and release included) moves it to the calling thread first when
 * another thread used it last; dispose, object_state and object_kind do not
 * move it, and fail GD_ERROR_WRONG_THREAD on a thread other than the
 * object's last one. last_error and the faces font_resolve registers are
 * per thread.
 *
 * Memory. Arrays, strings (gd_string_view: UTF-8 with a byte count; NUL
 * bytes are refused) and records an entry reads are borrowed for the call
 * and copied; nothing a caller passes is kept. Views an entry returns are
 * valid as the entry says (until a lease's token is returned, or while the
 * library is loaded). Size queries take a buffer and its capacity, write the
 * size required and fail GD_ERROR_BUFFER_TOO_SMALL when it does not fit.
 *
 * Records. The records in this header are frozen at 1.0: a field is never
 * added to one. A new field means a new record and a new entry, and every
 * record added after 1.0 starts with a uint32_t struct_size the caller sets,
 * as gd_font_resolution does.
 *
 * Windows entries. bitmap_export_hbitmap, bitmap_import_hbitmap,
 * native_surface_capture, native_surface_present, bitmap_acquire_hdc,
 * bitmap_release_hdc, native_surface_refresh and
 * native_surface_publish_retained work on Windows only; elsewhere they fail
 * GD_ERROR_NOT_AVAILABLE. Their native handles travel as uintptr_t.
 *
 * Names. Entries are <subject>_<verb>; the 0.x gradient setters that drop
 * "brush" keep their names, with <kind>_gradient_brush_set_* aliases from
 * 1.0 on the same functions.
 */
#define GD_ABI_VERSION_0_1 UINT32_C(0x00000001)
#define GD_ABI_VERSION_0_2 UINT32_C(0x00000002)
/* 0.3 appends the drawing primitives (pen_set_line_join onwards). Each
   version exposes the table prefix it introduced; a newer version only
   appends. */
#define GD_ABI_VERSION_0_3 UINT32_C(0x00000003)
/* 0.4 appends the gapped and per-element marks (recorder_fill_band
   onwards). */
#define GD_ABI_VERSION_0_4 UINT32_C(0x00000004)
/* 0.5 appends text in a box, batched strings, recorder reuse and the fitted
   measure (recorder_draw_string_in_rect onwards). */
#define GD_ABI_VERSION_0_5 UINT32_C(0x00000005)
/* 0.6 appends the curved path vocabulary (graphics_path_add_quadratic
   onwards), for consumers drawing their own curves (exhibit gap E7). */
#define GD_ABI_VERSION_0_6 UINT32_C(0x00000006)
/* 0.7 appends text at any angle (recorder_draw_string_rotated onwards). */
#define GD_ABI_VERSION_0_7 UINT32_C(0x00000007)
/* 1.0, the seal: the whole table (recorder_paint_target, the bridge to a
   GUI.Forms paint target, and the gradient brush aliases onwards). */
#define GD_ABI_VERSION_1_0 UINT32_C(0x00010000)
/* 1.1, provisional, appends path flattening (graphics_path_flatten)
   and then the raster's entry (raster_initialize), which replaces the
   former gdr_initialize export. The number is a placeholder that the
   integrator assigns in fold order; these slots may still change until it
   is fixed. */
#define GD_ABI_VERSION_1_1 UINT32_C(0x00010001)

typedef struct gd_handle {
    uint32_t slot;
    uint32_t generation;
} gd_handle;

typedef struct gd_string_view {
    const char* data;
    uint64_t size;
} gd_string_view;

typedef struct gd_color {
    uint32_t argb;
    uint32_t is_empty;
} gd_color;

typedef struct gd_point {
    double x;
    double y;
} gd_point;

typedef struct gd_size {
    double width;
    double height;
} gd_size;

typedef struct gd_rect {
    double x;
    double y;
    double width;
    double height;
} gd_rect;

/* One string of recorder_draw_strings: UTF-8 text (borrowed for the call),
   its origin and its colour. */
typedef struct gd_text_item {
    gd_string_view text;
    gd_point origin;
    gd_color color;
} gd_text_item;

typedef struct gd_rect_i {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
} gd_rect_i;

typedef struct gd_color_remap {
    gd_color old_color;
    gd_color new_color;
} gd_color_remap;

typedef struct gd_matrix {
    double m11;
    double m12;
    double m21;
    double m22;
    double dx;
    double dy;
} gd_matrix;

typedef enum gd_result {
    GD_OK = 0,
    GD_ERROR_INVALID_ARGUMENT = 1,
    GD_ERROR_STALE_HANDLE = 2,
    GD_ERROR_WRONG_HANDLE_KIND = 3,
    GD_ERROR_WRONG_THREAD = 4,
    GD_ERROR_DISPOSED = 5,
    GD_ERROR_BUFFER_TOO_SMALL = 6,
    GD_ERROR_UNSUPPORTED_VERSION = 7,
    /* A count, size or other quantity above its limit. */
    GD_ERROR_LIMIT_EXCEEDED = 8,
    GD_ERROR_INTERNAL = 9,
    /* The object is in a state that refuses the call: a closed recorder, a
       bitmap under a lock, an edit or a device-context lease (1.0). */
    GD_ERROR_INVALID_STATE = 10,
    /* Memory ran out (1.0). */
    GD_ERROR_OUT_OF_MEMORY = 11,
    /* The entry cannot work here: a Windows entry on another host, or an
       entry that needs the raster before raster_initialize has installed
       it, or in a library built without it (1.0). */
    GD_ERROR_NOT_AVAILABLE = 12
} gd_result;

typedef enum gd_object_state {
    GD_OBJECT_ALIVE = 0,
    GD_OBJECT_DISPOSING = 1,
    GD_OBJECT_DISPOSED = 2
} gd_object_state;

typedef enum gd_object_kind {
    GD_OBJECT_SOLID_BRUSH = 1,
    GD_OBJECT_PEN = 2,
    GD_OBJECT_FONT = 3,
    GD_OBJECT_STRING_FORMAT = 4,
    GD_OBJECT_RECORDER = 5,
    GD_OBJECT_GRAPHICS_PATH = 6,
    GD_OBJECT_IMAGE_REFERENCE = 7,
    GD_OBJECT_IMAGE_ATTRIBUTES = 8,
    GD_OBJECT_BITMAP = 9,
    GD_OBJECT_HATCH_BRUSH = 10,
    GD_OBJECT_LINEAR_GRADIENT_BRUSH = 11,
    GD_OBJECT_PATH_GRADIENT_BRUSH = 12,
    GD_OBJECT_REGION = 13,
    /* 0.3 */
    GD_OBJECT_CONIC_GRADIENT_BRUSH = 14,
    GD_OBJECT_CHECKER_BRUSH = 15,
    /* Reserved for a depth-composited bitmap (a picture with a depth plane,
       so a 3D view's overlay can depth-test 2D marks); no entry creates one
       yet, and no other kind will take the value. */
    GD_OBJECT_DEPTH_BITMAP_RESERVED = 16
} gd_object_kind;

typedef enum gd_wrap_mode {
    GD_WRAP_TILE = 0,
    GD_WRAP_TILE_FLIP_X = 1,
    GD_WRAP_TILE_FLIP_Y = 2,
    GD_WRAP_TILE_FLIP_XY = 3,
    GD_WRAP_CLAMP = 4
} gd_wrap_mode;

typedef enum gd_hatch_style {
    GD_HATCH_HORIZONTAL = 0,
    GD_HATCH_VERTICAL = 1,
    GD_HATCH_FORWARD_DIAGONAL = 2,
    GD_HATCH_BACKWARD_DIAGONAL = 3,
    GD_HATCH_CROSS = 4,
    GD_HATCH_DIAGONAL_CROSS = 5
} gd_hatch_style;

typedef enum gd_dash_style {
    GD_DASH_SOLID = 0,
    GD_DASH_DASH = 1,
    GD_DASH_DOT = 2,
    GD_DASH_DASH_DOT = 3,
    GD_DASH_DASH_DOT_DOT = 4,
    GD_DASH_CUSTOM = 5
} gd_dash_style;

/* How a pen's segments meet and how its open ends are capped. */
typedef enum gd_line_join {
    GD_LINE_JOIN_MITER = 0,
    GD_LINE_JOIN_BEVEL = 1,
    GD_LINE_JOIN_ROUND = 2
} gd_line_join;

typedef enum gd_line_cap {
    GD_LINE_CAP_FLAT = 0,
    GD_LINE_CAP_SQUARE = 1,
    GD_LINE_CAP_ROUND = 2
} gd_line_cap;

/* Marker glyphs; CIRCLE to TRIANGLE_RIGHT are filled, CROSS to ASTERISK
   are strokes in the edge colour. */
typedef enum gd_marker_shape {
    GD_MARKER_CIRCLE = 0,
    GD_MARKER_SQUARE = 1,
    GD_MARKER_DIAMOND = 2,
    GD_MARKER_TRIANGLE_UP = 3,
    GD_MARKER_TRIANGLE_DOWN = 4,
    GD_MARKER_TRIANGLE_LEFT = 5,
    GD_MARKER_TRIANGLE_RIGHT = 6,
    GD_MARKER_CROSS = 7,
    GD_MARKER_PLUS = 8,
    GD_MARKER_ASTERISK = 9
} gd_marker_shape;

/* The edge a proportional fill grows from. */
typedef enum gd_fill_direction {
    GD_FILL_LEFT_TO_RIGHT = 0,
    GD_FILL_RIGHT_TO_LEFT = 1,
    GD_FILL_BOTTOM_TO_TOP = 2,
    GD_FILL_TOP_TO_BOTTOM = 3
} gd_fill_direction;

/* A marker centred on each point: size its full width (finite, positive),
   fill its inside, edge its outline (or lines) edge_width wide (finite,
   non-negative). An empty colour or a zero width draws nothing of that
   part. */
typedef struct gd_marker_style {
    uint32_t shape;
    double size;
    gd_color fill;
    gd_color edge;
    double edge_width;
} gd_marker_style;

/* A world-aligned grid: lines spacing apart (at least 2) through origin,
   every major_every-th line (1 or more) in major and the rest in minor,
   each line_width wide (finite, positive). */
typedef struct gd_grid_pattern {
    gd_point origin;
    double spacing;
    uint32_t major_every;
    gd_color minor;
    gd_color major;
    double line_width;
} gd_grid_pattern;

/* A rounded rectangle's corner radii, finite and non-negative; each is held
   to half the rectangle's shorter side. */
typedef struct gd_corner_radii {
    double top_left;
    double top_right;
    double bottom_right;
    double bottom_left;
} gd_corner_radii;

/* font_create's style: a combination of these bits (other bits refused). */
typedef enum gd_font_style {
    GD_FONT_STYLE_REGULAR = 0,
    GD_FONT_STYLE_BOLD = 1,
    GD_FONT_STYLE_ITALIC = 2,
    GD_FONT_STYLE_UNDERLINE = 4,
    GD_FONT_STYLE_STRIKEOUT = 8
} gd_font_style;

/* font_create's unit, the unit of its size. */
typedef enum gd_font_unit {
    GD_FONT_UNIT_WORLD = 0,
    GD_FONT_UNIT_DISPLAY = 1,
    GD_FONT_UNIT_PIXEL = 2,
    GD_FONT_UNIT_POINT = 3,
    GD_FONT_UNIT_INCH = 4,
    GD_FONT_UNIT_DOCUMENT = 5,
    GD_FONT_UNIT_MILLIMETER = 6
} gd_font_unit;

/* font_create's charset: a Windows character set number, 0 to 255, kept
   with the font and not used to choose faces. The two usual values: */
#define GD_FONT_CHARSET_ANSI UINT32_C(0)
#define GD_FONT_CHARSET_DEFAULT UINT32_C(1)

/* string_format_create's and string_format_set's flags: a combination of
   these bits. Other bits are kept with the format and have no effect. */
#define GD_STRING_FORMAT_FIT_BLACK_BOX UINT32_C(0x0004)
#define GD_STRING_FORMAT_NO_WRAP UINT32_C(0x1000)
#define GD_STRING_FORMAT_NO_CLIP UINT32_C(0x4000)

/* recorder_set_quality's five modes, in its parameter order. */
typedef enum gd_smoothing_mode {
    GD_SMOOTHING_DEFAULT = 0,
    GD_SMOOTHING_HIGH_SPEED = 1,
    GD_SMOOTHING_HIGH_QUALITY = 2,
    GD_SMOOTHING_NONE = 3,
    GD_SMOOTHING_ANTI_ALIAS = 4
} gd_smoothing_mode;

typedef enum gd_interpolation_mode {
    GD_INTERPOLATION_DEFAULT = 0,
    GD_INTERPOLATION_LOW = 1,
    GD_INTERPOLATION_HIGH = 2,
    GD_INTERPOLATION_NEAREST = 3,
    GD_INTERPOLATION_BILINEAR = 4,
    GD_INTERPOLATION_BICUBIC = 5,
    GD_INTERPOLATION_HIGH_QUALITY_BILINEAR = 6,
    GD_INTERPOLATION_HIGH_QUALITY_BICUBIC = 7
} gd_interpolation_mode;

typedef enum gd_pixel_offset_mode {
    GD_PIXEL_OFFSET_DEFAULT = 0,
    GD_PIXEL_OFFSET_HIGH_SPEED = 1,
    GD_PIXEL_OFFSET_HIGH_QUALITY = 2,
    GD_PIXEL_OFFSET_NONE = 3,
    GD_PIXEL_OFFSET_HALF = 4
} gd_pixel_offset_mode;

typedef enum gd_compositing_mode {
    GD_COMPOSITING_SOURCE_OVER = 0,
    GD_COMPOSITING_SOURCE_COPY = 1
} gd_compositing_mode;

typedef enum gd_compositing_quality {
    GD_COMPOSITING_QUALITY_DEFAULT = 0,
    GD_COMPOSITING_QUALITY_HIGH_SPEED = 1,
    GD_COMPOSITING_QUALITY_HIGH_QUALITY = 2,
    GD_COMPOSITING_QUALITY_GAMMA_CORRECTED = 3,
    GD_COMPOSITING_QUALITY_ASSUME_LINEAR = 4
} gd_compositing_quality;

/* graphics_path_add_path's connect: whether the appended path's first
   figure joins the path's last one. */
#define GD_PATH_NO_CONNECT UINT32_C(0)
#define GD_PATH_CONNECT UINT32_C(1)

/* string_format_set's alignment and line_alignment (across and down). */
typedef enum gd_string_alignment {
    GD_STRING_NEAR = 0,
    GD_STRING_CENTER = 1,
    GD_STRING_FAR = 2
} gd_string_alignment;

typedef enum gd_string_trimming {
    GD_TRIM_NONE = 0,
    GD_TRIM_CHARACTER = 1,
    GD_TRIM_WORD = 2,
    GD_TRIM_ELLIPSIS_CHARACTER = 3,
    GD_TRIM_ELLIPSIS_WORD = 4,
    GD_TRIM_ELLIPSIS_PATH = 5
} gd_string_trimming;

typedef enum gd_fill_mode {
    GD_FILL_ALTERNATE = 0,
    GD_FILL_WINDING = 1
} gd_fill_mode;

/* Bitmaps and images are premultiplied BGRA8 (blue, green, red, alpha in
   memory) on every host. */
typedef enum gd_pixel_format {
    GD_PIXEL_BGRA32_PREMULTIPLIED = 0
} gd_pixel_format;

typedef enum gd_bitmap_lock_mode {
    GD_BITMAP_LOCK_READ = 0,
    GD_BITMAP_LOCK_WRITE = 1,
    GD_BITMAP_LOCK_READ_WRITE = 2
} gd_bitmap_lock_mode;

/* The native surfaces of the Windows entries. */
typedef enum gd_native_surface_kind {
    GD_NATIVE_SURFACE_HDC = 0,
    GD_NATIVE_SURFACE_HWND = 1
} gd_native_surface_kind;

typedef struct gd_bitmap_lock_view {
    const void* data;
    void* writable_data;
    uint64_t row_bytes;
    uint32_t width;
    uint32_t height;
    uint32_t pixel_format;
    uint64_t token;
} gd_bitmap_lock_view;

typedef struct gd_bitmap_edit_view {
    const void* data;
    void* writable_data;
    uint64_t row_bytes;
    gd_rect_i bounds;
    uint32_t pixel_format;
    uint64_t token;
} gd_bitmap_edit_view;

typedef struct gd_bitmap_damage_summary {
    uint64_t from_generation;
    uint64_t to_generation;
    uint32_t history_complete;
} gd_bitmap_damage_summary;

typedef struct gd_error_view {
    uint32_t code;
    gd_string_view message;
} gd_error_view;

/*
 * Stage F font resolution: how a font's requested family was answered.
 * BUNDLED to GENERIC come from the substitution table
 * (assets/fonts/font-substitution.json); a family it does not name is
 * UNMAPPED until a raster module asks the host, which makes it HOST (a host
 * face with the requested name) or CONTENT (the content family substitutes).
 */
typedef enum gd_font_source {
    GD_FONT_SOURCE_BUNDLED = 0,
    GD_FONT_SOURCE_SUBSTITUTION = 1,
    GD_FONT_SOURCE_ALIAS = 2,
    GD_FONT_SOURCE_GENERIC = 3,
    GD_FONT_SOURCE_UNMAPPED = 4,
    GD_FONT_SOURCE_HOST = 5,
    GD_FONT_SOURCE_CONTENT = 6
} gd_font_source;

typedef enum gd_font_mapping {
    GD_FONT_MAPPING_NONE = 0,
    GD_FONT_MAPPING_METRIC_COMPATIBLE = 1,
    GD_FONT_MAPPING_NEAREST = 2
} gd_font_mapping;

/*
 * The caller sets struct_size. family and face_file point into the static
 * table of the library that answered and stay valid while it is loaded;
 * both are empty for UNMAPPED and HOST.
 * The styles are the bold (1) and italic (2) bits: face_style differs from
 * requested_style when the family lacks that face and its nearest is drawn.
 * face_loaded is 1 when the raster module registered the face for drawing on
 * the calling thread.
 */
typedef struct gd_font_resolution {
    uint32_t struct_size;
    uint32_t source;
    uint32_t mapping;
    uint32_t requested_style;
    uint32_t face_style;
    uint32_t face_loaded;
    gd_string_view family;
    gd_string_view face_file;
} gd_font_resolution;

/*
 * The raster module's private attachment (raster_service_install): not part
 * of the 1.0 ABI. Its callbacks take GUI.Drawing's C++ objects, so only a
 * raster module built with this library installs it; the record is declared
 * in the library's own sources (src/abi/drawing_raster_service.h). A program
 * installs the raster through the table's raster_initialize instead.
 */
typedef struct gd_raster_service_v0 gd_raster_service_v0;

/* The pixels a GUI.Forms paint callback is lent (gui_forms/c/gf_abi.h), which
   recorder_paint_target draws into. */
struct gf_paint_target;

typedef struct gd_api_v0 {
    uint32_t struct_size;
    uint32_t abi_version;

    gd_result (*last_error)(gd_error_view* error);
    gd_result (*retain)(gd_handle handle);
    gd_result (*release)(gd_handle handle);
    gd_result (*dispose)(gd_handle handle);
    gd_result (*object_state)(gd_handle handle, uint32_t* state);
    gd_result (*object_kind)(gd_handle handle, uint32_t* kind);

    gd_result (*solid_brush_create)(gd_color color, gd_handle* brush);
    gd_result (*pen_create)(gd_color color, double width, gd_handle* pen);
    gd_result (*pen_set_width)(gd_handle pen, double width);
    gd_result (*pen_set_dash_style)(gd_handle pen, uint32_t style);
    gd_result (*pen_set_dash_pattern)(gd_handle pen,
                                      const double* entries,
                                      uint64_t count);
    gd_result (*font_create)(gd_string_view family, double size,
                             uint32_t style, uint32_t unit,
                             uint32_t charset, gd_handle* font);
    gd_result (*string_format_create)(uint32_t flags, gd_handle* format);
    gd_result (*string_format_set)(gd_handle format,
                                   uint32_t alignment,
                                   uint32_t line_alignment,
                                   uint32_t trimming,
                                   uint32_t flags);

    gd_result (*recorder_create)(gd_handle* recorder);
    gd_result (*recorder_save)(gd_handle recorder, uint64_t* token);
    gd_result (*recorder_restore)(gd_handle recorder, uint64_t token);
    gd_result (*recorder_translate)(gd_handle recorder, double x, double y);
    gd_result (*recorder_set_transform)(gd_handle recorder, gd_matrix transform);
    /* set_clip replaces the clip in force; it does not intersect with it.
       The clip is the axis-aligned bounds of the rectangle under the
       transform in force when it is set, in target coordinates, so a later
       transform does not move it. reset_clip removes it; recorder_save
       records it and recorder_restore brings back the one saved. Every
       command is drawn within the clip in force when it was recorded. */
    gd_result (*recorder_set_clip)(gd_handle recorder, gd_rect clip);
    gd_result (*recorder_reset_clip)(gd_handle recorder);
    gd_result (*recorder_set_quality)(gd_handle recorder,
                                      uint32_t smoothing,
                                      uint32_t interpolation,
                                      uint32_t pixel_offset,
                                      uint32_t compositing,
                                      uint32_t compositing_quality);
    gd_result (*recorder_is_visible)(gd_handle recorder, gd_point point,
                                     uint32_t* visible);
    gd_result (*recorder_clear)(gd_handle recorder, gd_color color);
    gd_result (*recorder_fill_rectangle)(gd_handle recorder, gd_handle brush,
                                         gd_rect rect);
    gd_result (*recorder_draw_rectangle)(gd_handle recorder, gd_handle pen,
                                         gd_rect rect);
    gd_result (*recorder_draw_line)(gd_handle recorder, gd_handle pen,
                                    gd_point from, gd_point to);
    gd_result (*recorder_draw_string)(gd_handle recorder, gd_string_view text,
                                      gd_handle font, gd_handle brush,
                                      gd_point origin, gd_handle format);
    /* After close every recording call fails (GD_ERROR_INVALID_ARGUMENT) and
       records nothing, until recorder_reset (ABI 0.5). recorder_clear
       records a clear of the target; it does not empty the recording. */
    gd_result (*recorder_close)(gd_handle recorder);
    gd_result (*recorder_command_count)(gd_handle recorder, uint64_t* count);
    gd_result (*recorder_trace)(gd_handle recorder,
                                char* buffer,
                                uint64_t capacity,
                                uint64_t* required_size);

    /* Appended M11e vocabulary; preserves the original ABI 0.1 prefix. */
    gd_result (*graphics_path_create)(uint32_t fill_mode, gd_handle* path);
    gd_result (*graphics_path_reset)(gd_handle path);
    gd_result (*graphics_path_start_figure)(gd_handle path);
    gd_result (*graphics_path_close_figure)(gd_handle path);
    gd_result (*graphics_path_add_line)(gd_handle path, gd_point from, gd_point to);
    gd_result (*graphics_path_add_rectangle)(gd_handle path, gd_rect rectangle);
    gd_result (*graphics_path_add_ellipse)(gd_handle path, gd_rect bounds);
    gd_result (*graphics_path_bounds)(gd_handle path, gd_rect* bounds);
    gd_result (*image_reference_create)(uint64_t stable_id,
                                        uint32_t width,
                                        uint32_t height,
                                        uint32_t pixel_format,
                                        uint64_t generation,
                                        gd_handle* image);
    gd_result (*image_attributes_create)(gd_handle* attributes);
    gd_result (*image_attributes_set_color_matrix)(gd_handle attributes,
                                                   const double* entries,
                                                   uint64_t count);
    gd_result (*image_attributes_reset)(gd_handle attributes);
    gd_result (*recorder_draw_ellipse)(gd_handle recorder, gd_handle pen,
                                       gd_rect bounds);
    gd_result (*recorder_fill_ellipse)(gd_handle recorder, gd_handle brush,
                                       gd_rect bounds);
    gd_result (*recorder_fill_polygon)(gd_handle recorder, gd_handle brush,
                                       const gd_point* points, uint64_t count,
                                       uint32_t fill_mode);
    gd_result (*recorder_draw_path)(gd_handle recorder, gd_handle pen,
                                    gd_handle path);
    gd_result (*recorder_fill_path)(gd_handle recorder, gd_handle brush,
                                    gd_handle path);
    gd_result (*recorder_draw_image)(gd_handle recorder, gd_handle image,
                                     gd_rect destination, gd_rect source,
                                     gd_handle attributes);

    /* Owned CPU bitmap and pixel-lease operations appended by M11f. */
    gd_result (*bitmap_create)(uint32_t width, uint32_t height,
                               uint32_t pixel_format, gd_handle* bitmap);
    gd_result (*bitmap_dimensions)(gd_handle bitmap, uint32_t* width,
                                   uint32_t* height, uint32_t* pixel_format,
                                   uint64_t* generation);
    gd_result (*bitmap_get_pixel)(gd_handle bitmap, uint32_t x, uint32_t y,
                                  gd_color* color);
    gd_result (*bitmap_set_pixel)(gd_handle bitmap, uint32_t x, uint32_t y,
                                  gd_color color);
    gd_result (*bitmap_lock)(gd_handle bitmap, uint32_t mode,
                             gd_bitmap_lock_view* view);
    gd_result (*bitmap_unlock)(gd_handle bitmap, uint64_t token);
    gd_result (*bitmap_clone)(gd_handle bitmap, gd_rect_i source,
                              gd_handle* clone);
    gd_result (*bitmap_make_transparent)(gd_handle bitmap, gd_color key);
    gd_result (*bitmap_thumbnail)(gd_handle bitmap, uint32_t width,
                                  uint32_t height, gd_handle* thumbnail);
    gd_result (*bitmap_adjusted)(gd_handle bitmap, gd_handle attributes,
                                 gd_handle* adjusted);
    gd_result (*recorder_draw_bitmap)(gd_handle recorder, gd_handle bitmap,
                                      gd_rect destination, gd_rect source,
                                      gd_handle attributes);

    /* M11f retained path, image-adjustment, and brush vocabulary. */
    gd_result (*graphics_path_add_arc)(gd_handle path, gd_rect bounds,
                                       double start_angle, double sweep_angle);
    gd_result (*graphics_path_add_path)(gd_handle path, gd_handle appended,
                                        uint32_t connect);
    gd_result (*graphics_path_transform)(gd_handle path, gd_matrix transform);
    gd_result (*graphics_path_is_visible)(gd_handle path, gd_point point,
                                          uint32_t* visible);
    gd_result (*graphics_path_points)(gd_handle path, gd_point* points,
                                      uint64_t capacity, uint64_t* required_count);
    gd_result (*graphics_path_clone)(gd_handle path, gd_handle* clone);
    gd_result (*image_attributes_set_remap_table)(gd_handle attributes,
                                                  const gd_color_remap* entries,
                                                  uint64_t count);
    gd_result (*image_attributes_reset_remap_table)(gd_handle attributes);
    gd_result (*hatch_brush_create)(uint32_t style, gd_color foreground,
                                    gd_color background, gd_handle* brush);
    gd_result (*linear_gradient_brush_create)(gd_rect bounds, gd_color first,
                                              gd_color second, double angle,
                                              uint32_t wrap_mode,
                                              gd_handle* brush);
    gd_result (*linear_gradient_set_blend)(gd_handle brush,
                                           const double* factors,
                                           const double* positions,
                                           uint64_t count);
    gd_result (*linear_gradient_set_interpolation)(gd_handle brush,
                                                   const gd_color* colors,
                                                   const double* positions,
                                                   uint64_t count);
    gd_result (*linear_gradient_set_wrap_mode)(gd_handle brush,
                                               uint32_t wrap_mode);
    gd_result (*path_gradient_brush_create)(const gd_point* points,
                                            uint64_t count,
                                            uint32_t wrap_mode,
                                            gd_handle* brush);
    gd_result (*path_gradient_set_center_color)(gd_handle brush, gd_color color);
    gd_result (*path_gradient_set_center_point)(gd_handle brush, gd_point point);
    gd_result (*path_gradient_set_surround_colors)(gd_handle brush,
                                                   const gd_color* colors,
                                                   uint64_t count);
    gd_result (*path_gradient_set_interpolation)(gd_handle brush,
                                                 const gd_color* colors,
                                                 const double* positions,
                                                 uint64_t count);
    gd_result (*region_create_rectangle)(gd_rect rectangle, gd_handle* region);
    gd_result (*region_create_path)(gd_handle path, gd_handle* region);
    gd_result (*region_union_rectangle)(gd_handle region, gd_rect rectangle);
    gd_result (*region_union_path)(gd_handle region, gd_handle path);
    gd_result (*region_exclude_rectangle)(gd_handle region, gd_rect rectangle);
    gd_result (*region_is_visible)(gd_handle region, gd_point point,
                                   uint32_t* visible);
    gd_result (*region_bounds)(gd_handle region, gd_rect* bounds);
    /* Private to the raster module (gd_raster_service_v0 above); not 1.0. */
    gd_result (*raster_service_install)(const gd_raster_service_v0* service);
    gd_result (*recorder_execute)(gd_handle recorder, gd_handle bitmap,
                                  uint64_t* commands_executed);
    gd_result (*bitmap_encode_png)(gd_handle bitmap, void* buffer,
                                   uint64_t capacity, uint64_t* required_size);
    gd_result (*bitmap_decode_png)(const void* data, uint64_t size,
                                   gd_handle* bitmap);
    gd_result (*image_attributes_clone)(gd_handle attributes,
                                        gd_handle* clone);
    /* D8 platform extension. Returned HBITMAP ownership transfers to caller. */
    gd_result (*bitmap_export_hbitmap)(gd_handle bitmap, gd_color background,
                                       uintptr_t* hbitmap);
    /* D8 platform extension. The source HBITMAP remains caller-owned. */
    gd_result (*bitmap_import_hbitmap)(uintptr_t hbitmap, gd_handle* bitmap);
    /* D8 captures an opaque Windows surface into owned GUI.Drawing storage. */
    gd_result (*native_surface_capture)(uintptr_t surface, uint32_t kind,
                                        gd_handle* bitmap, gd_rect* bounds);
    /* D8 presents owned GUI.Drawing storage back to the borrowed surface. */
    gd_result (*native_surface_present)(uintptr_t surface, uint32_t kind,
                                        gd_handle bitmap);
    /* D8 tokenized bitmap-backed HDC lease. */
    gd_result (*bitmap_acquire_hdc)(gd_handle bitmap, uintptr_t* hdc,
                                    uint64_t* lease_token);
    gd_result (*bitmap_release_hdc)(gd_handle bitmap, uint64_t lease_token);
    /* M11g renderer-owned glyph metrics; a zero format handle means defaults. */
    gd_result (*measure_string)(gd_string_view text, gd_handle font,
                                gd_handle format, double layout_width,
                                gd_size* measured);
    /* M11g incremental retained commit; returns commands run from first_command. */
    gd_result (*recorder_execute_from)(gd_handle recorder, gd_handle bitmap,
                                       uint64_t first_command,
                                       uint64_t* commands_executed);
    /* M12-P26 transactional rectangular edit and multi-consumer damage query. */
    gd_result (*bitmap_edit_begin)(gd_handle bitmap, gd_rect_i bounds,
                                   gd_bitmap_edit_view* view);
    gd_result (*bitmap_edit_commit)(gd_handle bitmap, uint64_t token,
                                    uint64_t* generation);
    gd_result (*bitmap_edit_cancel)(gd_handle bitmap, uint64_t token);
    gd_result (*bitmap_changes_since)(gd_handle bitmap, uint64_t generation,
                                      gd_rect_i* rectangles, uint64_t capacity,
                                      uint64_t* required_count,
                                      gd_bitmap_damage_summary* summary);
    /* M12-P27 refreshes an existing bitmap from a borrowed native surface. */
    gd_result (*native_surface_refresh)(uintptr_t surface, uint32_t kind,
                                        gd_handle bitmap, gd_rect* bounds);
    /* M12-P27 submits a coherent frame directly to a retained host endpoint. */
    gd_result (*native_surface_publish_retained)(uintptr_t surface, uint32_t kind,
                                                 gd_handle bitmap);
    /*
     * Stage F: how the font's family resolves. With a raster service it is
     * the face drawn on the calling thread, after any host lookup; without
     * one it is the substitution table's answer and nothing is loaded.
     */
    gd_result (*font_resolve)(gd_handle font, gd_font_resolution* resolution);
    /*
     * ABI 0.3, the drawing primitives (before the drawing ABI seals): pen joins
     * and caps, batched marks (one command per call, the array copied
     * once, at most 1,000,000 elements), conic and checker brushes, the
     * world-aligned grid, the proportional fill, text over a fill with
     * the contrast rule, and rounded path rectangles.
     */
    gd_result (*pen_set_line_join)(gd_handle pen, uint32_t join, double miter_limit);
    gd_result (*pen_set_line_cap)(gd_handle pen, uint32_t cap);
    /* An open polyline through count (2 or more) points. */
    gd_result (*recorder_draw_lines)(gd_handle recorder, gd_handle pen,
                                     const gd_point* points, uint64_t count);
    gd_result (*recorder_fill_rectangles)(gd_handle recorder, gd_handle brush,
                                          const gd_rect* rects, uint64_t count);
    /* Independent segments: count is even, each pair one segment. */
    gd_result (*recorder_draw_line_segments)(gd_handle recorder, gd_handle pen,
                                             const gd_point* points, uint64_t count);
    gd_result (*recorder_draw_markers)(gd_handle recorder, const gd_point* centers,
                                       uint64_t count, const gd_marker_style* style);
    /* A sweep of one full turn clockwise (y down) from start_angle degrees. */
    gd_result (*conic_gradient_brush_create)(gd_point center, double start_angle,
                                             gd_color first, gd_color second,
                                             gd_handle* brush);
    gd_result (*conic_gradient_set_interpolation)(gd_handle brush,
                                                  const gd_color* colors,
                                                  const double* positions,
                                                  uint64_t count);
    /* Square cells cell_size wide, a first cell's corner at the origin. */
    gd_result (*checker_brush_create)(double cell_size, gd_color first,
                                      gd_color second, gd_handle* brush);
    gd_result (*recorder_fill_grid_pattern)(gd_handle recorder, gd_rect rect,
                                            const gd_grid_pattern* grid);
    /* The part of track proportion (clamped to [0, 1]) covers from
       direction's edge, clipped to the track's rounded shape. */
    gd_result (*recorder_fill_proportion)(gd_handle recorder, gd_handle brush,
                                          gd_rect track, gd_corner_radii radii,
                                          double proportion, uint32_t direction);
    /* The string in brush's colour, and in on_fill_color where it lies over
       fill; up to five draw_string commands. The transform must be a scale
       and translation. */
    gd_result (*recorder_draw_string_over_fill)(gd_handle recorder, gd_string_view text,
                                                gd_handle font, gd_handle brush,
                                                gd_color on_fill_color, gd_point origin,
                                                gd_handle format, gd_rect fill);
    /* The WCAG 2 contrast ratio, 1 to 21; alpha is ignored. */
    gd_result (*contrast_ratio)(gd_color first, gd_color second, double* ratio);
    /* Of light and dark, the one contrasting more with background (dark on
       a tie). */
    gd_result (*contrasting_text_color)(gd_color background, gd_color light,
                                        gd_color dark, gd_color* chosen);
    gd_result (*graphics_path_add_rounded_rectangle)(gd_handle path, gd_rect rectangle,
                                                     gd_corner_radii radii);
    /*
     * ABI 0.4, the gapped and per-element marks (the plot demonstrator's
     * gaps). Each records the 0.3 commands (fill_polygon, draw_lines,
     * fill_rectangles), so a trace names those. A break is a point with a
     * NaN coordinate; an infinite coordinate is refused. Each validates its
     * whole input before it records; a refusal records nothing.
     *
     * The area between two polylines sampled at the same positions: upper
     * and lower each hold count points, 2 through 500,000. A break at index
     * i in either array breaks the band; each run of 2 or more unbroken
     * indices records one fill_polygon (winding), and a run of one records
     * nothing.
     */
    gd_result (*recorder_fill_band)(gd_handle recorder, gd_handle brush,
                                    const gd_point* upper, const gd_point* lower,
                                    uint64_t count);
    /* An open polyline through count (2 through 1,000,000) points that breaks
       at each break: each run of 2 or more unbroken points records one
       draw_lines, and a run of one records nothing. Unlike
       recorder_draw_lines, which refuses a NaN point. */
    gd_result (*recorder_draw_polyline_runs)(gd_handle recorder, gd_handle pen,
                                             const gd_point* points, uint64_t count);
    /* Each of count (1 through 1,000,000) finite rectangles filled in its own
       colour, colors[i] for rects[i]; each run of equal colours records one
       fill_rectangles with a solid brush. */
    gd_result (*recorder_fill_rectangles_each)(gd_handle recorder, const gd_rect* rects,
                                               const gd_color* colors, uint64_t count);
    /*
     * ABI 0.5, text in a box, batched strings, reuse and the fitted measure
     * (the flame graph demonstrator's gaps D2 to D4). Each validates its
     * whole input before it records; a refusal records nothing.
     *
     * The string laid out in layout (finite, non-negative size): lines break
     * at newlines only; each line is placed across the box by the format's
     * alignment and the block down it by its line alignment; a line wider
     * than the box is trimmed by the format's trimming (the ellipsis forms
     * add U+2026; ellipsis path is taken as ellipsis character; none lets it
     * run on); the text is clipped to the box unless the format has the
     * no-clip flag (0x4000). One draw_string command.
     */
    gd_result (*recorder_draw_string_in_rect)(gd_handle recorder, gd_string_view text,
                                              gd_handle font, gd_handle brush,
                                              gd_rect layout, gd_handle format);
    /* count (1 through 65,536) strings in one font and format, items[i] at its
       origin in its colour: one draw_string command each, in order. */
    gd_result (*recorder_draw_strings)(gd_handle recorder, gd_handle font,
                                       gd_handle format, const gd_text_item* items,
                                       uint64_t count);
    /* Empties the recording for reuse: the commands, the saved states (their
       tokens become invalid) and the state in force are cleared and a closed
       recorder reopens; the command storage is kept. */
    gd_result (*recorder_reset)(gd_handle recorder);
    /* The longest prefix of text, in whole code points, whose one-line
       measured width (measure_string with no layout width) is at most
       max_width (finite, non-negative): its byte count and that width. Text
       that fits whole reports its size; none fitting reports 0 and 0. A zero
       format handle means defaults. Needs the raster service. */
    gd_result (*measure_string_fitted)(gd_string_view text, gd_handle font,
                                       gd_handle format, double max_width,
                                       uint64_t* bytes_fitted, double* fitted_width);
    /*
     * ABI 0.6, curved paths. Each curve appends to the current figure as
     * graphics_path_add_line does. recorder_fill_path fills by the path's fill mode
     * (0 alternate, even-odd; 1 winding, non-zero) and recorder_draw_path
     * strokes with the pen's width, joins, caps and dashes. Every point is
     * finite; a refused call leaves the path as it was.
     */
    /* A quadratic Bezier curve from `from` through the control to `to`. */
    gd_result (*graphics_path_add_quadratic)(gd_handle path, gd_point from, gd_point control,
                                             gd_point to);
    /* A cubic Bezier curve from `from` by two controls to `to`. */
    gd_result (*graphics_path_add_bezier)(gd_handle path, gd_point from, gd_point first_control,
                                          gd_point second_control, gd_point to);
    /* Joined cubic curves: count is 4 + 3n (at most 1,000,000 points); each
       curve starts at the previous one's end. */
    gd_result (*graphics_path_add_beziers)(gd_handle path, const gd_point* points, uint64_t count);
    /* A closed figure through count (3 or more) points. */
    gd_result (*graphics_path_add_polygon)(gd_handle path, const gd_point* points, uint64_t count);
    /* A closed pie wedge: the centre of bounds, the arc as add_arc says, and
       back to the centre. */
    gd_result (*graphics_path_add_pie)(gd_handle path, gd_rect bounds, double start_angle,
                                       double sweep_angle);
    /* 0 alternate (even-odd), 1 winding (non-zero). */
    gd_result (*graphics_path_set_fill_mode)(gd_handle path, uint32_t fill_mode);
    gd_result (*graphics_path_fill_mode)(gd_handle path, uint32_t* fill_mode);
    /*
     * ABI 0.7, text at any angle.
     *
     * The string as recorder_draw_string draws it at origin, turned about
     * origin by degrees (finite; positive turns anticlockwise as seen, so 90
     * reads up the surface), with the same font, shaping, format and
     * colour, under the transform and clip in force. One draw_string command
     * whose recorded transform is the turn followed by the transform in
     * force; the state in force is unchanged. 0 degrees records exactly what
     * recorder_draw_string records. A refusal records nothing.
     */
    gd_result (*recorder_draw_string_rotated)(gd_handle recorder, gd_string_view text,
                                              gd_handle font, gd_handle brush,
                                              gd_point origin, double degrees,
                                              gd_handle format);
    /*
     * ABI 1.0, the bridge to GUI.Forms and the gradient brush aliases.
     *
     * The recording drawn into the pixels a GUI.Forms paint callback is lent
     * (gf_custom_control_callbacks.paint), as recorder_execute draws into a
     * bitmap: its units are the control's logical units, drawn at
     * target->scale device pixels each, and only inside target->damage
     * (rounded out to whole device pixels); pixels outside it are left as
     * they are. The recording is not changed, and may be drawn again. The
     * target is read for the call and its pixels written in place; a
     * command the raster module refuses part-way leaves the commands before
     * it drawn, as a native surface would. commands_executed receives the
     * number drawn. The target needs struct_size at least gf_paint_target's
     * 1.0 size, pixels, a width and height of 1 to 32,768 (at most 10^8
     * pixels), row_bytes a multiple of 4 holding a row, a finite positive
     * scale and a finite damage rectangle. Needs the raster module
     * (GD_ERROR_NOT_AVAILABLE before raster_initialize). Call it from the paint
     * callback, on its thread, while the pixels are lent.
     */
    gd_result (*recorder_paint_target)(gd_handle recorder,
                                       const struct gf_paint_target* target,
                                       uint64_t* commands_executed);
    /* The same functions as linear_gradient_set_blend,
       linear_gradient_set_interpolation, linear_gradient_set_wrap_mode,
       path_gradient_set_center_color, path_gradient_set_center_point,
       path_gradient_set_surround_colors, path_gradient_set_interpolation and
       conic_gradient_set_interpolation, under the <kind>_gradient_brush_set_*
       names. */
    gd_result (*linear_gradient_brush_set_blend)(gd_handle brush,
                                                 const double* factors,
                                                 const double* positions,
                                                 uint64_t count);
    gd_result (*linear_gradient_brush_set_interpolation)(gd_handle brush,
                                                         const gd_color* colors,
                                                         const double* positions,
                                                         uint64_t count);
    gd_result (*linear_gradient_brush_set_wrap_mode)(gd_handle brush,
                                                     uint32_t wrap_mode);
    gd_result (*path_gradient_brush_set_center_color)(gd_handle brush, gd_color color);
    gd_result (*path_gradient_brush_set_center_point)(gd_handle brush, gd_point point);
    gd_result (*path_gradient_brush_set_surround_colors)(gd_handle brush,
                                                         const gd_color* colors,
                                                         uint64_t count);
    gd_result (*path_gradient_brush_set_interpolation)(gd_handle brush,
                                                       const gd_color* colors,
                                                       const double* positions,
                                                       uint64_t count);
    gd_result (*conic_gradient_brush_set_interpolation)(gd_handle brush,
                                                        const gd_color* colors,
                                                        const double* positions,
                                                        uint64_t count);
    /*
     * ABI 1.1, provisional (the number is a placeholder): path flattening,
     * GraphicsPath::flatten in C++ and GraphicsPath.Flatten in C#.
     *
     * The path as polylines: every curve, arc and ellipse replaced by chords
     * none of whose points of the curve lies farther than tolerance from
     * them. tolerance is a distance in the path's own coordinate units (its
     * points as stored, after any graphics_path_transform), positive and
     * finite; a caller drawing at scale s passes its device tolerance / s.
     * Figures follow the raster's reading of the path: an element not
     * starting where the figure stands starts a new figure (an arc joins it
     * with a straight edge instead); start_figure ends a figure open and
     * close_figure closed; a rectangle and an ellipse are closed figures of
     * their own. A closed figure repeats its first point last. Figure i's
     * points run from figure_ends[i - 1] (0 for the first) to
     * figure_ends[i], exclusive, and figure_closed[i] is 1 when it is
     * closed, else 0.
     *
     * point_count and figure_count receive the counts the flattening needs.
     * When point_capacity or figure_capacity is smaller, nothing is written
     * to the arrays and the entry fails GD_ERROR_BUFFER_TOO_SMALL (a null
     * array with capacity 0 asks for the counts). A flattening of more than
     * 16,777,216 points fails GD_ERROR_LIMIT_EXCEEDED with both counts 0; a
     * tolerance that is not positive and finite fails
     * GD_ERROR_INVALID_ARGUMENT.
     */
    gd_result (*graphics_path_flatten)(gd_handle path, double tolerance,
                                       gd_point* points, uint64_t point_capacity,
                                       uint64_t* figure_ends, uint32_t* figure_closed,
                                       uint64_t figure_capacity,
                                       uint64_t* point_count, uint64_t* figure_count);
    /*
     * ABI 1.1, provisional (the number is a placeholder): the raster.
     *
     * Installs the CPU raster this library carries as the table's raster
     * service (recorder_execute, the PNG entries, measured text, font
     * resolution and recorder_paint_target need it) and registers its
     * packaged faces on the calling thread. Call it once per process before
     * those entries; a later call changes nothing and returns GD_OK. A
     * library built without the renderer fails GD_ERROR_NOT_AVAILABLE. It
     * replaces gdr_initialize, which is no longer exported: the library
     * exports gd_get_api_v0 and its gf_ entry points alone.
     */
    gd_result (*raster_initialize)(void);
} gd_api_v0;

/*
 * The caller sets table->struct_size. The implementation copies only the
 * common prefix and reports its complete table size on success (see
 * "Versions and the table" above).
 */
GD_C_API_EXPORT gd_result gd_get_api_v0(uint32_t requested_version,
                                        gd_api_v0* table);

#ifdef __cplusplus
}
#endif

#endif
