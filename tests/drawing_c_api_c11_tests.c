#include "gui_forms/drawing_c_api.h"

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
    CHECK(api.struct_size == sizeof(api));
    CHECK(api.abi_version == GD_ABI_VERSION_0_2);
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
              GD_ERROR_UNSUPPORTED_VERSION);
        CHECK(native_bitmap == 0);
        CHECK(api.bitmap_import_hbitmap(1, &bitmap_clone) ==
              GD_ERROR_UNSUPPORTED_VERSION);
        CHECK(bitmap_clone.slot == 0 && bitmap_clone.generation == 0);
        {
            gd_rect surface_bounds = {0};
            CHECK(api.native_surface_capture(
                      1, GD_NATIVE_SURFACE_HDC, &bitmap_clone, &surface_bounds) ==
                  GD_ERROR_UNSUPPORTED_VERSION);
            CHECK(api.native_surface_present(
                      1, GD_NATIVE_SURFACE_HDC, bitmap) ==
                  GD_ERROR_UNSUPPORTED_VERSION);
            {
                uintptr_t leased_hdc = 0;
                uint64_t lease_token = 0;
                CHECK(api.bitmap_acquire_hdc(bitmap, &leased_hdc, &lease_token) ==
                      GD_ERROR_UNSUPPORTED_VERSION);
                CHECK(leased_hdc == 0 && lease_token == 0);
                CHECK(api.bitmap_release_hdc(bitmap, 1) ==
                      GD_ERROR_UNSUPPORTED_VERSION);
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
