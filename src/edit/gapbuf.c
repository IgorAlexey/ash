#include "ash/edit/gapbuf.h"

#include <string.h>

#define GAP_CHUNK   16u
#define ALLOC_CHUNK 256u

static size_t text_length(const ash_gapbuf *b)
{
    return b->cap - b->gap_len;
}

void ash_gapbuf_init(ash_gapbuf *b, ash_arena *arena)
{
    b->arena = arena;
    b->data = NULL;
    b->cap = 0;
    b->gap_off = 0;
    b->gap_len = 0;
    b->generation = 0;
}

void ash_gapbuf_clear(ash_gapbuf *b)
{
    b->gap_off = 0;
    b->gap_len = b->cap;
    b->generation++;
}

size_t ash_gapbuf_len(const ash_gapbuf *b)
{
    return text_length(b);
}

uint32_t ash_gapbuf_generation(const ash_gapbuf *b)
{
    return b->generation;
}

static void move_gap(ash_gapbuf *b, size_t off)
{
    if (off == b->gap_off)
        return;
    if (off < b->gap_off) {
        memmove(b->data + off + b->gap_len, b->data + off, b->gap_off - off);
    } else {
        memmove(b->data + b->gap_off, b->data + b->gap_off + b->gap_len,
                off - b->gap_off);
    }
    b->gap_off = off;
}

static void enlarge_gap(ash_gapbuf *b, size_t need)
{
    if (b->gap_len >= need)
        return;

    size_t tlen = text_length(b);
    size_t new_gap = (need + GAP_CHUNK - 1) & ~(size_t)(GAP_CHUNK - 1);
    if (new_gap < GAP_CHUNK)
        new_gap = GAP_CHUNK;

    size_t new_cap = (tlen + new_gap + ALLOC_CHUNK - 1) & ~(size_t)(ALLOC_CHUNK - 1);
    size_t twice = b->cap * 2;
    if (new_cap < twice)
        new_cap = twice;
    if (new_cap < tlen + new_gap)
        new_cap = tlen + new_gap;

    uint8_t *nd = ash_arena_alloc(b->arena, new_cap, 16);
    size_t right = b->cap - (b->gap_off + b->gap_len);
    if (b->gap_off)
        memcpy(nd, b->data, b->gap_off);
    if (right)
        memcpy(nd + new_cap - right, b->data + b->gap_off + b->gap_len, right);

    b->data = nd;
    b->cap = new_cap;
    b->gap_len = new_cap - tlen;
}

void ash_gapbuf_replace(ash_gapbuf *b, size_t start, size_t end,
                        const void *src, size_t n)
{
    size_t len = text_length(b);
    if (start > len)
        start = len;
    if (end > len)
        end = len;
    if (end < start)
        end = start;

    move_gap(b, start);
    b->gap_len += end - start;
    enlarge_gap(b, n);
    if (n) {
        memcpy(b->data + b->gap_off, src, n);
        b->gap_off += n;
        b->gap_len -= n;
    }
    b->generation++;
}

void ash_gapbuf_insert(ash_gapbuf *b, size_t off, const void *src, size_t n)
{
    ash_gapbuf_replace(b, off, off, src, n);
}

void ash_gapbuf_erase(ash_gapbuf *b, size_t start, size_t end)
{
    ash_gapbuf_replace(b, start, end, NULL, 0);
}

void ash_gapbuf_set(ash_gapbuf *b, const void *src, size_t n)
{
    ash_gapbuf_replace(b, 0, text_length(b), src, n);
}

const uint8_t *ash_gapbuf_read_forward(const ash_gapbuf *b, size_t off,
                                       size_t *len)
{
    size_t tlen = text_length(b);
    if (off > tlen)
        off = tlen;
    if (off < b->gap_off) {
        *len = b->gap_off - off;
        return b->data + off;
    }
    *len = tlen - off;
    return b->data + off + b->gap_len;
}

const uint8_t *ash_gapbuf_read_backward(const ash_gapbuf *b, size_t off,
                                        size_t *len)
{
    size_t tlen = text_length(b);
    if (off > tlen)
        off = tlen;
    if (off <= b->gap_off) {
        *len = off;
        return b->data;
    }
    *len = off - b->gap_off;
    return b->data + b->gap_off + b->gap_len;
}

size_t ash_gapbuf_copy(const ash_gapbuf *b, size_t off, void *dst, size_t n)
{
    uint8_t *out = dst;
    size_t copied = 0;
    while (copied < n) {
        size_t run = 0;
        const uint8_t *p = ash_gapbuf_read_forward(b, off + copied, &run);
        if (run == 0)
            break;
        size_t take = n - copied < run ? n - copied : run;
        memcpy(out + copied, p, take);
        copied += take;
    }
    return copied;
}

uint8_t ash_gapbuf_at(const ash_gapbuf *b, size_t off)
{
    if (off >= text_length(b))
        return 0;
    size_t run = 0;
    const uint8_t *p = ash_gapbuf_read_forward(b, off, &run);
    return run ? p[0] : 0;
}

void ash_gapbuf_extract(const ash_gapbuf *b, ash_buf *out)
{
    size_t tlen = text_length(b);
    size_t off = 0;
    while (off < tlen) {
        size_t run = 0;
        const uint8_t *p = ash_gapbuf_read_forward(b, off, &run);
        if (run == 0)
            break;
        ash_buf_append(out, p, run);
        off += run;
    }
}
