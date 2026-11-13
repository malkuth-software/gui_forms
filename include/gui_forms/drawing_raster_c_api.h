#ifndef GUI_FORMS_DRAWING_RASTER_C_API_H
#define GUI_FORMS_DRAWING_RASTER_C_API_H

/*
 * The GUI.Drawing raster module (gui_drawing_raster0): the CPU renderer
 * behind the drawing ABI's recorder_execute, bitmap PNG coding, measured
 * text and font resolution. It is a separate shared library so that the
 * drawing ABI (gui_drawing_abi0) stays renderer-free.
 */

#include "gui_forms/drawing_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(GDR_C_API_BUILD)
#define GDR_C_API_EXPORT __declspec(dllexport)
#else
#define GDR_C_API_EXPORT __declspec(dllimport)
#endif
#elif defined(__GNUC__) || defined(__clang__)
#define GDR_C_API_EXPORT __attribute__((visibility("default")))
#else
#define GDR_C_API_EXPORT
#endif

/*
 * Installs the raster module as the drawing ABI's raster service (version
 * 0.1 of gd_raster_service_v0) and registers the faces packaged beside the
 * module (its fonts directory). Call it once per process, before
 * executing a recorder into a bitmap or measuring text with a raster
 * service.
 *
 * Returns GD_OK; GD_ERROR_UNSUPPORTED_VERSION when the drawing ABI has no
 * raster service entry; or the drawing ABI's own result when it refuses
 * the service, in which case no faces are registered.
 */
GDR_C_API_EXPORT gd_result gdr_initialize(void);

#ifdef __cplusplus
}
#endif

#endif
