#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdalign.h>

#include "arena.h"

static const size_t ALIGN = alignof(max_align_t);

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
    size_t padded_size = (bytes + ALIGN - 1) & ~(ALIGN - 1);
    if ((arena->size + padded_size) > arena->capacity)
        return NULL;

    void *ptr = &arena->heap[arena->size];
    printf("Adding a %zd byte block to the arena\n", padded_size);
    arena->size += padded_size;

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