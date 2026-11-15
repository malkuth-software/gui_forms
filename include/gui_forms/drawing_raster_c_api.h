#ifndef GUI_FORMS_DRAWING_RASTER_C_API_H
#define GUI_FORMS_DRAWING_RASTER_C_API_H

/*
 * The GUI.Drawing raster service: the CPU renderer behind the drawing ABI's
 * recorder_execute, bitmap PNG coding, measured text and font resolution.
 * It is part of the C ABI library (gui_forms_abi), which holds the drawing
 * ABI and the engine's one renderer; the drawing ABI's own objects stay
 * renderer-free and reach the renderer only through this service.
 */

#include "gui_forms/drawing_c_api.h"

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32)
#if defined(GDR_C_API_BUILD)
#define GDR_C_API_EXPORT __declspec(dllexport)
#elif defined(GDR_C_API_STATIC)
#define GDR_C_API_EXPORT
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
