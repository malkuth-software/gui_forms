/* An installed consumer of the drawing ABI, which the C ABI library
   (GUIForms::CAbi) holds: it negotiates ABI 0.4 and records a gapped band, a
   gapped polyline and per-cell colours through the installed header and that
   one shared library. */
#include <gui_forms/drawing_c_api.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

#define CHECK(expression) do { \
    if (!(expression)) { \
        fprintf(stderr, "drawing_abi_consumer:%d: %s\n", __LINE__, #expression); \
        return 1; \
    } \
} while (0)

int main(void) {
    gd_api_v0 api;
    gd_handle recorder = {0};
    gd_handle pen = {0};
    gd_handle brush = {0};
    uint64_t count = 0;
    const gd_point upper[4] = {{0, 2}, {1, 3}, {2, NAN}, {3, 2}};
    const gd_point lower[4] = {{0, 0}, {1, 0}, {2, 0}, {3, 0}};
    const gd_rect cells[2] = {{0, 0, 1, 1}, {1, 0, 1, 1}};
    const gd_color colors[2] = {{UINT32_C(0xffff0000), 0}, {UINT32_C(0xff0000ff), 0}};

    memset(&api, 0, sizeof(api));
    api.struct_size = sizeof(api);
    CHECK(gd_get_api_v0(GD_ABI_VERSION_0_4, &api) == GD_OK);
    CHECK(api.struct_size == sizeof(api));
    CHECK(api.recorder_create(&recorder) == GD_OK);
    CHECK(api.pen_create((gd_color){UINT32_C(0xff000000), 0}, 1.0, &pen) == GD_OK);
    CHECK(api.solid_brush_create((gd_color){UINT32_C(0x800000ff), 0}, &brush) == GD_OK);
    /* One band run (indices 0 and 1), one polyline run, two colours. */
    CHECK(api.recorder_fill_band(recorder, brush, upper, lower, 4) == GD_OK);
    CHECK(api.recorder_draw_polyline_runs(recorder, pen, upper, 4) == GD_OK);
    CHECK(api.recorder_fill_rectangles_each(recorder, cells, colors, 2) == GD_OK);
    CHECK(api.recorder_command_count(recorder, &count) == GD_OK && count == 4U);
    CHECK(api.release(brush) == GD_OK);
    CHECK(api.release(pen) == GD_OK);
    CHECK(api.release(recorder) == GD_OK);
    return 0;
}
