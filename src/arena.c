#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#include "arena.h"

Arena *arena_create(size_t bytes)
{
    Arena *arena = malloc(sizeof(*arena));
    arena->heap = malloc(bytes);
    arena->size = 0;
    arena->capacity = bytes;

    return arena;
}

// TODO: Handle misalignment
void *arena_add(Arena *arena, size_t bytes)
{
    if ((arena->size + bytes) > arena->capacity)
        return NULL;

    void *ptr = &arena->heap[arena->size];
    arena->size += bytes;

    return ptr;
}

void arena_clear(Arena *arena)
{
    arena->size = 0;
}

void arena_destroy(Arena *arena)
{
    free(arena->heap);
    free(arena);
}