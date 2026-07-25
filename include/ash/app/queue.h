#ifndef ASH_APP_QUEUE_H
#define ASH_APP_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/fb/cell.h"
#include "ash/fb/fb.h"

typedef struct ash_queue_item {
    const char *text;
    size_t      len;
} ash_queue_item;

typedef struct ash_queue {
    ash_arena      *arena;
    ash_queue_item *items;
    size_t          head;
    size_t          tail;
    size_t          cap;
} ash_queue;

ASH_API void   ash_queue_init(ash_queue *q, ash_arena *arena);
ASH_API void   ash_queue_push(ash_queue *q, const char *text, size_t len);
ASH_API bool   ash_queue_pop(ash_queue *q, const char **text, size_t *len);
ASH_API size_t ash_queue_count(const ash_queue *q);
ASH_API const char *ash_queue_at(const ash_queue *q, size_t i, size_t *len);
ASH_API void   ash_queue_clear(ash_queue *q);

ASH_API int ash_queue_render(const ash_queue *q, ash_fb *fb, ash_rect rect,
                             ash_style text_st, ash_style deco_st);

#endif
