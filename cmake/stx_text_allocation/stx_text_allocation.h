/*
 * Allocation hooks of the text stack the stx package builds
 * (cmake/stx_package/CMakeLists.txt): HarfBuzz's heap calls, counted per
 * thread. Its sources are compiled unmodified; the package routes its
 * allocation calls here through its hb_malloc_impl family.
 *
 * Counts are of calls on the calling thread since it started, never reset:
 * read one before and after a piece of work and subtract.
 */
#ifndef STX_TEXT_ALLOCATION_H
#define STX_TEXT_ALLOCATION_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* HarfBuzz's hb_malloc, hb_calloc and hb_realloc calls on this thread (each
 * one that may reach the C heap, a realloc counted whether or not it moved
 * the block). Its frees are not counted. */
uint64_t stx_text_harfbuzz_allocations(void);

#ifdef __cplusplus
}
#endif

#endif
