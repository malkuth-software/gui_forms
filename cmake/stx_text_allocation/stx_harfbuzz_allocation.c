/*
 * HarfBuzz's allocation functions (its hb_malloc_impl family, selected for
 * the harfbuzz target by cmake/stx_package/CMakeLists.txt): the C heap, with
 * each allocating call counted on the calling thread
 * (stx_text_allocation.h). Linked into the harfbuzz archive itself, so every
 * consumer of it has them.
 */
#include <stdlib.h>

#include "stx_text_allocation.h"

/* HarfBuzz declares these through its hb_*_impl macros; the definitions
 * below take their types from hb.hh's declarations. */
void *stx_text_hb_malloc(size_t size);
void *stx_text_hb_calloc(size_t count, size_t size);
void *stx_text_hb_realloc(void *pointer, size_t size);
void stx_text_hb_free(void *pointer);

static _Thread_local uint64_t harfbuzz_allocations;

void *stx_text_hb_malloc(size_t size)
{
    ++harfbuzz_allocations;
    return malloc(size);
}

void *stx_text_hb_calloc(size_t count, size_t size)
{
    ++harfbuzz_allocations;
    return calloc(count, size);
}

void *stx_text_hb_realloc(void *pointer, size_t size)
{
    ++harfbuzz_allocations;
    return realloc(pointer, size);
}

void stx_text_hb_free(void *pointer)
{
    free(pointer);
}

uint64_t stx_text_harfbuzz_allocations(void)
{
    return harfbuzz_allocations;
}
