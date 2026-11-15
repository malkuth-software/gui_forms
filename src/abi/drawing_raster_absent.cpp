// The raster installer of a program without GUI.Drawing's raster (a C ABI
// library built without the renderer, or a drawing ABI test that does not
// link the raster objects): the table's raster_initialize then fails
// GD_ERROR_NOT_AVAILABLE, and every entry that needs the raster keeps
// failing so. drawing_raster_service.h states the pairing: a program links
// this file or the raster objects (drawing_skia_c_api.cpp), never both.
#include "drawing_raster_service.h"

extern "C" gd_result gui_drawing_raster_module_initialize(void) {
    return GD_ERROR_NOT_AVAILABLE;
}
