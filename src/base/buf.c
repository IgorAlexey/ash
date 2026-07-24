#include <string.h>

#include "ash/base/buf.h"
#include "ash/base/poison.h"

void ash_buf_init(ash_buf *b, ash_arena *a)
{
    b->arena = a;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
}

void ash_buf_reserve(ash_buf *b, size_t extra)
{
    size_t need = b->len + extra;
    if (need < b->len)
        ash_die("buf overflow: len %zu + %zu", b->len, extra);
    if (need <= b->cap)
        return;

    size_t want = b->cap ? b->cap : 64;
    while (want < need) {
        size_t next = want * 2;
        if (next < want) {
            want = need;
            break;
        }
        want = next;
    }

    unsigned char *nd = ash_arena_alloc(b->arena, want, 16);
    if (b->len)
        memcpy(nd, b->data, b->len);
    b->data = nd;
    b->cap = want;
}

void ash_buf_append(ash_buf *b, const void *p, size_t n)
{
    if (n == 0)
        return;
    ash_buf_reserve(b, n);
    memcpy(b->data + b->len, p, n);
    b->len += n;
}

void ash_buf_append_byte(ash_buf *b, unsigned char c)
{
    ash_buf_reserve(b, 1);
    b->data[b->len++] = c;
}

void ash_buf_append_cstr(ash_buf *b, const char *s)
{
    ash_buf_append(b, s, strlen(s));
}
