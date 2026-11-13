/*
 * Allocation hooks of the text stack the stx package builds
 * (cmake/stx_package/CMakeLists.txt): HarfBuzz's and SheenBidi's heap calls,
 * counted per thread, and an arena that serves SheenBidi's allocations from
 * memory a caller owns. The sources of both libraries are compiled
 * unmodified; the package routes their allocation calls here (HarfBuzz
 * through its hb_malloc_impl family, SheenBidi through a wrapper unit that
 * renames its malloc and free).
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

/* SheenBidi's allocations on this thread that reached the C heap: every one
 * when no arena is installed, and those an installed arena had no room for. */
uint64_t stx_text_sheenbidi_heap_allocations(void);

/* SheenBidi's allocations on this thread that an installed arena served. */
uint64_t stx_text_sheenbidi_arena_allocations(void);

/* The alignment of the arena's base and of every block it serves. */
#define STX_SHEENBIDI_ARENA_ALIGNMENT 16U

/* Memory a caller owns that serves SheenBidi's allocations on one thread
 * while it is installed. `base` (aligned to STX_SHEENBIDI_ARENA_ALIGNMENT)
 * holds `capacity` bytes; the other members are the arena's own state, set
 * by stx_sheenbidi_use_arena. Each block carries a 16-byte header before it;
 * releasing the newest live block gives its bytes back, with every released
 * block beneath it, so SheenBidi's create-then-release order keeps the arena
 * from growing across paragraphs and calls. A request it has no room for
 * goes to the C heap (and is released there). */
typedef struct StxSheenBidiArena {
    unsigned char *base;
    size_t capacity;
    /* Bytes in use from base, headers included. */
    size_t used;
    /* The offset of the newest block's header, or SIZE_MAX for none. */
    size_t top;
    /* The most bytes in use since the arena was installed. */
    size_t peak;
} StxSheenBidiArena;

/* Installs `arena` for this thread's SheenBidi allocations and returns the
 * arena installed before (NULL for none); NULL restores the C heap. An
 * arena being installed starts empty (used, top and peak reset), so every
 * SheenBidi object allocated from an arena must be released before that
 * arena is installed again or uninstalled, and none may be released while
 * another arena is installed. */
StxSheenBidiArena *stx_sheenbidi_use_arena(StxSheenBidiArena *arena);

#ifdef __cplusplus
}
#endif

#endif
