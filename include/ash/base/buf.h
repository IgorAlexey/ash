#ifndef ASH_BASE_BUF_H
#define ASH_BASE_BUF_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"

typedef struct ash_buf {
    ash_arena     *arena;
    unsigned char *data;
    size_t         len;
    size_t         cap;
} ash_buf;

ASH_API void ash_buf_init(ash_buf *b, ash_arena *a);
ASH_API void ash_buf_reserve(ash_buf *b, size_t extra);
ASH_API void ash_buf_append(ash_buf *b, const void *p, size_t n);
ASH_API void ash_buf_append_byte(ash_buf *b, unsigned char c);
ASH_API void ash_buf_append_cstr(ash_buf *b, const char *s);

#endif
