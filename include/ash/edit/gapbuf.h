#ifndef ASH_EDIT_GAPBUF_H
#define ASH_EDIT_GAPBUF_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"

typedef struct ash_gapbuf {
    ash_arena *arena;
    uint8_t   *data;
    size_t     cap;
    size_t     gap_off;
    size_t     gap_len;
    uint32_t   generation;
} ash_gapbuf;

ASH_API void     ash_gapbuf_init(ash_gapbuf *b, ash_arena *arena);
ASH_API void     ash_gapbuf_clear(ash_gapbuf *b);
ASH_API size_t   ash_gapbuf_len(const ash_gapbuf *b);
ASH_API uint32_t ash_gapbuf_generation(const ash_gapbuf *b);

ASH_API void ash_gapbuf_replace(ash_gapbuf *b, size_t start, size_t end,
                                const void *src, size_t n);
ASH_API void ash_gapbuf_insert(ash_gapbuf *b, size_t off, const void *src,
                               size_t n);
ASH_API void ash_gapbuf_erase(ash_gapbuf *b, size_t start, size_t end);
ASH_API void ash_gapbuf_set(ash_gapbuf *b, const void *src, size_t n);

ASH_API const uint8_t *ash_gapbuf_read_forward(const ash_gapbuf *b, size_t off,
                                               size_t *len);
ASH_API const uint8_t *ash_gapbuf_read_backward(const ash_gapbuf *b, size_t off,
                                                size_t *len);
ASH_API size_t ash_gapbuf_copy(const ash_gapbuf *b, size_t off, void *dst,
                               size_t n);
ASH_API uint8_t ash_gapbuf_at(const ash_gapbuf *b, size_t off);
ASH_API void    ash_gapbuf_extract(const ash_gapbuf *b, ash_buf *out);

#endif
