#include "ash/app/queue.h"

#include <string.h>

void ash_queue_init(ash_queue *q, ash_arena *arena)
{
    q->arena = arena;
    q->items = NULL;
    q->head = 0;
    q->tail = 0;
    q->cap = 0;
}

static void compact_empty(ash_queue *q)
{
    q->head = 0;
    q->tail = 0;
    q->items = NULL;
    q->cap = 0;
    if (q->arena != NULL)
        ash_arena_reset(q->arena);
}

void ash_queue_push(ash_queue *q, const char *text, size_t len)
{
    if (q->head == q->tail)
        compact_empty(q);

    if (q->tail == q->cap) {
        size_t nc = q->cap ? q->cap * 2 : 8;
        ash_queue_item *ni = ash_array(q->arena, ash_queue_item, nc);
        size_t live = q->tail - q->head;
        if (live)
            memcpy(ni, q->items + q->head, live * sizeof *ni);
        q->items = ni;
        q->head = 0;
        q->tail = live;
        q->cap = nc;
    }

    char *c = ash_array(q->arena, char, len + 1);
    if (len)
        memcpy(c, text, len);
    c[len] = 0;
    q->items[q->tail].text = c;
    q->items[q->tail].len = len;
    q->tail++;
}

int ash_queue_pop(ash_queue *q, const char **text, size_t *len)
{
    if (q->head == q->tail)
        return 0;
    if (text != NULL)
        *text = q->items[q->head].text;
    if (len != NULL)
        *len = q->items[q->head].len;
    q->head++;
    return 1;
}

size_t ash_queue_count(const ash_queue *q)
{
    return q->tail - q->head;
}

const char *ash_queue_at(const ash_queue *q, size_t i, size_t *len)
{
    if (i >= q->tail - q->head)
        return NULL;
    if (len != NULL)
        *len = q->items[q->head + i].len;
    return q->items[q->head + i].text;
}

void ash_queue_clear(ash_queue *q)
{
    compact_empty(q);
}

static const char PREFIX[] = "> ";

static int draw_text(ash_fb *fb, int x, int y, ash_style st, const char *s,
                     size_t len, int budget)
{
    int col = 0;
    size_t off = 0;
    while (off < len && col < budget) {
        uint32_t cp = 0;
        size_t adv = ash_utf8_decode((const uint8_t *)s + off, len - off, &cp);
        if (adv == 0) {
            off++;
            continue;
        }
        off += adv;
        int cw = ash_char_width(cp);
        if (cw <= 0)
            continue;
        if (col + cw > budget)
            break;
        if (cp == '\n' || cp == '\r' || cp == '\t') {
            ash_fb_put_text(fb, x + col, y, st, " ", 1);
        } else {
            ash_fb_put_text(fb, x + col, y, st, s + off - adv, adv);
        }
        col += cw;
    }
    return col;
}

int ash_queue_render(const ash_queue *q, ash_fb *fb, ash_rect rect,
                     ash_style text_st, ash_style deco_st)
{
    if (rect.w <= 0 || rect.h <= 0)
        return 0;
    size_t n = ash_queue_count(q);
    int plen = (int)(sizeof PREFIX - 1);
    int rows = 0;
    for (size_t i = 0; i < n && rows < rect.h; i++, rows++) {
        int y = rect.y + rows;
        int px = plen < rect.w ? plen : rect.w;
        ash_fb_put_text(fb, rect.x, y, deco_st, PREFIX, (size_t)px);
        size_t len = 0;
        const char *s = ash_queue_at(q, i, &len);
        if (s != NULL)
            draw_text(fb, rect.x + plen, y, text_st, s, len, rect.w - plen);
    }
    return rows;
}
