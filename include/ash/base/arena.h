#ifndef ASH_BASE_ARENA_H
#define ASH_BASE_ARENA_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/status.h"

typedef struct ash_arena_chunk ash_arena_chunk;

typedef struct ash_arena {
    ash_arena_chunk *head;
    ash_arena_chunk *current;
    size_t           chunk_size;
    size_t           high_water;
    const char      *name;
} ash_arena;

typedef struct ash_arena_mark {
    ash_arena_chunk *chunk;
    size_t           used;
} ash_arena_mark;

typedef struct ash_mem {
    ash_arena session;
    ash_arena turn;
    ash_arena scratch;
    ash_arena stack;
} ash_mem;

ASH_API ASH_WUR ash_status ash_arena_create(ash_arena *a, const char *name,
                                            size_t chunk_size);
ASH_API void               ash_arena_destroy(ash_arena *a);
ASH_API ASH_WUR void      *ash_arena_alloc(ash_arena *a, size_t size, size_t align);
ASH_API ash_arena_mark     ash_arena_mark_get(const ash_arena *a);
ASH_API void               ash_arena_rewind(ash_arena *a, ash_arena_mark m);
ASH_API void               ash_arena_reset(ash_arena *a);
ASH_API size_t             ash_checked_mul(size_t x, size_t y);

ASH_API ASH_WUR ash_status ash_mem_create(ash_mem *m);
ASH_API void               ash_mem_destroy(ash_mem *m);

#define ash_new(a, T)      ((T *)ash_arena_alloc((a), sizeof(T), _Alignof(T)))
#define ash_array(a, T, n) \
    ((T *)ash_arena_alloc((a), ash_checked_mul((n), sizeof(T)), _Alignof(T)))

#endif
