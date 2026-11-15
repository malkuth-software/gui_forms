#ifndef GUI_FORMS_DRAWING_RASTER_SERVICE_H
#define GUI_FORMS_DRAWING_RASTER_SERVICE_H

/*
 * The raster module's attachment to the gd_* library (raster_service_install),
 * private to the two and not part of the 1.0 ABI (seal decision 3): the
 * callbacks receive GUI.Drawing C++ object addresses, so only a raster module
 * built with this library from the same sources installs it. The handle
 * registry validates kind, lifetime, owner thread and disposal before it
 * passes an object. The public header declares the type only, so the table's
 * slot keeps its place; the table's raster_initialize, through
 * gui_drawing_raster_module_initialize below, is how a program attaches the
 * raster.
 *
 * abi_version stays GD_ABI_VERSION_0_1 and struct_size is this record's size
 * as the installer declares it; resolve_font was added (stage F) within the
 * same build pairing, which is the only compatibility this record keeps.
 */

#include "gui_forms/drawing_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gd_raster_service_v0 {
    uint32_t struct_size;
    uint32_t abi_version;
    gd_result (*execute)(const void* recorder, void* bitmap,
                         uint64_t first_command,
                         uint64_t* commands_executed);
    gd_result (*encode_png)(const void* bitmap, void* buffer,
                            uint64_t capacity, uint64_t* required_size);
    gd_result (*decode_png)(const void* data, uint64_t size,
                            void** bitmap);
    gd_result (*measure_string)(const void* font, const void* format,
                                gd_string_view text, double layout_width,
                                gd_size* measured);
    /* Stage F: resolves and registers the font's face on the calling thread. */
    gd_result (*resolve_font)(const void* font, gd_font_resolution* resolution);
};

/*
 * The raster's installer, behind the table's raster_initialize: installs
 * the raster service and registers the packaged faces on the calling
 * thread. Defined by the raster objects (drawing_skia_c_api.cpp) or, in a
 * build or test program without the renderer, by drawing_raster_absent.cpp,
 * which returns GD_ERROR_NOT_AVAILABLE. Exactly one of the two is linked
 * into a program. Not exported: hidden, and absent from every export list.
 */
gd_result gui_drawing_raster_module_initialize(void);

#ifdef __cplusplus
}
#endif

#endif
