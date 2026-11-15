/* The SDK's C ABI library as a C program sees it: the GUI.Forms tables and
 * GUI.Drawing's table with its raster seam (raster_initialize), both from the
 * one shared library (tests/sdk_consumer/CMakeLists.txt). */
#include "gui_forms/c/gf_abi.h"
#include "gui_forms/drawing_c_api.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    gf_objects_table objects;
    memset(&objects, 0, sizeof(objects));
    if (gf_abi_get_table(GF_TABLE_OBJECTS, GF_ABI_MAJOR, GF_ABI_MINOR, &objects, sizeof(objects)) != GF_STATUS_OK) {
        fprintf(stderr, "sdk_c_abi_consumer: the objects table did not load\n");
        return 1;
    }
    gd_api_v0 drawing;
    memset(&drawing, 0, sizeof(drawing));
    drawing.struct_size = (uint32_t)sizeof(drawing);
    if (gd_get_api_v0(GD_ABI_VERSION_1_1, &drawing) != GD_OK || drawing.raster_initialize == NULL) {
        fprintf(stderr, "sdk_c_abi_consumer: the drawing table did not load at 1.1\n");
        return 1;
    }
    if (drawing.raster_initialize() != GD_OK) {
        fprintf(stderr, "sdk_c_abi_consumer: the raster did not install\n");
        return 1;
    }
    printf("sdk_c_abi_consumer: ok\n");
    return 0;
}
