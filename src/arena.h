#ifndef ARENA_H
#define ARENA_H

typedef struct
{
    void **heap;
    size_t size;
    size_t capacity;
} Arena;

Arena *arena_create(size_t bytes);
void *arena_add(Arena *arena, size_t bytes);
void arena_destroy(Arena *arena);

#endif
