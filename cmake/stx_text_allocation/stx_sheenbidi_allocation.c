/*
 * SheenBidi 2.9's unity build with its two heap calls (Source/Object.c's
 * malloc and free) renamed to the arena and counters of
 * stx_text_allocation.h. SheenBidi's sources are compiled unmodified: this
 * unit defines the replacements, renames the two calls with macros after
 * <stdlib.h> has been read (so its declarations keep their names), and
 * includes SheenBidi.c, which includes every source (SB_CONFIG_UNITY). Every
 * standard header SheenBidi includes is read first, so that no declaration
 * in one is renamed.
 */
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "stx_text_allocation.h"

/* Each block's header: its rounded size (the low bit set once it is
 * released) and the offset of the block beneath it. 16 bytes, so blocks
 * stay aligned to STX_SHEENBIDI_ARENA_ALIGNMENT. */
typedef struct ArenaHeader {
    size_t size;
    size_t previous;
} ArenaHeader;

#define ARENA_HEADER_BYTES 16U
#define ARENA_RELEASED 1U

_Static_assert(sizeof(ArenaHeader) <= ARENA_HEADER_BYTES, "the arena header fits its 16 bytes");

static _Thread_local StxSheenBidiArena *installed_arena;
static _Thread_local uint64_t heap_allocations;
static _Thread_local uint64_t arena_allocations;

static ArenaHeader *arena_header(StxSheenBidiArena *arena, size_t offset)
{
    return (ArenaHeader *)(void *)(arena->base + offset);
}

static int arena_holds(const StxSheenBidiArena *arena, const void *pointer)
{
    const uintptr_t address = (uintptr_t)pointer;
    const uintptr_t begin = (uintptr_t)arena->base;
    return address >= begin && address - begin < arena->capacity;
}

static void *stx_sheenbidi_allocate(size_t size)
{
    StxSheenBidiArena *const arena = installed_arena;
    if (arena != NULL && arena->base != NULL && size <= arena->capacity) {
        const size_t alignment = STX_SHEENBIDI_ARENA_ALIGNMENT;
        const size_t rounded = (size + (alignment - 1U)) & ~(size_t)(alignment - 1U);
        const size_t available = arena->capacity - arena->used;
        if (rounded >= size && available >= ARENA_HEADER_BYTES && available - ARENA_HEADER_BYTES >= rounded) {
            const size_t offset = arena->used;
            ArenaHeader *const header = arena_header(arena, offset);
            header->size = rounded;
            header->previous = arena->top;
            arena->top = offset;
            arena->used = offset + ARENA_HEADER_BYTES + rounded;
            if (arena->used > arena->peak) arena->peak = arena->used;
            ++arena_allocations;
            return arena->base + offset + ARENA_HEADER_BYTES;
        }
    }
    ++heap_allocations;
    return malloc(size);
}

static void stx_sheenbidi_release(void *pointer)
{
    StxSheenBidiArena *const arena = installed_arena;
    if (pointer == NULL) return;
    if (arena == NULL || arena->base == NULL || !arena_holds(arena, pointer)) {
        free(pointer);
        return;
    }
    {
        const size_t offset = (size_t)((unsigned char *)pointer - arena->base) - ARENA_HEADER_BYTES;
        ArenaHeader *const released = arena_header(arena, offset);
        released->size |= ARENA_RELEASED;
        /* Give back the newest blocks while they are released. */
        while (arena->top != SIZE_MAX && (arena_header(arena, arena->top)->size & ARENA_RELEASED) != 0U) {
            const size_t top = arena->top;
            arena->top = arena_header(arena, top)->previous;
            arena->used = top;
        }
    }
}

StxSheenBidiArena *stx_sheenbidi_use_arena(StxSheenBidiArena *arena)
{
    StxSheenBidiArena *const previous = installed_arena;
    if (arena != NULL) {
        arena->used = 0U;
        arena->top = SIZE_MAX;
        arena->peak = 0U;
    }
    installed_arena = arena;
    return previous;
}

uint64_t stx_text_sheenbidi_heap_allocations(void)
{
    return heap_allocations;
}

uint64_t stx_text_sheenbidi_arena_allocations(void)
{
    return arena_allocations;
}

#define malloc stx_sheenbidi_allocate
#define free stx_sheenbidi_release

#include "SheenBidi.c"
