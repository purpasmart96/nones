#ifndef ARENA_H
#define ARENA_H

typedef struct
{
    uint8_t *heap;
    size_t size;
    size_t capacity;
} Arena;

Arena *ArenaCreate(size_t bytes);
void *ArenaPush(Arena *arena, size_t bytes);
void ArenaDestroy(Arena *arena);

#endif
