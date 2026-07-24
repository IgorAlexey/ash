#include "ash/edit/measure.h"

#include "ash/fb/fb.h"

#define ZWJ 0x200Du

static size_t decode_at(const ash_gapbuf *b, size_t off, uint32_t *cp)
{
    uint8_t tmp[4];
    size_t n = ash_gapbuf_copy(b, off, tmp, sizeof tmp);
    if (n == 0) {
        *cp = 0;
        return 1;
    }
    size_t adv = ash_utf8_decode(tmp, n, cp);
    return adv ? adv : 1;
}

static size_t prev_cp_start(const ash_gapbuf *b, size_t off)
{
    size_t k = off;
    do {
        k--;
    } while (k > 0 && (ash_gapbuf_at(b, k) & 0xC0u) == 0x80u);
    return k;
}

static int is_regional(uint32_t cp)
{
    return cp >= 0x1F1E6u && cp <= 0x1F1FFu;
}

int ash_measure_next(const ash_gapbuf *b, size_t off, int column, int tab_size,
                     ash_grapheme *g)
{
    size_t len = ash_gapbuf_len(b);
    if (off >= len)
        return 0;

    uint32_t cp;
    size_t adv = decode_at(b, off, &cp);
    g->offset = off;
    g->first_cp = cp;
    size_t next = off + adv;

    if (cp == '\n') {
        g->next = next;
        g->width = 0;
        return 1;
    }
    if (cp == '\t') {
        if (tab_size < 1)
            tab_size = 1;
        g->next = next;
        g->width = tab_size - (column % tab_size);
        return 1;
    }

    int w = ash_char_width(cp);
    if (w < 0)
        w = 0;

    if (is_regional(cp) && next < len) {
        uint32_t c2;
        size_t a2 = decode_at(b, next, &c2);
        if (is_regional(c2)) {
            next += a2;
            w = 2;
        }
    }

    for (;;) {
        if (next >= len)
            break;
        uint32_t c2;
        size_t a2 = decode_at(b, next, &c2);
        if (c2 == ZWJ) {
            next += a2;
            if (next < len) {
                uint32_t c3;
                size_t a3 = decode_at(b, next, &c3);
                if (ash_char_width(c3) == 2)
                    w = 2;
                next += a3;
            }
            continue;
        }
        if (ash_char_width(c2) == 0) {
            next += a2;
            continue;
        }
        break;
    }

    if (w > 2)
        w = 2;
    g->next = next;
    g->width = w;
    return 1;
}

size_t ash_measure_prev(const ash_gapbuf *b, size_t off)
{
    if (off == 0)
        return 0;
    size_t len = ash_gapbuf_len(b);
    if (off > len)
        off = len;

    size_t p = prev_cp_start(b, off);
    for (;;) {
        if (p == 0)
            break;
        uint32_t cp;
        decode_at(b, p, &cp);
        size_t q = prev_cp_start(b, p);
        uint32_t cq;
        decode_at(b, q, &cq);
        if (ash_char_width(cp) == 0) {
            p = q;
            continue;
        }
        if (cq == ZWJ) {
            p = prev_cp_start(b, q);
            continue;
        }
        if (is_regional(cp) && is_regional(cq)) {
            p = q;
            break;
        }
        break;
    }
    return p;
}

size_t ash_measure_line_start(const ash_gapbuf *b, size_t off)
{
    size_t len = ash_gapbuf_len(b);
    if (off > len)
        off = len;
    while (off > 0 && ash_gapbuf_at(b, off - 1) != '\n')
        off--;
    return off;
}

size_t ash_measure_line_end(const ash_gapbuf *b, size_t off)
{
    size_t len = ash_gapbuf_len(b);
    if (off > len)
        off = len;
    while (off < len && ash_gapbuf_at(b, off) != '\n')
        off++;
    return off;
}

int ash_measure_line_index(const ash_gapbuf *b, size_t off)
{
    size_t len = ash_gapbuf_len(b);
    if (off > len)
        off = len;
    int line = 0;
    size_t i = 0;
    while (i < off) {
        size_t run = 0;
        const uint8_t *p = ash_gapbuf_read_forward(b, i, &run);
        if (i + run > off)
            run = off - i;
        for (size_t k = 0; k < run; k++)
            if (p[k] == '\n')
                line++;
        i += run;
    }
    return line;
}

size_t ash_measure_line_at(const ash_gapbuf *b, int line)
{
    if (line <= 0)
        return 0;
    size_t len = ash_gapbuf_len(b);
    int seen = 0;
    size_t i = 0;
    while (i < len) {
        size_t run = 0;
        const uint8_t *p = ash_gapbuf_read_forward(b, i, &run);
        for (size_t k = 0; k < run; k++) {
            if (p[k] == '\n') {
                seen++;
                if (seen == line)
                    return i + k + 1;
            }
        }
        i += run;
    }
    return len;
}

int ash_measure_col(const ash_gapbuf *b, size_t line_start, size_t off,
                    int tab_size)
{
    int col = 0;
    size_t p = line_start;
    ash_grapheme g;
    while (p < off && ash_measure_next(b, p, col, tab_size, &g)) {
        if (g.first_cp == '\n')
            break;
        if (g.next > off)
            break;
        col += g.width;
        p = g.next;
    }
    return col;
}

size_t ash_measure_col_to_offset(const ash_gapbuf *b, size_t line_start,
                                 int target_col, int tab_size, int *out_col)
{
    int col = 0;
    size_t p = line_start;
    ash_grapheme g;
    while (ash_measure_next(b, p, col, tab_size, &g)) {
        if (g.first_cp == '\n')
            break;
        if (col + g.width > target_col)
            break;
        col += g.width;
        p = g.next;
    }
    if (out_col)
        *out_col = col;
    return p;
}
