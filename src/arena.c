#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

#include "arena.h"
#include "utils.h"

static const size_t ALIGN = alignof(max_align_t);

Arena *ArenaCreate(size_t bytes)
{
    Arena *arena = malloc(sizeof(*arena));
    arena->heap = malloc(bytes);
    arena->size = 0;
    arena->capacity = bytes;

    return arena;
}

void *ArenaPush(Arena *arena, size_t bytes)
{
    size_t padded_size = (bytes + ALIGN - 1) & ~(ALIGN - 1);
    assert((arena->size + padded_size) < arena->capacity);

    void *ptr = &arena->heap[arena->size];
    DEBUG_LOG("Adding a %zd byte block to the arena\n", padded_size);

    arena->size += padded_size;

    return ptr;
}

void ArenaClear(Arena *arena)
{
    arena->size = 0;
}

void ArenaDestroy(Arena *arena)
{
    free(arena->heap);
    free(arena);
}
