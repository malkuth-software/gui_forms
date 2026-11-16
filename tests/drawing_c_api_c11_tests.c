#include "gui_forms/drawing_c_api.h"
#include "gui_forms/c/gf_abi.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "drawing_c_api_c11_tests:%d: %s\n", __LINE__, #expression); \
        return 1; \
    } \
} while (0)

static gd_string_view view_of(const char* text) {
    gd_string_view result;
    result.data = text;
    result.size = (uint64_t)strlen(text);
    return result;
}

/* The drawing-primitives append: every entry records, validates before it
   mutates, and traces. */
/* ABI 0.6, curved paths: each verb is accepted and kept, refusals leave the
   path as it was, and the fill mode round-trips. */
static int check_curved_paths(const gd_api_v0* api) {
    gd_handle path = {0};
    gd_rect bounds = {0};
    uint32_t fill_mode = 9U;
    uint64_t required = 0;
    gd_point kept[64];
    const gd_point beziers[7] = {{0, 0}, {4, -8}, {8, 8}, {12, 0}, {16, -8}, {20, 8}, {24, 0}};
    const gd_point triangle[3] = {{0, 0}, {10, 0}, {5, 9}};

    CHECK((*api).graphics_path_create(GD_FILL_ALTERNATE, &path) == GD_OK);
    CHECK((*api).graphics_path_fill_mode(path, &fill_mode) == GD_OK && fill_mode == 0U);
    CHECK((*api).graphics_path_add_quadratic(path, (gd_point){0, 0}, (gd_point){10, 20}, (gd_point){20, 0}) == GD_OK);
    CHECK((*api).graphics_path_add_bezier(path, (gd_point){20, 0}, (gd_point){25, -10}, (gd_point){35, 10},
                                          (gd_point){40, 0}) == GD_OK);
    CHECK((*api).graphics_path_points(path, NULL, 0, &required) == GD_ERROR_BUFFER_TOO_SMALL);
    /* The quadratic's three points, then the Bezier's controls and end: its start is
       the quadratic's end, which the path keeps once (as for lines). */
    CHECK(required == 6U);
    CHECK((*api).graphics_path_points(path, kept, 64, &required) == GD_OK);
    CHECK(kept[0].x == 0.0 && kept[0].y == 0.0);
    CHECK((*api).graphics_path_bounds(path, &bounds) == GD_OK);
    CHECK(bounds.x <= 0.0 && bounds.x + bounds.width >= 40.0);

    /* Refusals keep the path's points as they were. */
    CHECK((*api).graphics_path_add_beziers(path, beziers, 5) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).graphics_path_add_beziers(path, NULL, 7) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).graphics_path_add_polygon(path, triangle, 2) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).graphics_path_add_quadratic(path, (gd_point){0, 0}, (gd_point){INFINITY, 0}, (gd_point){1, 1}) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).graphics_path_set_fill_mode(path, 2U) == GD_ERROR_INVALID_ARGUMENT);
    {
        uint64_t again = 0;
        CHECK((*api).graphics_path_points(path, NULL, 0, &again) == GD_ERROR_BUFFER_TOO_SMALL);
        CHECK(again == required);
    }

    CHECK((*api).graphics_path_add_beziers(path, beziers, 7) == GD_OK);
    CHECK((*api).graphics_path_add_polygon(path, triangle, 3) == GD_OK);
    CHECK((*api).graphics_path_add_pie(path, (gd_rect){50, 0, 20, 20}, 0, 90) == GD_OK);
    CHECK((*api).graphics_path_set_fill_mode(path, GD_FILL_WINDING) == GD_OK);
    CHECK((*api).graphics_path_fill_mode(path, &fill_mode) == GD_OK && fill_mode == 1U);
    CHECK((*api).graphics_path_fill_mode(path, NULL) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).graphics_path_bounds(path, &bounds) == GD_OK);
    CHECK(bounds.x + bounds.width >= 70.0);
    CHECK((*api).release(path) == GD_OK);
    CHECK((*api).graphics_path_add_quadratic(path, (gd_point){0, 0}, (gd_point){1, 1}, (gd_point){2, 0}) ==
          GD_ERROR_STALE_HANDLE);
    return 0;
}

/* ABI 1.1, provisional: the texture and radial gradient brushes. Each
   refuses before it creates or changes anything, and a brush of the wrong
   kind is refused by its kind. */
static int check_texture_and_radial_brushes(const gd_api_v0* api) {
    gd_handle bitmap = {0};
    gd_handle texture = {0};
    gd_handle radial = {0};
    gd_handle refused = {0};
    gd_handle recorder = {0};
    gd_bitmap_lock_view lock;
    uint32_t kind = 0;
    uint64_t count = 0;
    const gd_color stops[3] = {{UINT32_C(0xffff0000), 0}, {UINT32_C(0xffffffff), 0}, {UINT32_C(0xff0000ff), 0}};
    const double positions[3] = {0, 0.5, 1};
    const gd_matrix doubled = {2, 0, 0, 2, 1, 1};
    const gd_matrix broken = {NAN, 0, 0, 1, 0, 0};

    memset(&lock, 0, sizeof(lock));
    CHECK((*api).bitmap_create(2, 2, GD_PIXEL_BGRA32_PREMULTIPLIED, &bitmap) == GD_OK);
    CHECK((*api).texture_brush_create(bitmap, GD_WRAP_TILE_FLIP_XY, &texture) == GD_OK);
    CHECK((*api).object_kind(texture, &kind) == GD_OK && kind == GD_OBJECT_TEXTURE_BRUSH);
    CHECK((*api).texture_brush_create(bitmap, 5U, &refused) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).texture_brush_create(bitmap, GD_WRAP_TILE, NULL) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).bitmap_lock(bitmap, GD_BITMAP_LOCK_READ, &lock) == GD_OK);
    CHECK((*api).texture_brush_create(bitmap, GD_WRAP_TILE, &refused) == GD_ERROR_INVALID_STATE);
    CHECK((*api).bitmap_unlock(bitmap, lock.token) == GD_OK);
    CHECK((*api).texture_brush_set_wrap_mode(texture, GD_WRAP_CLAMP) == GD_OK);
    CHECK((*api).texture_brush_set_wrap_mode(texture, 9U) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).texture_brush_set_transform(texture, doubled) == GD_OK);
    CHECK((*api).texture_brush_set_transform(texture, broken) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).texture_brush_set_wrap_mode(bitmap, GD_WRAP_TILE) == GD_ERROR_WRONG_HANDLE_KIND);

    CHECK((*api).radial_gradient_brush_create((gd_point){8, 8}, 8.0, stops[0], stops[2], GD_WRAP_TILE, &radial) ==
          GD_OK);
    CHECK((*api).object_kind(radial, &kind) == GD_OK && kind == GD_OBJECT_RADIAL_GRADIENT_BRUSH);
    CHECK((*api).radial_gradient_brush_create((gd_point){8, 8}, 0.0, stops[0], stops[2], GD_WRAP_TILE, &refused) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).radial_gradient_brush_create((gd_point){NAN, 8}, 8.0, stops[0], stops[2], GD_WRAP_TILE,
                                              &refused) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).radial_gradient_brush_create((gd_point){8, 8}, 8.0, stops[0], stops[2], 5U, &refused) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).radial_gradient_brush_set_interpolation(radial, stops, positions, 3) == GD_OK);
    CHECK((*api).radial_gradient_brush_set_interpolation(radial, stops, positions, 1) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).radial_gradient_brush_set_interpolation(texture, stops, positions, 3) ==
          GD_ERROR_WRONG_HANDLE_KIND);
    CHECK((*api).radial_gradient_brush_set_focus(radial, (gd_point){4, 8}) == GD_OK);
    CHECK((*api).radial_gradient_brush_set_focus(radial, (gd_point){16, 8}) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).radial_gradient_brush_set_focus(texture, (gd_point){8, 8}) == GD_ERROR_WRONG_HANDLE_KIND);
    /* An ellipse 8 across and 4 down keeps the focus (4, 8) inside; one
       2 across would put it outside and is refused. */
    CHECK((*api).radial_gradient_brush_set_radii(radial, 8.0, 4.0) == GD_OK);
    CHECK((*api).radial_gradient_brush_set_radii(radial, 2.0, 4.0) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).radial_gradient_brush_set_radii(radial, 8.0, 0.0) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).radial_gradient_brush_set_radii(texture, 8.0, 4.0) == GD_ERROR_WRONG_HANDLE_KIND);

    CHECK((*api).recorder_create(&recorder) == GD_OK);
    CHECK((*api).recorder_fill_rectangle(recorder, texture, (gd_rect){0, 0, 16, 16}) == GD_OK);
    CHECK((*api).recorder_fill_rectangle(recorder, radial, (gd_rect){0, 0, 16, 16}) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 2U);
    CHECK((*api).release(recorder) == GD_OK);
    CHECK((*api).release(radial) == GD_OK);
    CHECK((*api).release(texture) == GD_OK);
    CHECK((*api).release(bitmap) == GD_OK);
    return 0;
}

/* ABI 1.1, provisional: path flattening into the caller's three arrays,
   counted first; short arrays and bad tolerances leave the arrays as they
   were. The quadratic needs 5 chords at 0.25 (Wang's formula), six points;
   the rectangle is a closed figure of five. */
static int check_path_flattening(const gd_api_v0* api) {
    gd_handle path = {0};
    gd_point points[16];
    uint64_t ends[4] = {0};
    uint32_t closed[4] = {9U, 9U, 9U, 9U};
    uint64_t point_count = 0;
    uint64_t figure_count = 0;

    CHECK((*api).graphics_path_create(GD_FILL_ALTERNATE, &path) == GD_OK);
    CHECK((*api).graphics_path_add_quadratic(path, (gd_point){0, 10}, (gd_point){5, 0}, (gd_point){10, 10}) == GD_OK);
    CHECK((*api).graphics_path_add_rectangle(path, (gd_rect){1, 2, 3, 4}) == GD_OK);
    CHECK((*api).graphics_path_flatten(path, 0.25, NULL, 0, NULL, NULL, 0, &point_count, &figure_count) ==
          GD_ERROR_BUFFER_TOO_SMALL);
    CHECK(point_count == 11U && figure_count == 2U);
    points[0].x = -7.0;
    CHECK((*api).graphics_path_flatten(path, 0.25, points, 10, ends, closed, 4, &point_count, &figure_count) ==
          GD_ERROR_BUFFER_TOO_SMALL);
    CHECK(points[0].x == -7.0 && closed[0] == 9U);
    CHECK((*api).graphics_path_flatten(path, 0.25, points, 16, ends, closed, 4, &point_count, &figure_count) ==
          GD_OK);
    CHECK(point_count == 11U && figure_count == 2U);
    CHECK(ends[0] == 6U && closed[0] == 0U);
    CHECK(ends[1] == 11U && closed[1] == 1U);
    CHECK(points[0].x == 0.0 && points[0].y == 10.0);
    CHECK(points[5].x == 10.0 && points[5].y == 10.0);
    CHECK(points[6].x == 1.0 && points[6].y == 2.0);
    CHECK(points[10].x == 1.0 && points[10].y == 2.0);
    CHECK((*api).graphics_path_flatten(path, 0.0, points, 16, ends, closed, 4, &point_count, &figure_count) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).graphics_path_flatten(path, NAN, points, 16, ends, closed, 4, &point_count, &figure_count) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).graphics_path_flatten(path, 0.25, points, 16, ends, closed, 4, NULL, &figure_count) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).release(path) == GD_OK);

    CHECK((*api).graphics_path_create(GD_FILL_ALTERNATE, &path) == GD_OK);
    CHECK((*api).graphics_path_add_ellipse(path, (gd_rect){0, 0, 1e9, 1e9}) == GD_OK);
    CHECK((*api).graphics_path_flatten(path, 1e-9, NULL, 0, NULL, NULL, 0, &point_count, &figure_count) ==
          GD_ERROR_LIMIT_EXCEEDED);
    CHECK(point_count == 0U && figure_count == 0U);
    CHECK((*api).release(path) == GD_OK);
    CHECK((*api).graphics_path_flatten(path, 0.25, NULL, 0, NULL, NULL, 0, &point_count, &figure_count) ==
          GD_ERROR_STALE_HANDLE);
    return 0;
}

/* The 1.0 seal's error model (decision 4): counts over their limits give
   LIMIT_EXCEEDED, state refusals INVALID_STATE, and an entry that needs the
   raster module NOT_AVAILABLE when it is not loaded (this program does not
   load it). Every refusal leaves its object as it was. */
static int check_seal_errors(const gd_api_v0* api) {
    gd_handle recorder = {0};
    gd_handle pen = {0};
    gd_handle brush = {0};
    gd_handle bitmap = {0};
    gd_handle path = {0};
    gd_handle font = {0};
    gd_bitmap_lock_view lock = {0};
    gd_point points[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    gd_rect rects[1] = {{0, 0, 4, 4}};
    double dashes[2] = {2.0, 1.0};
    uint64_t count = 0;
    uint64_t executed = 7;
    uint8_t pixels[4 * 4 * 4] = {0};
    gf_paint_target target;
    gd_size measured = {0};
    gd_error_view error = {0};
    /* Far over every limit; each count is refused before its array is read. */
    const uint64_t huge = UINT64_C(1) << 40;

    CHECK((*api).recorder_create(&recorder) == GD_OK);
    CHECK((*api).pen_create((gd_color){UINT32_C(0xff000000), 0}, 1.0, &pen) == GD_OK);
    CHECK((*api).solid_brush_create((gd_color){UINT32_C(0xffff0000), 0}, &brush) == GD_OK);
    CHECK((*api).graphics_path_create(GD_FILL_ALTERNATE, &path) == GD_OK);

    CHECK((*api).recorder_draw_lines(recorder, pen, points, huge) == GD_ERROR_LIMIT_EXCEEDED);
    CHECK((*api).last_error(&error) == GD_OK && error.code == (uint32_t)GD_ERROR_LIMIT_EXCEEDED);
    CHECK((*api).recorder_draw_line_segments(recorder, pen, points, huge) == GD_ERROR_LIMIT_EXCEEDED);
    CHECK((*api).recorder_fill_rectangles(recorder, brush, rects, huge) == GD_ERROR_LIMIT_EXCEEDED);
    CHECK((*api).recorder_fill_polygon(recorder, brush, points, huge, GD_FILL_WINDING) == GD_ERROR_LIMIT_EXCEEDED);
    CHECK((*api).graphics_path_add_polygon(path, points, huge) == GD_ERROR_LIMIT_EXCEEDED);
    CHECK((*api).graphics_path_add_beziers(path, points, huge) == GD_ERROR_LIMIT_EXCEEDED);
    CHECK((*api).pen_set_dash_pattern(pen, dashes, 257) == GD_ERROR_LIMIT_EXCEEDED);
    CHECK((*api).pen_set_dash_pattern(pen, dashes, 0) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_polygon(recorder, brush, NULL, 3, GD_FILL_WINDING) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 0U);

    /* A closed recorder refuses recording as a state fault, not an argument. */
    CHECK((*api).recorder_close(recorder) == GD_OK);
    CHECK((*api).recorder_fill_rectangle(recorder, brush, rects[0]) == GD_ERROR_INVALID_STATE);
    CHECK((*api).last_error(&error) == GD_OK && error.code == (uint32_t)GD_ERROR_INVALID_STATE);
    CHECK((*api).recorder_translate(recorder, 1.0, 1.0) == GD_ERROR_INVALID_STATE);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 0U);

    /* A bitmap under a lock refuses pixel writes and edits. */
    CHECK((*api).bitmap_create(4, 4, GD_PIXEL_BGRA32_PREMULTIPLIED, &bitmap) == GD_OK);
    CHECK((*api).bitmap_lock(bitmap, GD_BITMAP_LOCK_READ, &lock) == GD_OK);
    CHECK((*api).bitmap_set_pixel(bitmap, 0, 0, (gd_color){UINT32_C(0xffffffff), 0}) == GD_ERROR_INVALID_STATE);
    CHECK((*api).bitmap_unlock(bitmap, lock.token) == GD_OK);
    CHECK((*api).bitmap_set_pixel(bitmap, 0, 0, (gd_color){UINT32_C(0xffffffff), 0}) == GD_OK);

    /* Entries that need the raster module. */
    CHECK((*api).recorder_execute(recorder, bitmap, &executed) == GD_ERROR_NOT_AVAILABLE);
    CHECK((*api).font_create(view_of("Lucida Grande"), 12.0, GD_FONT_STYLE_BOLD, GD_FONT_UNIT_POINT,
                             GD_FONT_CHARSET_DEFAULT, &font) == GD_OK);
    CHECK((*api).measure_string(view_of("x"), font, (gd_handle){0, 0}, 0.0, &measured) == GD_ERROR_NOT_AVAILABLE);

    /* The bridge checks its target before it needs the raster module. */
    memset(&target, 0, sizeof(target));
    target.struct_size = (uint32_t)sizeof(target);
    target.width = 4U;
    target.height = 4U;
    target.row_bytes = 16U;
    target.pixels = pixels;
    target.scale = 1.0;
    target.damage = (gf_logical_rect){0, 0, 4, 4};
    CHECK((*api).recorder_paint_target(recorder, &target, NULL) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_paint_target(recorder, NULL, &executed) == GD_ERROR_INVALID_ARGUMENT);
    CHECK(executed == 0U);
    target.row_bytes = 12U;
    CHECK((*api).recorder_paint_target(recorder, &target, &executed) == GD_ERROR_INVALID_ARGUMENT);
    target.row_bytes = 16U;
    target.scale = 0.0;
    CHECK((*api).recorder_paint_target(recorder, &target, &executed) == GD_ERROR_INVALID_ARGUMENT);
    target.scale = 1.0;
    target.damage.width = -1.0;
    CHECK((*api).recorder_paint_target(recorder, &target, &executed) == GD_ERROR_INVALID_ARGUMENT);
    target.damage.width = 4.0;
    target.width = 40000U;
    target.row_bytes = 160000U;
    CHECK((*api).recorder_paint_target(recorder, &target, &executed) == GD_ERROR_LIMIT_EXCEEDED);
    target.width = 4U;
    target.row_bytes = 16U;
    target.struct_size = 8U;
    CHECK((*api).recorder_paint_target(recorder, &target, &executed) == GD_ERROR_INVALID_ARGUMENT);
    target.struct_size = (uint32_t)sizeof(target);
    CHECK((*api).recorder_paint_target(recorder, &target, &executed) == GD_ERROR_NOT_AVAILABLE);
    CHECK(pixels[0] == 0U);

    CHECK((*api).release(font) == GD_OK);
    CHECK((*api).release(bitmap) == GD_OK);
    CHECK((*api).release(path) == GD_OK);
    CHECK((*api).release(brush) == GD_OK);
    CHECK((*api).release(pen) == GD_OK);
    CHECK((*api).release(recorder) == GD_OK);
    return 0;
}

/* The seal's handle rules (decision 8): {0, 0} is null and stale; a released
   handle goes stale and stays stale when its slot is reused, while the new
   handle in that slot works; dispose keeps the handle valid, refusing use
   with DISPOSED, until its last release. */
static int check_handle_staleness(const gd_api_v0* api) {
    gd_handle first = {0};
    gd_handle second = {0};
    gd_handle retained = {0};
    const gd_handle null_handle = {0, 0};
    gd_handle beyond = {0};
    uint32_t state = 9U;
    uint32_t kind = 0U;

    CHECK((*api).pen_set_width(null_handle, 1.0) == GD_ERROR_STALE_HANDLE);
    CHECK((*api).release(null_handle) == GD_ERROR_STALE_HANDLE);
    CHECK((*api).object_kind(null_handle, &kind) == GD_ERROR_STALE_HANDLE);

    CHECK((*api).pen_create((gd_color){UINT32_C(0xff000000), 0}, 1.0, &first) == GD_OK);
    CHECK(first.slot != 0U && first.generation != 0U);
    beyond.slot = first.slot + 100000U;
    beyond.generation = first.generation;
    CHECK((*api).pen_set_width(beyond, 1.0) == GD_ERROR_STALE_HANDLE);
    CHECK((*api).release(first) == GD_OK);
    CHECK((*api).pen_set_width(first, 2.0) == GD_ERROR_STALE_HANDLE);
    /* The freed slot is reused first, with its generation advanced: the old
       handle names nothing, the new one its own object. */
    CHECK((*api).pen_create((gd_color){UINT32_C(0xff000000), 0}, 1.0, &second) == GD_OK);
    CHECK(second.slot == first.slot && second.generation != first.generation && second.generation != 0U);
    CHECK((*api).pen_set_width(first, 2.0) == GD_ERROR_STALE_HANDLE);
    CHECK((*api).retain(first) == GD_ERROR_STALE_HANDLE);
    CHECK((*api).pen_set_width(second, 2.0) == GD_OK);
    CHECK((*api).object_kind(second, &kind) == GD_OK && kind == (uint32_t)GD_OBJECT_PEN);

    /* dispose against references: the handle stays valid until released. */
    retained = second;
    CHECK((*api).retain(retained) == GD_OK);
    CHECK((*api).dispose(second) == GD_OK);
    CHECK((*api).object_state(second, &state) == GD_OK && state == (uint32_t)GD_OBJECT_DISPOSED);
    CHECK((*api).pen_set_width(second, 3.0) == GD_ERROR_DISPOSED);
    CHECK((*api).release(second) == GD_OK);
    CHECK((*api).object_state(retained, &state) == GD_OK && state == (uint32_t)GD_OBJECT_DISPOSED);
    CHECK((*api).release(retained) == GD_OK);
    CHECK((*api).object_state(retained, &state) == GD_ERROR_STALE_HANDLE);
    CHECK((*api).release(retained) == GD_ERROR_STALE_HANDLE);
    return 0;
}

/* Reads a recorder's trace into a fresh NUL-terminated buffer the caller
   frees, or NULL. */
static char* read_trace(const gd_api_v0* api, gd_handle recorder) {
    uint64_t required = 0;
    char* trace = NULL;
    if ((*api).recorder_trace(recorder, NULL, 0, &required) != GD_ERROR_BUFFER_TOO_SMALL) return NULL;
    trace = (char*)malloc((size_t)required + 1U);
    if (trace == NULL) return NULL;
    if ((*api).recorder_trace(recorder, trace, required, &required) != GD_OK) {
        free(trace);
        return NULL;
    }
    trace[required] = '\0';
    return trace;
}

/* ABI 0.7, text at any angle: one draw_string command under the turn
   followed by the transform in force, which stays in force; 0 degrees
   records what recorder_draw_string records; refusals record nothing. */
static int check_rotated_text(const gd_api_v0* api) {
    gd_handle level = {0};
    gd_handle turned = {0};
    gd_handle font = {0};
    gd_handle format = {0};
    gd_handle brush = {0};
    uint64_t count = 0;
    char* level_trace = NULL;
    char* turned_trace = NULL;
    const char* entry = NULL;

    CHECK((*api).recorder_draw_string_rotated != NULL);
    CHECK((*api).font_create(view_of("Lucida Grande"), 12.0, 0, 3, 1, &font) == GD_OK);
    CHECK((*api).string_format_create(0, &format) == GD_OK);
    CHECK((*api).solid_brush_create((gd_color){UINT32_C(0xff000000), 0}, &brush) == GD_OK);
    CHECK((*api).recorder_create(&level) == GD_OK);
    CHECK((*api).recorder_create(&turned) == GD_OK);
    CHECK((*api).recorder_translate(level, 10, 5) == GD_OK);
    CHECK((*api).recorder_translate(turned, 10, 5) == GD_OK);
    CHECK((*api).recorder_draw_string(level, view_of("axis"), font, brush, (gd_point){4, 7}, format) == GD_OK);
    CHECK((*api).recorder_draw_string_rotated(turned, view_of("axis"), font, brush, (gd_point){4, 7}, 0.0,
                                              format) == GD_OK);
    level_trace = read_trace(api, level);
    turned_trace = read_trace(api, turned);
    CHECK(level_trace != NULL && turned_trace != NULL);
    CHECK(strcmp(level_trace, turned_trace) == 0);
    free(turned_trace);
    turned_trace = NULL;

    /* A quarter turn about (4, 7) under translate(10, 5): m12 = -1 and
       m21 = 1 exactly (sin of a quarter turn), and the next command is back
       under the plain translation. */
    CHECK((*api).recorder_draw_string_rotated(turned, view_of("axis"), font, brush, (gd_point){4, 7}, 90.0,
                                              format) == GD_OK);
    CHECK((*api).recorder_fill_rectangle(turned, brush, (gd_rect){0, 0, 2, 2}) == GD_OK);
    CHECK((*api).recorder_draw_string_rotated(turned, view_of("axis"), font, brush, (gd_point){4, 7}, INFINITY,
                                              format) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_string_rotated(turned, view_of("axis"), font, brush, (gd_point){NAN, 7}, 30.0,
                                              format) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_string_rotated(turned, view_of("\xff"), font, brush, (gd_point){4, 7}, 30.0,
                                              format) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_command_count(turned, &count) == GD_OK && count == 4U);
    turned_trace = read_trace(api, turned);
    CHECK(turned_trace != NULL);
    entry = strstr(turned_trace, "2|draw_string|at=4,7|");
    CHECK(entry != NULL);
    entry = strstr(entry, "|transform=");
    CHECK(entry != NULL && strstr(entry, ",-1,1,") != NULL);
    CHECK(strchr(entry, '\n') != NULL && strstr(entry, ",-1,1,") < strchr(entry, '\n'));
    CHECK(strstr(turned_trace, "3|fill_rectangle|rect=0,0,2,2|") != NULL);
    CHECK(strstr(strstr(turned_trace, "3|fill_rectangle|"), "|transform=1,0,0,1,10,5|") != NULL);

    free(level_trace);
    free(turned_trace);
    CHECK((*api).release(turned) == GD_OK);
    CHECK((*api).release(level) == GD_OK);
    CHECK((*api).release(brush) == GD_OK);
    CHECK((*api).release(format) == GD_OK);
    CHECK((*api).release(font) == GD_OK);
    return 0;
}

static int check_drawing_primitives(const gd_api_v0* api) {
    gd_handle recorder = {0};
    gd_handle pen = {0};
    gd_handle brush = {0};
    gd_handle conic = {0};
    gd_handle checker = {0};
    gd_handle font = {0};
    gd_handle format = {0};
    gd_handle path = {0};
    uint64_t count = 0;
    uint64_t required = 0;
    uint32_t kind = 0;
    double ratio = 0.0;
    gd_color chosen = {0};
    gd_rect bounds = {0};
    char* trace = NULL;
    const gd_point polyline[3] = {{0, 0}, {10, 5}, {20, 0}};
    const gd_point segments[4] = {{0, 0}, {1, 1}, {2, 2}, {3, 3}};
    const gd_rect rects[2] = {{0, 0, 2, 2}, {4, 4, 2, 2}};
    const gd_point centers[2] = {{5, 5}, {15, 5}};
    const gd_color stops[3] = {{UINT32_C(0xffff0000), 0}, {UINT32_C(0xffffffff), 0}, {UINT32_C(0xff0000ff), 0}};
    const double positions[3] = {0, 0.5, 1};
    gd_marker_style marker;
    gd_grid_pattern grid;
    const gd_corner_radii radii = {5, 5, 5, 5};
    const gd_corner_radii negative = {-1, 0, 0, 0};

    memset(&marker, 0, sizeof(marker));
    marker.shape = GD_MARKER_DIAMOND;
    marker.size = 8.0;
    marker.fill = (gd_color){UINT32_C(0xffffffff), 0};
    marker.edge = (gd_color){UINT32_C(0xff000000), 0};
    marker.edge_width = 1.0;
    memset(&grid, 0, sizeof(grid));
    grid.origin = (gd_point){0.5, 0.5};
    grid.spacing = 8.0;
    grid.major_every = 4U;
    grid.minor = (gd_color){UINT32_C(0x40ffffff), 0};
    grid.major = (gd_color){UINT32_C(0x80ffffff), 0};
    grid.line_width = 1.0;

    CHECK((*api).graphics_path_add_rounded_rectangle != NULL);
    CHECK((*api).recorder_create(&recorder) == GD_OK);
    CHECK((*api).pen_create((gd_color){UINT32_C(0xffff0000), 0}, 2.0, &pen) == GD_OK);
    CHECK((*api).pen_set_line_join(pen, GD_LINE_JOIN_ROUND, 4.0) == GD_OK);
    CHECK((*api).pen_set_line_cap(pen, GD_LINE_CAP_SQUARE) == GD_OK);
    CHECK((*api).pen_set_line_join(pen, 3U, 4.0) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).pen_set_line_join(pen, GD_LINE_JOIN_MITER, 0.5) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).pen_set_line_cap(pen, 3U) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).solid_brush_create((gd_color){UINT32_C(0xff0000ff), 0}, &brush) == GD_OK);

    CHECK((*api).recorder_draw_lines(recorder, pen, polyline, 3) == GD_OK);
    CHECK((*api).recorder_fill_rectangles(recorder, brush, rects, 2) == GD_OK);
    CHECK((*api).recorder_draw_line_segments(recorder, pen, segments, 4) == GD_OK);
    CHECK((*api).recorder_draw_markers(recorder, centers, 2, &marker) == GD_OK);
    CHECK((*api).recorder_fill_grid_pattern(recorder, (gd_rect){0, 0, 64, 32}, &grid) == GD_OK);
    CHECK((*api).recorder_fill_proportion(recorder, brush, (gd_rect){0, 0, 100, 10}, radii, 1.5,
                                          GD_FILL_RIGHT_TO_LEFT) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 6U);

    /* Refusals record nothing. */
    CHECK((*api).recorder_draw_lines(recorder, pen, polyline, 1) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_lines(recorder, pen, NULL, 3) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_line_segments(recorder, pen, segments, 3) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_rectangles(recorder, brush, rects, 0) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_markers(recorder, centers, 2, NULL) == GD_ERROR_INVALID_ARGUMENT);
    marker.shape = 10U;
    CHECK((*api).recorder_draw_markers(recorder, centers, 2, &marker) == GD_ERROR_INVALID_ARGUMENT);
    marker.shape = GD_MARKER_DIAMOND;
    marker.size = 0.0;
    CHECK((*api).recorder_draw_markers(recorder, centers, 2, &marker) == GD_ERROR_INVALID_ARGUMENT);
    grid.spacing = 1.0;
    CHECK((*api).recorder_fill_grid_pattern(recorder, (gd_rect){0, 0, 64, 32}, &grid) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_grid_pattern(recorder, (gd_rect){0, 0, 64, 32}, NULL) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_proportion(recorder, brush, (gd_rect){0, 0, 10, 10}, negative, 0.5,
                                          GD_FILL_LEFT_TO_RIGHT) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_proportion(recorder, brush, (gd_rect){0, 0, 10, 10}, radii, 0.5, 4U) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 6U);

    CHECK((*api).conic_gradient_brush_create((gd_point){8, 8}, 30.0, stops[0], stops[2], &conic) == GD_OK);
    CHECK((*api).object_kind(conic, &kind) == GD_OK && kind == GD_OBJECT_CONIC_GRADIENT_BRUSH);
    CHECK((*api).conic_gradient_set_interpolation(conic, stops, positions, 3) == GD_OK);
    CHECK((*api).conic_gradient_set_interpolation(brush, stops, positions, 3) == GD_ERROR_WRONG_HANDLE_KIND);
    CHECK((*api).checker_brush_create(4.0, stops[1], (gd_color){UINT32_C(0xffc0c0c0), 0}, &checker) == GD_OK);
    CHECK((*api).object_kind(checker, &kind) == GD_OK && kind == GD_OBJECT_CHECKER_BRUSH);
    {
        gd_handle refused = {0};
        CHECK((*api).checker_brush_create(0.0, stops[1], stops[1], &refused) == GD_ERROR_INVALID_ARGUMENT);
    }
    CHECK((*api).recorder_fill_rectangle(recorder, conic, (gd_rect){0, 0, 16, 16}) == GD_OK);
    CHECK((*api).recorder_fill_rectangles(recorder, checker, rects, 2) == GD_OK);

    CHECK((*api).contrast_ratio(stops[1], (gd_color){UINT32_C(0xff000000), 0}, &ratio) == GD_OK);
    CHECK(ratio > 20.999 && ratio < 21.001);
    CHECK((*api).contrast_ratio(stops[1], stops[1], NULL) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).contrasting_text_color((gd_color){UINT32_C(0xffffff00), 0}, stops[1],
                                        (gd_color){UINT32_C(0xff000000), 0}, &chosen) == GD_OK);
    CHECK(chosen.argb == UINT32_C(0xff000000) && chosen.is_empty == 0U);

    CHECK((*api).font_create(view_of("Lucida Grande"), 12.0, 0, 3, 1, &font) == GD_OK);
    CHECK((*api).string_format_create(0, &format) == GD_OK);
    CHECK((*api).recorder_draw_string_over_fill(recorder, view_of("50%"), font, brush, stops[1],
                                                (gd_point){0, 0}, format, (gd_rect){10, 0, 20, 10}) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 13U);

    CHECK((*api).graphics_path_create(GD_FILL_ALTERNATE, &path) == GD_OK);
    CHECK((*api).graphics_path_add_rounded_rectangle(path, (gd_rect){0, 0, 20, 10}, radii) == GD_OK);
    CHECK((*api).graphics_path_add_rounded_rectangle(path, (gd_rect){0, 0, 20, 10}, negative) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).graphics_path_bounds(path, &bounds) == GD_OK);
    CHECK(bounds.x == 0.0 && bounds.y == 0.0 && bounds.width == 20.0 && bounds.height == 10.0);

    CHECK((*api).recorder_trace(recorder, NULL, 0, &required) == GD_ERROR_BUFFER_TOO_SMALL);
    trace = (char*)malloc((size_t)required + 1U);
    CHECK(trace != NULL);
    CHECK((*api).recorder_trace(recorder, trace, required, &required) == GD_OK);
    trace[required] = '\0';
    CHECK(strstr(trace, "0|draw_lines|points=0,0;10,5;20,0|pen=ffff0000,2,0,join=2/4,cap=1|") != NULL);
    CHECK(strstr(trace, "3|draw_markers|centers=5,5;15,5|marker=2,8,ffffffff,ff000000,1|") != NULL);
    CHECK(strstr(trace, "5|fill_proportion|track=0,0,100,10|radii=5,5,5,5|proportion=1|direction=1|") != NULL);
    CHECK(strstr(trace, "7|fill_rectangles|rects=0,0,2,2;4,4,2,2|brush=6,ffffffff,ffc0c0c0,") != NULL);
    CHECK(strstr(trace, "8|draw_string|at=0,0|") != NULL);
    free(trace);

    CHECK((*api).release(path) == GD_OK);
    CHECK((*api).release(format) == GD_OK);
    CHECK((*api).release(font) == GD_OK);
    CHECK((*api).release(checker) == GD_OK);
    CHECK((*api).release(conic) == GD_OK);
    CHECK((*api).release(brush) == GD_OK);
    CHECK((*api).release(pen) == GD_OK);
    CHECK((*api).release(recorder) == GD_OK);
    return 0;
}

/* ABI 0.4: the gapped and per-element marks record 0.3 commands, break at
   NaN points, refuse infinities and record nothing on a refusal. */
static int check_gapped_marks(const gd_api_v0* api) {
    gd_handle recorder = {0};
    gd_handle pen = {0};
    gd_handle brush = {0};
    uint64_t count = 0;
    uint64_t required = 0;
    char* trace = NULL;
    const double gap = NAN;
    const gd_point upper[5] = {{0, 1}, {1, 2}, {2, gap}, {3, 2}, {4, 1}};
    const gd_point lower[5] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}, {4, 0}};
    const gd_point lone_upper[3] = {{0, 1}, {1, gap}, {2, 1}};
    const gd_point lone_lower[3] = {{0, 0}, {1, 0}, {2, 0}};
    const gd_point runs[6] = {{0, 0}, {1, 1}, {gap, 0}, {3, 3}, {gap, gap}, {5, 5}};
    const gd_point infinite[2] = {{0, 0}, {HUGE_VAL, 0}};
    const gd_rect cells[4] = {{0, 0, 1, 1}, {1, 0, 1, 1}, {2, 0, 1, 1}, {3, 0, 1, 1}};
    const gd_color colors[4] = {{UINT32_C(0xffff0000), 0}, {UINT32_C(0xffff0000), 0},
                                {UINT32_C(0xff0000ff), 0}, {UINT32_C(0xffff0000), 0}};

    CHECK((*api).recorder_create(&recorder) == GD_OK);
    CHECK((*api).pen_create((gd_color){UINT32_C(0xff00ff00), 0}, 1.0, &pen) == GD_OK);
    CHECK((*api).solid_brush_create((gd_color){UINT32_C(0x800000ff), 0}, &brush) == GD_OK);

    /* Two runs of two samples: two polygons, each upper forward, lower back. */
    CHECK((*api).recorder_fill_band(recorder, brush, upper, lower, 5) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 2U);
    /* Runs of one sample record nothing. */
    CHECK((*api).recorder_fill_band(recorder, brush, lone_upper, lone_lower, 3) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 2U);
    /* Runs {0,1} and {3}, {5}: one draw_lines. */
    CHECK((*api).recorder_draw_polyline_runs(recorder, pen, runs, 6) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 3U);
    /* Red, red | blue | red: three fill_rectangles. */
    CHECK((*api).recorder_fill_rectangles_each(recorder, cells, colors, 4) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 6U);

    /* Refusals record nothing. */
    CHECK((*api).recorder_fill_band(recorder, brush, upper, NULL, 5) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_band(recorder, brush, upper, lower, 1) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_band(recorder, brush, infinite, lower, 2) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_polyline_runs(recorder, pen, runs, 1) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_polyline_runs(recorder, pen, infinite, 2) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_rectangles_each(recorder, cells, NULL, 4) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_fill_rectangles_each(recorder, cells, colors, 0) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_lines(recorder, pen, runs, 6) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 6U);

    CHECK((*api).recorder_trace(recorder, NULL, 0, &required) == GD_ERROR_BUFFER_TOO_SMALL);
    trace = (char*)malloc((size_t)required + 1U);
    CHECK(trace != NULL);
    CHECK((*api).recorder_trace(recorder, trace, required, &required) == GD_OK);
    trace[required] = '\0';
    CHECK(strstr(trace, "0|fill_polygon|") != NULL);
    CHECK(strstr(trace, "1|fill_polygon|") != NULL);
    CHECK(strstr(trace, "2|draw_lines|points=0,0;1,1|") != NULL);
    CHECK(strstr(trace, "3|fill_rectangles|rects=0,0,1,1;1,0,1,1|brush=ffff0000|") != NULL);
    CHECK(strstr(trace, "4|fill_rectangles|rects=2,0,1,1|brush=ff0000ff|") != NULL);
    CHECK(strstr(trace, "5|fill_rectangles|rects=3,0,1,1|brush=ffff0000|") != NULL);
    free(trace);

    CHECK((*api).release(brush) == GD_OK);
    CHECK((*api).release(pen) == GD_OK);
    CHECK((*api).release(recorder) == GD_OK);
    return 0;
}

/* Whether the trace holds `needle`. */
static int trace_has(const gd_api_v0* api, gd_handle recorder, const char* needle) {
    uint64_t required = 0;
    char* trace = NULL;
    int found = 0;
    if ((*api).recorder_trace(recorder, NULL, 0, &required) != GD_ERROR_BUFFER_TOO_SMALL) return 0;
    trace = (char*)malloc((size_t)required + 1U);
    if (trace == NULL) return 0;
    if ((*api).recorder_trace(recorder, trace, required, &required) == GD_OK) {
        trace[required] = '\0';
        found = strstr(trace, needle) != NULL;
    }
    free(trace);
    return found;
}

/* ABI 0.5 and the clip rules (the flame graph demonstrator's D2 to D5):
   text in a box, batched strings, reset for reuse, closed recorders refuse,
   and set_clip replaces the clip in force, in target coordinates, restored
   by recorder_restore. */
static int check_text_and_reuse(const gd_api_v0* api) {
    gd_handle recorder = {0};
    gd_handle brush = {0};
    gd_handle font = {0};
    gd_handle format = {0};
    uint64_t count = 0;
    uint64_t token = 0;
    uint64_t fitted = 0;
    double fitted_width = 0.0;
    gd_text_item items[3];
    gd_text_item bad[2];

    CHECK((*api).recorder_create(&recorder) == GD_OK);
    CHECK((*api).solid_brush_create((gd_color){UINT32_C(0xff102030), 0}, &brush) == GD_OK);
    CHECK((*api).font_create(view_of("Lucida Grande"), 12.0, 0, 3, 1, &font) == GD_OK);
    CHECK((*api).string_format_create(0, &format) == GD_OK);
    CHECK((*api).string_format_set(format, GD_STRING_CENTER, GD_STRING_FAR, GD_TRIM_ELLIPSIS_CHARACTER, 0) == GD_OK);

    /* D5: set_clip replaces; the clip is in target coordinates. */
    CHECK((*api).recorder_set_clip(recorder, (gd_rect){0, 0, 100, 50}) == GD_OK);
    CHECK((*api).recorder_set_clip(recorder, (gd_rect){10, 10, 20, 20}) == GD_OK);
    CHECK(trace_has(api, recorder, "1|set_clip|"));
    CHECK(trace_has(api, recorder, "|clip=10,10,20,20|"));
    CHECK((*api).recorder_save(recorder, &token) == GD_OK);
    CHECK((*api).recorder_translate(recorder, 5.0, 5.0) == GD_OK);
    CHECK((*api).recorder_set_clip(recorder, (gd_rect){0, 0, 10, 10}) == GD_OK);
    CHECK(trace_has(api, recorder, "|clip=5,5,10,10|"));
    CHECK((*api).recorder_restore(recorder, token) == GD_OK);
    CHECK((*api).recorder_fill_rectangle(recorder, brush, (gd_rect){0, 0, 1, 1}) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 7U);
    CHECK(trace_has(api, recorder, "6|fill_rectangle|"));

    /* D4: reset empties the recording, reopens it and drops saved tokens. */
    CHECK((*api).recorder_save(recorder, &token) == GD_OK);
    CHECK((*api).recorder_close(recorder) == GD_OK);
    /* A closed recorder refuses drawing as a state (seal decision 4). */
    CHECK((*api).recorder_fill_rectangle(recorder, brush, (gd_rect){0, 0, 1, 1}) == GD_ERROR_INVALID_STATE);
    CHECK((*api).recorder_reset(recorder) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 0U);
    CHECK((*api).recorder_restore(recorder, token) == GD_ERROR_INVALID_ARGUMENT);
    CHECK(trace_has(api, recorder, "end|commands=0|closed=0\n"));

    /* D2: a string laid out in a box records its layout. */
    CHECK((*api).recorder_draw_string_in_rect(recorder, view_of("flame cell"), font, brush,
                                              (gd_rect){4, 6, 40, 14}, format) == GD_OK);
    CHECK(trace_has(api, recorder, "|text=flame cell|layout=4,6,40,14|"));
    CHECK((*api).recorder_draw_string_in_rect(recorder, view_of("x"), font, brush,
                                              (gd_rect){0, 0, -1, 14}, format) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_string_in_rect(recorder, view_of("x"), font, brush,
                                              (gd_rect){0, 0, NAN, 14}, format) == GD_ERROR_INVALID_ARGUMENT);

    /* D3: one draw_string per item, each in its colour; a bad item refuses
       the whole list. */
    items[0].text = view_of("main");
    items[0].origin = (gd_point){0, 0};
    items[0].color = (gd_color){UINT32_C(0xffff0000), 0};
    items[1].text = view_of("parse");
    items[1].origin = (gd_point){0, 20};
    items[1].color = (gd_color){UINT32_C(0xff00ff00), 0};
    items[2].text = view_of("render");
    items[2].origin = (gd_point){0, 40};
    items[2].color = (gd_color){UINT32_C(0xff0000ff), 0};
    CHECK((*api).recorder_draw_strings(recorder, font, format, items, 3) == GD_OK);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 4U);
    CHECK(trace_has(api, recorder, "|brush=ff00ff00|"));
    CHECK(trace_has(api, recorder, "|text=render|"));
    bad[0] = items[0];
    bad[1] = items[1];
    bad[1].origin = (gd_point){HUGE_VAL, 0};
    CHECK((*api).recorder_draw_strings(recorder, font, format, bad, 2) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_strings(recorder, font, format, items, 0) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_draw_strings(recorder, font, format, NULL, 1) == GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).recorder_command_count(recorder, &count) == GD_OK && count == 4U);

    /* The fitted measure validates before it needs a raster service. */
    CHECK((*api).measure_string_fitted(view_of("abc"), font, format, -1.0, &fitted, &fitted_width) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK((*api).measure_string_fitted(view_of("abc"), font, format, 10.0, NULL, &fitted_width) ==
          GD_ERROR_INVALID_ARGUMENT);
    /* Without the raster it is not available, as measure_string is
     * (A-gf-abi-01-F14). */
    CHECK((*api).measure_string_fitted(view_of("abc"), font, format, 10.0, &fitted, &fitted_width) ==
          GD_ERROR_NOT_AVAILABLE);

    CHECK((*api).release(format) == GD_OK);
    CHECK((*api).release(font) == GD_OK);
    CHECK((*api).release(brush) == GD_OK);
    CHECK((*api).release(recorder) == GD_OK);
    return 0;
}

int main(void) {
    gd_api_v0 api;
    gd_api_v0 prefix;
    gd_handle brush = {0};
    gd_handle pen = {0};
    gd_handle font = {0};
    gd_handle substituted_font = {0};
    gd_font_resolution font_resolution;
    gd_handle format = {0};
    gd_handle recorder = {0};
    gd_handle extended_recorder = {0};
    gd_handle path = {0};
    gd_handle image = {0};
    gd_handle attributes = {0};
    gd_handle bitmap = {0};
    gd_handle bitmap_clone = {0};
    gd_handle bitmap_thumbnail = {0};
    gd_handle bitmap_adjusted = {0};
    gd_handle path_clone = {0};
    gd_handle hatch = {0};
    gd_handle linear_gradient = {0};
    gd_handle path_gradient = {0};
    gd_handle region = {0};
    gd_handle stale_brush = {0};
    uint64_t token = 0;
    uint64_t required = 0;
    uint64_t count = 0;
    uint32_t kind = 0;
    uint32_t state = 0;
    uint32_t visible = 0;
    char* trace = NULL;
    double invalid_dash = 0.0;
    double color_matrix[25] = {0};
    gd_point polygon[3] = {{0, 0}, {10, 0}, {5, 8}};
    gd_point gradient_boundary[4] = {{0, 0}, {10, 0}, {10, 10}, {0, 10}};
    gd_point* path_points = NULL;
    uint64_t path_point_count = 0;
    gd_rect path_bounds = {0};
    gd_bitmap_lock_view bitmap_lock = {0};
    gd_bitmap_edit_view bitmap_edit = {0};
    gd_bitmap_damage_summary damage_summary = {0};
    gd_rect_i bitmap_damage[2] = {{0}};
    uint64_t bitmap_damage_count = 0;
    gd_color pixel = {0};
    uint32_t bitmap_width = 0;
    uint32_t bitmap_height = 0;
    uint32_t bitmap_format = 0;
    uint64_t bitmap_generation = 0;

    memset(&prefix, 0, sizeof(prefix));
    prefix.struct_size = (uint32_t)(sizeof(uint32_t) * 2U);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_1, &prefix) == GD_OK);
    CHECK(prefix.struct_size == offsetof(gd_api_v0, bitmap_edit_begin));
    CHECK(prefix.abi_version == GD_ABI_VERSION_0_1);

    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(UINT32_C(0xffffffff), &api) ==
          GD_ERROR_UNSUPPORTED_VERSION);
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_2, &api) == GD_OK);
    CHECK(api.struct_size == offsetof(gd_api_v0, pen_set_line_join));
    CHECK(api.abi_version == GD_ABI_VERSION_0_2);
    CHECK(api.pen_set_line_join == NULL);
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_3, &api) == GD_OK);
    CHECK(api.struct_size == offsetof(gd_api_v0, recorder_fill_band));
    CHECK(api.abi_version == GD_ABI_VERSION_0_3);
    CHECK(api.graphics_path_add_rounded_rectangle != NULL);
    CHECK(api.recorder_fill_band == NULL);
    CHECK(api.graphics_path_add_quadratic == NULL);
    /* The curves are 0.6, after 0.5's measure_string_fitted: their slots
       follow the 0.4 and 0.5 entries, whatever a fold adds before them. */
    CHECK(offsetof(gd_api_v0, graphics_path_add_quadratic) ==
          offsetof(gd_api_v0, measure_string_fitted) + sizeof(api.measure_string_fitted));
    CHECK(offsetof(gd_api_v0, recorder_draw_string_in_rect) ==
          offsetof(gd_api_v0, recorder_fill_band) + 3U * sizeof(api.recorder_fill_band));
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_4, &api) == GD_OK);
    CHECK(api.struct_size == offsetof(gd_api_v0, recorder_draw_string_in_rect));
    CHECK(api.abi_version == GD_ABI_VERSION_0_4);
    CHECK(api.recorder_fill_rectangles_each != NULL);
    CHECK(api.recorder_draw_string_in_rect == NULL);
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_5, &api) == GD_OK);
    CHECK(api.struct_size == offsetof(gd_api_v0, graphics_path_add_quadratic));
    CHECK(api.abi_version == GD_ABI_VERSION_0_5);
    CHECK(api.measure_string_fitted != NULL);
    CHECK(api.graphics_path_add_quadratic == NULL);
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_6, &api) == GD_OK);
    CHECK(api.struct_size == offsetof(gd_api_v0, recorder_draw_string_rotated));
    CHECK(api.abi_version == GD_ABI_VERSION_0_6);
    CHECK(api.graphics_path_fill_mode != NULL);
    CHECK(api.recorder_draw_string_rotated == NULL);
    /* Text at any angle is 0.7, after 0.6's graphics_path_fill_mode. */
    CHECK(offsetof(gd_api_v0, recorder_draw_string_rotated) ==
          offsetof(gd_api_v0, graphics_path_fill_mode) + sizeof(api.graphics_path_fill_mode));
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_7, &api) == GD_OK);
    CHECK(api.struct_size == offsetof(gd_api_v0, recorder_paint_target));
    CHECK(api.abi_version == GD_ABI_VERSION_0_7);
    CHECK(api.recorder_draw_string_rotated != NULL);
    CHECK(api.recorder_paint_target == NULL);
    /* 1.0 is the whole table: the bridge after 0.7's rotated text, then the
       eight gradient brush aliases. 144 entries after the two size fields;
       a mislabelled or reordered append fails here. */
    CHECK(offsetof(gd_api_v0, recorder_paint_target) ==
          offsetof(gd_api_v0, recorder_draw_string_rotated) + sizeof(api.recorder_draw_string_rotated));
    CHECK(offsetof(gd_api_v0, linear_gradient_brush_set_blend) ==
          offsetof(gd_api_v0, recorder_paint_target) + sizeof(api.recorder_paint_target));
    CHECK(offsetof(gd_api_v0, graphics_path_flatten) ==
          offsetof(gd_api_v0, conic_gradient_brush_set_interpolation) +
              sizeof(api.conic_gradient_brush_set_interpolation));
    CHECK((offsetof(gd_api_v0, graphics_path_flatten) - offsetof(gd_api_v0, last_error)) /
              sizeof(api.last_error) == 144U);
    CHECK(offsetof(gd_api_v0, recorder_fill_band) - offsetof(gd_api_v0, last_error) ==
          120U * sizeof(api.last_error));
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_1_0, &api) == GD_OK);
    CHECK(api.struct_size == offsetof(gd_api_v0, graphics_path_flatten));
    CHECK(api.abi_version == GD_ABI_VERSION_1_0);
    CHECK(api.recorder_paint_target != NULL);
    CHECK(api.graphics_path_flatten == NULL);
    CHECK(api.raster_initialize == NULL);
    CHECK(api.linear_gradient_brush_set_blend == api.linear_gradient_set_blend);
    CHECK(api.linear_gradient_brush_set_interpolation == api.linear_gradient_set_interpolation);
    CHECK(api.linear_gradient_brush_set_wrap_mode == api.linear_gradient_set_wrap_mode);
    CHECK(api.path_gradient_brush_set_center_color == api.path_gradient_set_center_color);
    CHECK(api.path_gradient_brush_set_center_point == api.path_gradient_set_center_point);
    CHECK(api.path_gradient_brush_set_surround_colors == api.path_gradient_set_surround_colors);
    CHECK(api.path_gradient_brush_set_interpolation == api.path_gradient_set_interpolation);
    CHECK(api.conic_gradient_brush_set_interpolation == api.conic_gradient_set_interpolation);
    /* A caller with a shorter table (built against 0.7) asking for 1.0 gets
       its own prefix filled and learns the full size. */
    {
        gd_api_v0 shorter;
        memset(&shorter, 0, sizeof(shorter));
        shorter.struct_size = (uint32_t)offsetof(gd_api_v0, recorder_paint_target);
        CHECK(gd_get_api_v0(GD_ABI_VERSION_1_0, &shorter) == GD_OK);
        CHECK(shorter.struct_size == offsetof(gd_api_v0, graphics_path_flatten));
        CHECK(shorter.recorder_draw_string_rotated != NULL);
        CHECK(shorter.recorder_paint_target == NULL);
    }
    /* 1.1 (provisional, the number a placeholder) is the whole table:
       graphics_path_flatten after the 1.0 aliases, then the raster's
       raster_initialize. This program links no raster, so it answers
       NOT_AVAILABLE and leaves the raster entries refusing as before. */
    CHECK(offsetof(gd_api_v0, raster_initialize) ==
          offsetof(gd_api_v0, graphics_path_flatten) + sizeof(api.graphics_path_flatten));
    /* Then the texture and radial gradient brushes, six entries in fold
       order, and the radial brush's radii; the table ends with them. */
    CHECK(offsetof(gd_api_v0, texture_brush_create) ==
          offsetof(gd_api_v0, raster_initialize) + sizeof(api.raster_initialize));
    CHECK((offsetof(gd_api_v0, radial_gradient_brush_set_focus) - offsetof(gd_api_v0, texture_brush_create)) /
              sizeof(api.last_error) == 5U);
    CHECK(offsetof(gd_api_v0, radial_gradient_brush_set_radii) ==
          offsetof(gd_api_v0, radial_gradient_brush_set_focus) + sizeof(api.radial_gradient_brush_set_focus));
    CHECK(sizeof(gd_api_v0) ==
          offsetof(gd_api_v0, radial_gradient_brush_set_radii) + sizeof(api.radial_gradient_brush_set_radii));
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_1_1, &api) == GD_OK);
    CHECK(api.struct_size == sizeof(api));
    CHECK(api.abi_version == GD_ABI_VERSION_1_1);
    CHECK(api.graphics_path_flatten != NULL);
    CHECK(api.raster_initialize != NULL);
    CHECK(api.raster_initialize() == GD_ERROR_NOT_AVAILABLE);
    CHECK(api.texture_brush_create != NULL);
    CHECK(api.radial_gradient_brush_set_focus != NULL);
    CHECK(api.radial_gradient_brush_set_radii != NULL);
    CHECK(gd_get_api_v0(UINT32_C(0x00010002), &api) == GD_ERROR_UNSUPPORTED_VERSION);
    CHECK(gd_get_api_v0(UINT32_C(0x00020000), &api) == GD_ERROR_UNSUPPORTED_VERSION);
    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_1_1, &api) == GD_OK);
    CHECK(api.graphics_path_add_quadratic != NULL);
    CHECK(api.graphics_path_fill_mode != NULL);
    CHECK(api.recorder_trace != NULL);
    CHECK(api.graphics_path_create != NULL);
    CHECK(api.recorder_draw_image != NULL);
    CHECK(api.bitmap_create != NULL);
    CHECK(api.path_gradient_brush_create != NULL);
    CHECK(api.bitmap_export_hbitmap != NULL);
    CHECK(api.bitmap_import_hbitmap != NULL);
    CHECK(api.native_surface_capture != NULL);
    CHECK(api.native_surface_present != NULL);
    CHECK(api.bitmap_acquire_hdc != NULL);
    CHECK(api.bitmap_release_hdc != NULL);
    CHECK(api.bitmap_edit_begin != NULL);
    CHECK(api.bitmap_edit_commit != NULL);
    CHECK(api.bitmap_edit_cancel != NULL);
    CHECK(api.bitmap_changes_since != NULL);
    CHECK(api.font_resolve != NULL);

    CHECK(api.solid_brush_create((gd_color){UINT32_C(0xffff0000), 0},
                                 &brush) == GD_OK);
    CHECK(api.pen_create((gd_color){UINT32_C(0xffffff00), 0}, 2.0, &pen) == GD_OK);
    CHECK(api.pen_set_dash_style(pen, GD_DASH_DASH) == GD_OK);
    CHECK(api.pen_set_dash_pattern(pen, &invalid_dash, 1) ==
          GD_ERROR_INVALID_ARGUMENT);
    CHECK(api.pen_set_dash_style(pen, GD_DASH_DASH) == GD_OK);
    CHECK(api.font_create(view_of("Lucida Grande"), 12.0, 1, 3, 1, &font) == GD_OK);
    /* Stage F without a raster module: the table's answer, nothing loaded. */
    memset(&font_resolution, 0, sizeof(font_resolution));
    font_resolution.struct_size = (uint32_t)(sizeof(uint32_t) * 2U);
    CHECK(api.font_resolve(font, &font_resolution) == GD_ERROR_INVALID_ARGUMENT);
    font_resolution.struct_size = (uint32_t)sizeof(font_resolution);
    CHECK(api.font_resolve(font, &font_resolution) == GD_OK);
    CHECK(font_resolution.struct_size == sizeof(font_resolution));
    CHECK(font_resolution.source == GD_FONT_SOURCE_UNMAPPED);
    CHECK(font_resolution.requested_style == 1U);
    CHECK(font_resolution.family.size == 0U && font_resolution.face_loaded == 0U);
    CHECK(api.font_create(view_of("CALIBRI"), 12.0, 3, 3, 1, &substituted_font) == GD_OK);
    CHECK(api.font_resolve(substituted_font, &font_resolution) == GD_OK);
    CHECK(font_resolution.source == GD_FONT_SOURCE_SUBSTITUTION);
    CHECK(font_resolution.mapping == GD_FONT_MAPPING_METRIC_COMPATIBLE);
    CHECK(font_resolution.requested_style == 3U && font_resolution.face_style == 3U);
    CHECK(font_resolution.family.size == 7U &&
          memcmp(font_resolution.family.data, "Carlito", 7U) == 0);
    CHECK(font_resolution.face_file.size == 22U &&
          memcmp(font_resolution.face_file.data, "Carlito-BoldItalic.ttf", 22U) == 0);
    CHECK(api.release(substituted_font) == GD_OK);
    CHECK(api.font_resolve(substituted_font, &font_resolution) == GD_ERROR_STALE_HANDLE);
    CHECK(api.string_format_create(4, &format) == GD_OK);
    CHECK(api.string_format_set(format, GD_STRING_CENTER, GD_STRING_NEAR,
                                GD_TRIM_ELLIPSIS_CHARACTER, 4) == GD_OK);
    CHECK(api.recorder_create(&recorder) == GD_OK);
    CHECK(api.object_kind(recorder, &kind) == GD_OK && kind == GD_OBJECT_RECORDER);
    CHECK(api.recorder_clear(recorder,
                             (gd_color){UINT32_C(0xff000000), 0}) == GD_OK);
    CHECK(api.recorder_save(recorder, &token) == GD_OK && token == 1);
    CHECK(api.recorder_translate(recorder, 10.0, 5.0) == GD_OK);
    CHECK(api.recorder_set_clip(recorder, (gd_rect){0, 0, 100, 50}) == GD_OK);
    CHECK(api.recorder_is_visible(recorder, (gd_point){0, 0}, &visible) == GD_OK);
    CHECK(visible == 1);
    CHECK(api.recorder_set_quality(recorder, 4, 5, 4, 0, 2) == GD_OK);
    CHECK(api.recorder_fill_rectangle(recorder, brush,
                                      (gd_rect){1, 2, 30, 20}) == GD_OK);
    CHECK(api.recorder_draw_rectangle(recorder, pen,
                                      (gd_rect){1, 2, 30, 20}) == GD_OK);
    CHECK(api.recorder_draw_line(recorder, pen, (gd_point){1, 2},
                                 (gd_point){31, 22}) == GD_OK);
    CHECK(api.recorder_draw_string(recorder, view_of("field \xce\xa9"), font,
                                   brush, (gd_point){4, 7}, format) == GD_OK);
    CHECK(api.recorder_restore(recorder, token) == GD_OK);
    CHECK(api.recorder_restore(recorder, token) == GD_ERROR_INVALID_ARGUMENT);
    CHECK(api.recorder_close(recorder) == GD_OK);
    CHECK(api.recorder_command_count(recorder, &count) == GD_OK && count == 10);

    CHECK(api.recorder_trace(recorder, NULL, 0, &required) ==
          GD_ERROR_BUFFER_TOO_SMALL);
    CHECK(required > 0);
    trace = (char*)malloc((size_t)required + 1U);
    CHECK(trace != NULL);
    CHECK(api.recorder_trace(recorder, trace, required, &required) == GD_OK);
    trace[required] = '\0';
    CHECK(strncmp(trace, "gui.drawing.trace/v1\n", 21) == 0);
    CHECK(strstr(trace, "end|commands=10|closed=1\n") != NULL);
    free(trace);

    CHECK(api.retain(brush) == GD_OK);
    CHECK(api.release(brush) == GD_OK);
    CHECK(api.dispose(brush) == GD_OK);
    CHECK(api.dispose(brush) == GD_OK);
    CHECK(api.object_state(brush, &state) == GD_OK && state == GD_OBJECT_DISPOSED);
    CHECK(api.recorder_fill_rectangle(recorder, brush,
                                      (gd_rect){0, 0, 1, 1}) == GD_ERROR_DISPOSED);
    CHECK(api.pen_set_width(brush, 1.0) == GD_ERROR_WRONG_HANDLE_KIND);
    stale_brush = brush;

    CHECK(api.graphics_path_create(1, &path) == GD_OK);
    CHECK(api.graphics_path_start_figure(path) == GD_OK);
    CHECK(api.graphics_path_add_line(path, (gd_point){2, 1},
                                     (gd_point){2, 9}) == GD_OK);
    CHECK(api.graphics_path_add_rectangle(path, (gd_rect){-1, -2, 4, 5}) == GD_OK);
    CHECK(api.graphics_path_add_ellipse(path, (gd_rect){10, 20, 5, 5}) == GD_OK);
    CHECK(api.graphics_path_add_arc(path, (gd_rect){20, 10, 8, 6}, 0, 90) == GD_OK);
    CHECK(api.graphics_path_close_figure(path) == GD_OK);
    CHECK(api.graphics_path_bounds(path, &path_bounds) == GD_OK);
    CHECK(path_bounds.x == -1 && path_bounds.y == -2 &&
          path_bounds.width == 29 && path_bounds.height == 27);
    CHECK(api.graphics_path_is_visible(path, (gd_point){0, 0}, &visible) == GD_OK &&
          visible == 1);
    CHECK(api.graphics_path_points(path, NULL, 0, &path_point_count) ==
          GD_ERROR_BUFFER_TOO_SMALL);
    CHECK(path_point_count == 23);
    path_points = (gd_point*)calloc((size_t)path_point_count, sizeof(gd_point));
    CHECK(path_points != NULL);
    CHECK(api.graphics_path_points(path, path_points, path_point_count,
                                   &path_point_count) == GD_OK);
    free(path_points);
    CHECK(api.graphics_path_clone(path, &path_clone) == GD_OK);
    CHECK(api.graphics_path_transform(path_clone,
                                      (gd_matrix){1, 0, 0, 1, 4, 5}) == GD_OK);
    CHECK(api.graphics_path_add_path(path_clone, path, 1) == GD_OK);
    CHECK(api.region_create_rectangle((gd_rect){0, 0, 10, 10}, &region) == GD_OK);
    CHECK(api.region_exclude_rectangle(region, (gd_rect){2, 2, 4, 4}) == GD_OK);
    CHECK(api.region_union_path(region, path) == GD_OK);
    CHECK(api.region_is_visible(region, (gd_point){1, 1}, &visible) == GD_OK &&
          visible == 1);
    CHECK(api.region_is_visible(region, (gd_point){3, 3}, &visible) == GD_OK &&
          visible == 0);
    CHECK(api.region_bounds(region, &path_bounds) == GD_OK &&
          path_bounds.x == -1 && path_bounds.y == -2 &&
          path_bounds.width == 29 && path_bounds.height == 27);
    CHECK(api.image_reference_create(42, 640, 480, 0, 7, &image) == GD_OK);
    CHECK(api.image_attributes_create(&attributes) == GD_OK);
    color_matrix[0] = color_matrix[6] = color_matrix[12] =
        color_matrix[18] = color_matrix[24] = 1.0;
    color_matrix[18] = 0.5;
    CHECK(api.image_attributes_set_color_matrix(attributes, color_matrix, 25) == GD_OK);
    {
        gd_color_remap remap = {
            {UINT32_C(0xffff0000), 0}, {UINT32_C(0xff0000ff), 0}};
        CHECK(api.image_attributes_set_remap_table(attributes, &remap, 1) == GD_OK);
    }
    CHECK(api.bitmap_create(2, 2, GD_PIXEL_BGRA32_PREMULTIPLIED,
                            &bitmap) == GD_OK);
    CHECK(api.bitmap_set_pixel(bitmap, 0, 0,
                               (gd_color){UINT32_C(0xffff0000), 0}) == GD_OK);
    CHECK(api.bitmap_get_pixel(bitmap, 0, 0, &pixel) == GD_OK);
    CHECK(pixel.argb == UINT32_C(0xffff0000) && pixel.is_empty == 0);
#if !defined(_WIN32)
    {
        uintptr_t native_bitmap = UINTPTR_MAX;
        CHECK(api.bitmap_export_hbitmap(
                  bitmap, (gd_color){UINT32_C(0xffffffff), 0}, &native_bitmap) ==
              GD_ERROR_NOT_AVAILABLE);
        CHECK(native_bitmap == 0);
        CHECK(api.bitmap_import_hbitmap(1, &bitmap_clone) ==
              GD_ERROR_NOT_AVAILABLE);
        CHECK(bitmap_clone.slot == 0 && bitmap_clone.generation == 0);
        {
            gd_rect surface_bounds = {0};
            CHECK(api.native_surface_capture(
                      1, GD_NATIVE_SURFACE_HDC, &bitmap_clone, &surface_bounds) ==
                  GD_ERROR_NOT_AVAILABLE);
            CHECK(api.native_surface_present(
                      1, GD_NATIVE_SURFACE_HDC, bitmap) ==
                  GD_ERROR_NOT_AVAILABLE);
            {
                uintptr_t leased_hdc = 0;
                uint64_t lease_token = 0;
                CHECK(api.bitmap_acquire_hdc(bitmap, &leased_hdc, &lease_token) ==
                      GD_ERROR_NOT_AVAILABLE);
                CHECK(leased_hdc == 0 && lease_token == 0);
                CHECK(api.bitmap_release_hdc(bitmap, 1) ==
                      GD_ERROR_NOT_AVAILABLE);
            }
        }
    }
#endif
    CHECK(api.bitmap_dimensions(bitmap, &bitmap_width, &bitmap_height,
                                &bitmap_format, &bitmap_generation) == GD_OK);
    CHECK(bitmap_width == 2 && bitmap_height == 2 &&
          bitmap_format == GD_PIXEL_BGRA32_PREMULTIPLIED &&
          bitmap_generation == 2);
    CHECK(api.bitmap_lock(bitmap, GD_BITMAP_LOCK_READ, &bitmap_lock) == GD_OK);
    CHECK(bitmap_lock.data != NULL && bitmap_lock.writable_data == NULL &&
          bitmap_lock.row_bytes == 8);
    CHECK(api.bitmap_unlock(bitmap, bitmap_lock.token) == GD_OK);
    CHECK(api.bitmap_unlock(bitmap, bitmap_lock.token) == GD_ERROR_INVALID_ARGUMENT);
    CHECK(api.bitmap_lock(bitmap, GD_BITMAP_LOCK_WRITE, &bitmap_lock) == GD_OK);
    CHECK(bitmap_lock.writable_data != NULL);
    /* A-gf-abi-01-F12: dispose waits for the lock to be returned. */
    CHECK(api.dispose(bitmap) == GD_ERROR_INVALID_STATE);
    ((uint8_t*)bitmap_lock.writable_data)[0] = 255;
    ((uint8_t*)bitmap_lock.writable_data)[1] = 0;
    ((uint8_t*)bitmap_lock.writable_data)[2] = 0;
    ((uint8_t*)bitmap_lock.writable_data)[3] = 255;
    CHECK(api.bitmap_unlock(bitmap, bitmap_lock.token) == GD_OK);
    CHECK(api.bitmap_get_pixel(bitmap, 0, 0, &pixel) == GD_OK &&
          pixel.argb == UINT32_C(0xff0000ff));
    CHECK(api.bitmap_dimensions(bitmap, &bitmap_width, &bitmap_height,
                                &bitmap_format, &bitmap_generation) == GD_OK &&
          bitmap_generation == 3);
    CHECK(api.bitmap_edit_begin(bitmap, (gd_rect_i){1, 1, 1, 1},
                                &bitmap_edit) == GD_OK);
    CHECK(bitmap_edit.writable_data != NULL && bitmap_edit.row_bytes == 8 &&
          bitmap_edit.bounds.x == 1 && bitmap_edit.bounds.y == 1);
    CHECK(api.dispose(bitmap) == GD_ERROR_INVALID_STATE);
    ((uint8_t*)bitmap_edit.writable_data)[0] = 0;
    ((uint8_t*)bitmap_edit.writable_data)[1] = 255;
    ((uint8_t*)bitmap_edit.writable_data)[2] = 0;
    ((uint8_t*)bitmap_edit.writable_data)[3] = 255;
    CHECK(api.bitmap_edit_commit(bitmap, bitmap_edit.token,
                                 &bitmap_generation) == GD_OK &&
          bitmap_generation == 4);
    CHECK(api.bitmap_changes_since(bitmap, 3, NULL, 0, &bitmap_damage_count,
                                   &damage_summary) == GD_ERROR_BUFFER_TOO_SMALL);
    CHECK(bitmap_damage_count == 1 && damage_summary.from_generation == 3 &&
          damage_summary.to_generation == 4 &&
          damage_summary.history_complete == 1);
    CHECK(api.bitmap_changes_since(bitmap, 3, bitmap_damage, 2,
                                   &bitmap_damage_count,
                                   &damage_summary) == GD_OK);
    CHECK(bitmap_damage[0].x == 1 && bitmap_damage[0].y == 1 &&
          bitmap_damage[0].width == 1 && bitmap_damage[0].height == 1);
    CHECK(api.bitmap_edit_begin(bitmap, (gd_rect_i){1, 1, 1, 1},
                                &bitmap_edit) == GD_OK);
    ((uint8_t*)bitmap_edit.writable_data)[1] = 0;
    CHECK(api.bitmap_edit_cancel(bitmap, bitmap_edit.token) == GD_OK);
    CHECK(api.bitmap_get_pixel(bitmap, 1, 1, &pixel) == GD_OK &&
          pixel.argb == UINT32_C(0xff00ff00));
    CHECK(api.bitmap_changes_since(bitmap, 4, NULL, 0, &bitmap_damage_count,
                                   &damage_summary) == GD_OK &&
          bitmap_damage_count == 0 && damage_summary.to_generation == 4);
    CHECK(api.bitmap_clone(bitmap, (gd_rect_i){0, 0, 1, 1},
                           &bitmap_clone) == GD_OK);
    CHECK(api.bitmap_thumbnail(bitmap, 1, 1, &bitmap_thumbnail) == GD_OK);
    CHECK(api.bitmap_adjusted(bitmap, attributes, &bitmap_adjusted) == GD_OK);
    CHECK(api.recorder_create(&extended_recorder) == GD_OK);
    CHECK(api.recorder_draw_ellipse(extended_recorder, pen,
                                    (gd_rect){1, 2, 3, 4}) == GD_OK);
    CHECK(api.recorder_fill_ellipse(extended_recorder, brush,
                                    (gd_rect){1, 2, 3, 4}) == GD_ERROR_DISPOSED);
    CHECK(api.release(stale_brush) == GD_OK);
    CHECK(api.release(stale_brush) == GD_ERROR_STALE_HANDLE);
    CHECK(api.solid_brush_create((gd_color){UINT32_C(0xffff0000), 0},
                                 &brush) == GD_OK);
    CHECK(api.hatch_brush_create(GD_HATCH_DIAGONAL_CROSS,
                                 (gd_color){UINT32_C(0xffffffff), 0},
                                 (gd_color){UINT32_C(0xff000000), 0},
                                 &hatch) == GD_OK);
    CHECK(api.linear_gradient_brush_create(
              (gd_rect){0, 0, 20, 10},
              (gd_color){UINT32_C(0xffff0000), 0},
              (gd_color){UINT32_C(0xff0000ff), 0}, 0, GD_WRAP_CLAMP,
              &linear_gradient) == GD_OK);
    {
        const gd_color colors[3] = {
            {UINT32_C(0xffff0000), 0}, {UINT32_C(0xffffffff), 0},
            {UINT32_C(0xff0000ff), 0}};
        const double positions[3] = {0, 0.5, 1};
        CHECK(api.linear_gradient_set_interpolation(
                  linear_gradient, colors, positions, 3) == GD_OK);
    }
    CHECK(api.path_gradient_brush_create(gradient_boundary, 4, GD_WRAP_CLAMP,
                                         &path_gradient) == GD_OK);
    CHECK(api.path_gradient_set_center_color(
              path_gradient, (gd_color){UINT32_C(0xffffffff), 0}) == GD_OK);
    CHECK(api.recorder_fill_ellipse(extended_recorder, brush,
                                    (gd_rect){1, 2, 3, 4}) == GD_OK);
    CHECK(api.recorder_fill_polygon(extended_recorder, brush, polygon, 3, 1) == GD_OK);
    CHECK(api.recorder_draw_path(extended_recorder, pen, path) == GD_OK);
    CHECK(api.recorder_fill_path(extended_recorder, brush, path) == GD_OK);
    CHECK(api.recorder_fill_rectangle(extended_recorder, hatch,
                                      (gd_rect){0, 0, 8, 8}) == GD_OK);
    CHECK(api.recorder_fill_rectangle(extended_recorder, linear_gradient,
                                      (gd_rect){8, 0, 8, 8}) == GD_OK);
    CHECK(api.recorder_fill_rectangle(extended_recorder, path_gradient,
                                      (gd_rect){16, 0, 8, 8}) == GD_OK);
    CHECK(api.recorder_draw_image(extended_recorder, image,
                                  (gd_rect){0, 0, 320, 240},
                                  (gd_rect){0, 0, 640, 480}, attributes) == GD_OK);
    CHECK(api.recorder_draw_bitmap(extended_recorder, bitmap,
                                   (gd_rect){0, 0, 2, 2},
                                   (gd_rect){0, 0, 2, 2}, attributes) == GD_OK);
    CHECK(api.recorder_close(extended_recorder) == GD_OK);
    CHECK(api.recorder_command_count(extended_recorder, &count) == GD_OK && count == 10);

    CHECK(check_drawing_primitives(&api) == 0);
    CHECK(check_gapped_marks(&api) == 0);
    CHECK(check_text_and_reuse(&api) == 0);
    CHECK(check_curved_paths(&api) == 0);
    CHECK(check_path_flattening(&api) == 0);
    CHECK(check_texture_and_radial_brushes(&api) == 0);
    CHECK(check_rotated_text(&api) == 0);
    CHECK(check_seal_errors(&api) == 0);
    CHECK(check_handle_staleness(&api) == 0);
    CHECK(api.release(recorder) == GD_OK);
    CHECK(api.release(recorder) == GD_ERROR_STALE_HANDLE);
    CHECK(api.release(format) == GD_OK);
    CHECK(api.release(font) == GD_OK);
    CHECK(api.release(pen) == GD_OK);
    CHECK(api.release(brush) == GD_OK);
    CHECK(api.release(stale_brush) == GD_ERROR_STALE_HANDLE);
    CHECK(api.release(extended_recorder) == GD_OK);
    CHECK(api.release(attributes) == GD_OK);
    CHECK(api.release(image) == GD_OK);
    CHECK(api.release(path) == GD_OK);
    CHECK(api.release(path_clone) == GD_OK);
    CHECK(api.release(hatch) == GD_OK);
    CHECK(api.release(linear_gradient) == GD_OK);
    CHECK(api.release(path_gradient) == GD_OK);
    CHECK(api.release(region) == GD_OK);
    CHECK(api.release(bitmap_adjusted) == GD_OK);
    CHECK(api.release(bitmap_thumbnail) == GD_OK);
    CHECK(api.release(bitmap_clone) == GD_OK);
    CHECK(api.release(bitmap) == GD_OK);
    return 0;
}
