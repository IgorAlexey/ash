#include "ash/fb/fb.h"

#include <string.h>

#include "ash/term/vtout.h"

struct interval {
    uint32_t lo;
    uint32_t hi;
};

static const struct interval COMBINING[] = {
    { 0x0300, 0x036F }, { 0x0483, 0x0486 }, { 0x0488, 0x0489 },
    { 0x0591, 0x05BD }, { 0x05BF, 0x05BF }, { 0x05C1, 0x05C2 },
    { 0x05C4, 0x05C5 }, { 0x05C7, 0x05C7 }, { 0x0600, 0x0603 },
    { 0x0610, 0x0615 }, { 0x064B, 0x065E }, { 0x0670, 0x0670 },
    { 0x06D6, 0x06E4 }, { 0x06E7, 0x06E8 }, { 0x06EA, 0x06ED },
    { 0x070F, 0x070F }, { 0x0711, 0x0711 }, { 0x0730, 0x074A },
    { 0x07A6, 0x07B0 }, { 0x07EB, 0x07F3 }, { 0x0901, 0x0902 },
    { 0x093C, 0x093C }, { 0x0941, 0x0948 }, { 0x094D, 0x094D },
    { 0x0951, 0x0954 }, { 0x0962, 0x0963 }, { 0x0981, 0x0981 },
    { 0x09BC, 0x09BC }, { 0x09C1, 0x09C4 }, { 0x09CD, 0x09CD },
    { 0x09E2, 0x09E3 }, { 0x0A01, 0x0A02 }, { 0x0A3C, 0x0A3C },
    { 0x0A41, 0x0A42 }, { 0x0A47, 0x0A48 }, { 0x0A4B, 0x0A4D },
    { 0x0A70, 0x0A71 }, { 0x0A81, 0x0A82 }, { 0x0ABC, 0x0ABC },
    { 0x0AC1, 0x0AC5 }, { 0x0AC7, 0x0AC8 }, { 0x0ACD, 0x0ACD },
    { 0x0AE2, 0x0AE3 }, { 0x0B01, 0x0B01 }, { 0x0B3C, 0x0B3C },
    { 0x0B3F, 0x0B3F }, { 0x0B41, 0x0B43 }, { 0x0B4D, 0x0B4D },
    { 0x0B56, 0x0B56 }, { 0x0B82, 0x0B82 }, { 0x0BC0, 0x0BC0 },
    { 0x0BCD, 0x0BCD }, { 0x0C3E, 0x0C40 }, { 0x0C46, 0x0C48 },
    { 0x0C4A, 0x0C4D }, { 0x0C55, 0x0C56 }, { 0x0CBC, 0x0CBC },
    { 0x0CBF, 0x0CBF }, { 0x0CC6, 0x0CC6 }, { 0x0CCC, 0x0CCD },
    { 0x0CE2, 0x0CE3 }, { 0x0D41, 0x0D43 }, { 0x0D4D, 0x0D4D },
    { 0x0DCA, 0x0DCA }, { 0x0DD2, 0x0DD4 }, { 0x0DD6, 0x0DD6 },
    { 0x0E31, 0x0E31 }, { 0x0E34, 0x0E3A }, { 0x0E47, 0x0E4E },
    { 0x0EB1, 0x0EB1 }, { 0x0EB4, 0x0EB9 }, { 0x0EBB, 0x0EBC },
    { 0x0EC8, 0x0ECD }, { 0x0F18, 0x0F19 }, { 0x0F35, 0x0F35 },
    { 0x0F37, 0x0F37 }, { 0x0F39, 0x0F39 }, { 0x0F71, 0x0F7E },
    { 0x0F80, 0x0F84 }, { 0x0F86, 0x0F87 }, { 0x0F90, 0x0F97 },
    { 0x0F99, 0x0FBC }, { 0x0FC6, 0x0FC6 }, { 0x102D, 0x1030 },
    { 0x1032, 0x1032 }, { 0x1036, 0x1037 }, { 0x1039, 0x1039 },
    { 0x1058, 0x1059 }, { 0x1160, 0x11FF }, { 0x135F, 0x135F },
    { 0x1712, 0x1714 }, { 0x1732, 0x1734 }, { 0x1752, 0x1753 },
    { 0x1772, 0x1773 }, { 0x17B4, 0x17B5 }, { 0x17B7, 0x17BD },
    { 0x17C6, 0x17C6 }, { 0x17C9, 0x17D3 }, { 0x17DD, 0x17DD },
    { 0x180B, 0x180D }, { 0x18A9, 0x18A9 }, { 0x1920, 0x1922 },
    { 0x1927, 0x1928 }, { 0x1932, 0x1932 }, { 0x1939, 0x193B },
    { 0x1A17, 0x1A18 }, { 0x1B00, 0x1B03 }, { 0x1B34, 0x1B34 },
    { 0x1B36, 0x1B3A }, { 0x1B3C, 0x1B3C }, { 0x1B42, 0x1B42 },
    { 0x1B6B, 0x1B73 }, { 0x1DC0, 0x1DCA }, { 0x1DFE, 0x1DFF },
    { 0x200B, 0x200F }, { 0x202A, 0x202E }, { 0x2060, 0x2063 },
    { 0x206A, 0x206F }, { 0x20D0, 0x20EF }, { 0x302A, 0x302F },
    { 0x3099, 0x309A }, { 0xA806, 0xA806 }, { 0xA80B, 0xA80B },
    { 0xA825, 0xA826 }, { 0xFB1E, 0xFB1E }, { 0xFE00, 0xFE0F },
    { 0xFE20, 0xFE23 }, { 0xFEFF, 0xFEFF }, { 0xFFF9, 0xFFFB },
    { 0x10A01, 0x10A03 }, { 0x10A05, 0x10A06 }, { 0x10A0C, 0x10A0F },
    { 0x10A38, 0x10A3A }, { 0x10A3F, 0x10A3F }, { 0x1D167, 0x1D169 },
    { 0x1D173, 0x1D182 }, { 0x1D185, 0x1D18B }, { 0x1D1AA, 0x1D1AD },
    { 0x1D242, 0x1D244 }, { 0xE0001, 0xE0001 }, { 0xE0020, 0xE007F },
    { 0xE0100, 0xE01EF },
};

static int is_combining(uint32_t cp)
{
    size_t lo = 0;
    size_t hi = sizeof COMBINING / sizeof COMBINING[0];
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (cp < COMBINING[mid].lo)
            hi = mid;
        else if (cp > COMBINING[mid].hi)
            lo = mid + 1;
        else
            return 1;
    }
    return 0;
}

static int is_wide(uint32_t cp)
{
    return cp >= 0x1100u &&
        (cp <= 0x115Fu ||
         cp == 0x2329u || cp == 0x232Au ||
         (cp >= 0x2E80u && cp <= 0xA4CFu && cp != 0x303Fu) ||
         (cp >= 0xAC00u && cp <= 0xD7A3u) ||
         (cp >= 0xF900u && cp <= 0xFAFFu) ||
         (cp >= 0xFE10u && cp <= 0xFE19u) ||
         (cp >= 0xFE30u && cp <= 0xFE6Fu) ||
         (cp >= 0xFF00u && cp <= 0xFF60u) ||
         (cp >= 0xFFE0u && cp <= 0xFFE6u) ||
         (cp >= 0x1F300u && cp <= 0x1FAFFu) ||
         (cp >= 0x20000u && cp <= 0x2FFFDu) ||
         (cp >= 0x30000u && cp <= 0x3FFFDu));
}

static int cell_width(uint32_t cp)
{
    if (cp == 0)
        return 0;
    if (cp < 0x20u || (cp >= 0x7Fu && cp < 0xA0u))
        return -1;
    if (is_combining(cp))
        return 0;
    return is_wide(cp) ? 2 : 1;
}

static size_t utf8_decode(const uint8_t *p, size_t len, uint32_t *out)
{
    uint8_t b0 = p[0];
    if (b0 < 0x80u) {
        *out = b0;
        return 1;
    }

    uint32_t need;
    uint32_t cp;
    if ((b0 & 0xE0u) == 0xC0u) {
        need = 1;
        cp = b0 & 0x1Fu;
    } else if ((b0 & 0xF0u) == 0xE0u) {
        need = 2;
        cp = b0 & 0x0Fu;
    } else if ((b0 & 0xF8u) == 0xF0u) {
        need = 3;
        cp = b0 & 0x07u;
    } else {
        *out = 0xFFFDu;
        return 1;
    }

    if (need + 1u > len) {
        *out = 0xFFFDu;
        return 1;
    }
    for (uint32_t i = 1; i <= need; i++) {
        if ((p[i] & 0xC0u) != 0x80u) {
            *out = 0xFFFDu;
            return 1;
        }
        cp = (cp << 6) | (p[i] & 0x3Fu);
    }
    *out = cp;
    return (size_t)need + 1u;
}

static size_t utf8_encode(uint32_t cp, uint8_t out[4])
{
    if (cp < 0x80u) {
        out[0] = (uint8_t)cp;
        return 1;
    }
    if (cp < 0x800u) {
        out[0] = (uint8_t)(0xC0u | (cp >> 6));
        out[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 2;
    }
    if (cp < 0x10000u) {
        out[0] = (uint8_t)(0xE0u | (cp >> 12));
        out[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        out[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        return 3;
    }
    out[0] = (uint8_t)(0xF0u | (cp >> 18));
    out[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
    out[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
    out[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
    return 4;
}

int ash_char_width(uint32_t cp)
{
    return cell_width(cp);
}

size_t ash_utf8_decode(const void *utf8, size_t len, uint32_t *cp_out)
{
    uint32_t cp = 0;
    if (len == 0) {
        if (cp_out)
            *cp_out = 0;
        return 0;
    }
    size_t adv = utf8_decode((const uint8_t *)utf8, len, &cp);
    if (cp_out)
        *cp_out = cp;
    return adv;
}

static int cell_eq(const ash_cell *a, const ash_cell *b)
{
    if (a->width != b->width || a->len != b->len || a->attr != b->attr ||
        a->fg != b->fg || a->bg != b->bg)
        return 0;
    return memcmp(a->bytes, b->bytes, a->len) == 0;
}

static ash_cell *cell_at(ash_fb *fb, ash_cell *buf, int x, int y)
{
    return &buf[(size_t)y * (size_t)fb->w + (size_t)x];
}

static void cell_set(ash_cell *c, ash_style st, const uint8_t *bytes,
                     size_t len, int width)
{
    c->fg = st.fg;
    c->bg = st.bg;
    c->attr = st.attr;
    c->width = (uint8_t)width;
    c->len = (uint8_t)len;
    memset(c->bytes, 0, ASH_CELL_BYTES);
    if (len)
        memcpy(c->bytes, bytes, len);
}

void ash_fb_init(ash_fb *fb, ash_arena *arena, ash_style fill)
{
    memset(fb, 0, sizeof *fb);
    fb->arena = arena;
    fb->fill = fill;
    fb->cursor[0] = (ash_cursor){ -1, -1, ASH_CURSOR_HIDDEN };
    fb->cursor[1] = (ash_cursor){ -1, -1, ASH_CURSOR_HIDDEN };
    ash_contrast_init(&fb->contrast, ash_rgb(0, 0, 0), ash_rgb(255, 255, 255));
}

void ash_fb_begin(ash_fb *fb, int w, int h)
{
    if (w < 0)
        w = 0;
    if (h < 0)
        h = 0;

    if (w != fb->w || h != fb->h) {
        size_t n = (size_t)w * (size_t)h;
        size_t alloc = n ? n : 1;
        fb->buffers[0] = ash_array(fb->arena, ash_cell, alloc);
        fb->buffers[1] = ash_array(fb->arena, ash_cell, alloc);
        fb->w = w;
        fb->h = h;
        fb->full_redraw = 1;
    }

    ash_cell blank;
    memset(&blank, 0, sizeof blank);
    blank.fg = fb->fill.fg;
    blank.bg = fb->fill.bg;
    blank.attr = fb->fill.attr;
    blank.width = 1;
    blank.len = 1;
    blank.bytes[0] = ' ';

    ash_cell *back = fb->buffers[fb->frame & 1u];
    size_t n = (size_t)w * (size_t)h;
    for (size_t i = 0; i < n; i++)
        back[i] = blank;

    fb->cursor[fb->frame & 1u] = (ash_cursor){ -1, -1, ASH_CURSOR_HIDDEN };
    fb->clip[0] = (ash_rect){ 0, 0, w, h };
    fb->clip_top = 0;
}

static int is_regional(uint32_t cp)
{
    return cp >= 0x1F1E6u && cp <= 0x1F1FFu;
}

void ash_fb_put_text(ash_fb *fb, int x, int y, ash_style st,
                     const void *utf8, size_t len)
{
    if (fb->w == 0 || fb->h == 0)
        return;

    ash_rect cl = fb->clip[fb->clip_top];
    int cx0 = cl.x < 0 ? 0 : cl.x;
    int cy0 = cl.y < 0 ? 0 : cl.y;
    int cx1 = cl.x + cl.w;
    int cy1 = cl.y + cl.h;
    if (cx1 > fb->w)
        cx1 = fb->w;
    if (cy1 > fb->h)
        cy1 = fb->h;
    if (y < cy0 || y >= cy1 || cx0 >= cx1)
        return;

    ash_cell *back = fb->buffers[fb->frame & 1u];
    const uint8_t *p = utf8;
    size_t off = 0;
    int lead = -1;

    while (off < len) {
        uint32_t cp;
        size_t adv = utf8_decode(p + off, len - off, &cp);
        int wdt = cell_width(cp);

        if (wdt < 0) {
            off += adv;
            continue;
        }
        if (wdt == 0) {
            if (lead >= cx0 && lead < cx1) {
                ash_cell *lc = cell_at(fb, back, lead, y);
                if ((size_t)lc->len + adv <= ASH_CELL_BYTES) {
                    memcpy(lc->bytes + lc->len, p + off, adv);
                    lc->len = (uint8_t)((size_t)lc->len + adv);
                }
            }
            off += adv;
            continue;
        }

        size_t cbeg = off;
        int width = wdt;
        off += adv;

        if (is_regional(cp) && off < len) {
            uint32_t c2;
            size_t a2 = utf8_decode(p + off, len - off, &c2);
            if (is_regional(c2)) {
                off += a2;
                width = 2;
            }
        }

        for (;;) {
            if (off >= len)
                break;
            uint32_t c2;
            size_t a2 = utf8_decode(p + off, len - off, &c2);
            if (c2 == 0x200Du) {
                off += a2;
                if (off < len) {
                    uint32_t c3;
                    size_t a3 = utf8_decode(p + off, len - off, &c3);
                    if (cell_width(c3) == 2)
                        width = 2;
                    off += a3;
                }
                continue;
            }
            if (cell_width(c2) == 0) {
                off += a2;
                continue;
            }
            break;
        }

        if (x >= cx1 || x + width > cx1)
            break;
        if (x < cx0) {
            x += width;
            continue;
        }

        size_t clen = off - cbeg;
        if (clen > ASH_CELL_BYTES) {
            size_t k = 0;
            while (k < clen) {
                uint32_t cc;
                size_t a = utf8_decode(p + cbeg + k, clen - k, &cc);
                if (k + a > ASH_CELL_BYTES)
                    break;
                k += a;
            }
            clen = k;
        }

        cell_set(cell_at(fb, back, x, y), st, p + cbeg, clen, width);
        lead = x;
        if (width == 2)
            cell_set(cell_at(fb, back, x + 1, y), st, NULL, 0, 0);
        x += width;
    }
}

void ash_fb_fill_rect(ash_fb *fb, ash_rect r, ash_style st, uint32_t cp)
{
    if (fb->w == 0 || fb->h == 0)
        return;

    uint8_t bytes[4];
    size_t bl = utf8_encode(cp, bytes);

    ash_rect cl = fb->clip[fb->clip_top];
    int x0 = r.x > cl.x ? r.x : cl.x;
    int y0 = r.y > cl.y ? r.y : cl.y;
    int x1 = r.x + r.w;
    int y1 = r.y + r.h;
    int cx1 = cl.x + cl.w;
    int cy1 = cl.y + cl.h;
    if (x1 > cx1)
        x1 = cx1;
    if (y1 > cy1)
        y1 = cy1;
    if (x0 < 0)
        x0 = 0;
    if (y0 < 0)
        y0 = 0;
    if (x1 > fb->w)
        x1 = fb->w;
    if (y1 > fb->h)
        y1 = fb->h;

    ash_cell *back = fb->buffers[fb->frame & 1u];
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++)
            cell_set(cell_at(fb, back, x, y), st, bytes, bl, 1);
}

void ash_fb_add_attr(ash_fb *fb, int x, int y, int n, uint16_t attr)
{
    if (fb->w == 0 || fb->h == 0 || y < 0 || y >= fb->h)
        return;
    if (x < 0) {
        n += x;
        x = 0;
    }
    if (x + n > fb->w)
        n = fb->w - x;
    if (n <= 0)
        return;

    ash_cell *back = fb->buffers[fb->frame & 1u];
    for (int cx = x; cx < x + n; cx++)
        back[(size_t)y * (size_t)fb->w + (size_t)cx].attr |= attr;
}

void ash_fb_style_range(ash_fb *fb, int x, int y, int n, ash_style st)
{
    if (fb->w == 0 || fb->h == 0 || y < 0 || y >= fb->h)
        return;
    if (x < 0) {
        n += x;
        x = 0;
    }
    if (x + n > fb->w)
        n = fb->w - x;
    if (n <= 0)
        return;

    ash_cell *back = fb->buffers[fb->frame & 1u];
    for (int cx = x; cx < x + n; cx++) {
        ash_cell *c = &back[(size_t)y * (size_t)fb->w + (size_t)cx];
        c->fg = st.fg;
        c->bg = st.bg;
        c->attr = (uint16_t)(c->attr | st.attr);
    }
}

ash_style ash_selection_style(void)
{
    return (ash_style){ ash_rgb(0, 0, 0), ash_rgb(0x77, 0x9a, 0xe2), ASH_ATTR_NONE };
}

void ash_fb_set_cursor(ash_fb *fb, int x, int y, int style)
{
    if (style != ASH_CURSOR_BLOCK && style != ASH_CURSOR_BAR)
        style = ASH_CURSOR_BLOCK;
    fb->cursor[fb->frame & 1u] = (ash_cursor){ x, y, style };
}

void ash_fb_hide_cursor(ash_fb *fb)
{
    fb->cursor[fb->frame & 1u] = (ash_cursor){ -1, -1, ASH_CURSOR_HIDDEN };
}

int ash_fb_clip_push(ash_fb *fb, ash_rect r)
{
    if (fb->clip_top + 1 >= ASH_FB_CLIP_MAX)
        return 0;

    ash_rect cur = fb->clip[fb->clip_top];
    int x0 = cur.x > r.x ? cur.x : r.x;
    int y0 = cur.y > r.y ? cur.y : r.y;
    int cx1 = cur.x + cur.w;
    int cy1 = cur.y + cur.h;
    int rx1 = r.x + r.w;
    int ry1 = r.y + r.h;
    int x1 = cx1 < rx1 ? cx1 : rx1;
    int y1 = cy1 < ry1 ? cy1 : ry1;

    ash_rect ni = { x0, y0, x1 > x0 ? x1 - x0 : 0, y1 > y0 ? y1 - y0 : 0 };
    fb->clip[++fb->clip_top] = ni;
    return 1;
}

void ash_fb_clip_pop(ash_fb *fb)
{
    if (fb->clip_top > 0)
        fb->clip_top--;
}

ash_rgba ash_fb_contrasted(ash_fb *fb, ash_rgba color)
{
    return ash_contrasted(&fb->contrast, color);
}

void ash_fb_flip(ash_fb *fb, ash_buf *out)
{
    ash_cell *back = fb->buffers[fb->frame & 1u];
    ash_cell *front = fb->buffers[(fb->frame ^ 1u) & 1u];

    int emitted = 0;
    int have_state = 0;
    ash_rgba last_fg = 0;
    ash_rgba last_bg = 0;
    uint16_t last_attr = 0;

    for (int y = 0; y < fb->h; y++) {
        ash_cell *brow = back + (size_t)y * (size_t)fb->w;
        ash_cell *frow = front + (size_t)y * (size_t)fb->w;
        int col = 0;

        while (col < fb->w) {
            if (!fb->full_redraw && cell_eq(&brow[col], &frow[col])) {
                col++;
                continue;
            }
            int s = col;
            while (col < fb->w &&
                   (fb->full_redraw || !cell_eq(&brow[col], &frow[col])))
                col++;
            int e = col;
            while (s > 0 && brow[s].width == 0)
                s--;

            if (!emitted) {
                ash_vt_reset(out);
                emitted = 1;
            }
            ash_vt_cup(out, y + 1, s + 1);

            for (int cx = s; cx < e; cx++) {
                ash_cell *c = &brow[cx];
                if (c->width == 0)
                    continue;
                if (!have_state || c->fg != last_fg) {
                    ash_vt_color(out, 1, c->fg);
                    last_fg = c->fg;
                }
                if (!have_state || c->bg != last_bg) {
                    ash_vt_color(out, 0, c->bg);
                    last_bg = c->bg;
                }
                if (!have_state || c->attr != last_attr) {
                    ash_vt_attr_diff(out, have_state ? last_attr : 0, c->attr);
                    last_attr = c->attr;
                }
                have_state = 1;
                ash_buf_append(out, c->bytes, c->len);
            }
        }
    }

    ash_cursor bc = fb->cursor[fb->frame & 1u];
    ash_cursor fc = fb->cursor[(fb->frame ^ 1u) & 1u];
    int cur_changed = fb->full_redraw || bc.x != fc.x || bc.y != fc.y ||
                      bc.style != fc.style;
    if (emitted || cur_changed) {
        if (bc.style != ASH_CURSOR_HIDDEN && bc.x >= 0 && bc.y >= 0) {
            int decscusr = (bc.style == ASH_CURSOR_BAR) ? 5 : 1;
            ash_vt_cursor_show(out, bc.y + 1, bc.x + 1, decscusr);
        } else {
            ash_vt_cursor_hide(out);
        }
    }

    fb->frame++;
    fb->full_redraw = 0;
}

static int style_is_default(const ash_cell *c)
{
    return c->fg == ASH_RGBA_DEFAULT && c->bg == ASH_RGBA_DEFAULT && c->attr == 0;
}

static char snap_key(const ash_cell *c, ash_rgba *fgs, ash_rgba *bgs,
                     uint16_t *attrs, int *count)
{
    if (style_is_default(c))
        return '-';
    for (int i = 0; i < *count; i++) {
        if (fgs[i] == c->fg && bgs[i] == c->bg && attrs[i] == c->attr)
            return i < 26 ? (char)('A' + i) : (char)('a' + i - 26);
    }
    int i = *count;
    fgs[i] = c->fg;
    bgs[i] = c->bg;
    attrs[i] = c->attr;
    *count = i + 1;
    return i < 26 ? (char)('A' + i) : (char)('a' + i - 26);
}

static void snap_hex(ash_buf *b, ash_rgba c)
{
    static const char h[] = "0123456789abcdef";
    if (ash_rgba_a(c) == 0) {
        ash_buf_append_cstr(b, "default");
        return;
    }
    uint32_t r = ash_rgba_r(c);
    uint32_t g = ash_rgba_g(c);
    uint32_t bl = ash_rgba_b(c);
    uint32_t a = ash_rgba_a(c);
    char t[8];
    int n = 0;
    t[n++] = h[r >> 4];
    t[n++] = h[r & 0xfu];
    t[n++] = h[g >> 4];
    t[n++] = h[g & 0xfu];
    t[n++] = h[bl >> 4];
    t[n++] = h[bl & 0xfu];
    if (a != 0xffu) {
        t[n++] = h[a >> 4];
        t[n++] = h[a & 0xfu];
    }
    ash_buf_append(b, t, (size_t)n);
}

static void snap_flags(ash_buf *b, uint16_t attr)
{
    static const struct {
        uint16_t bit;
        const char *name;
    } m[] = {
        { ASH_ATTR_BOLD, "bold" },
        { ASH_ATTR_ITALIC, "italic" },
        { ASH_ATTR_UNDERLINE, "underline" },
        { ASH_ATTR_REVERSE, "reverse" },
        { ASH_ATTR_STRIKETHROUGH, "strike" },
    };
    if (attr == 0) {
        ash_buf_append_cstr(b, "none");
        return;
    }
    int first = 1;
    for (size_t i = 0; i < sizeof m / sizeof m[0]; i++) {
        if (!(attr & m[i].bit))
            continue;
        if (!first)
            ash_buf_append_byte(b, ',');
        ash_buf_append_cstr(b, m[i].name);
        first = 0;
    }
}

void ash_fb_snapshot(const ash_fb *fb, ash_buf *out)
{
    ash_rgba fgs[52];
    ash_rgba bgs[52];
    uint16_t attrs[52];
    int count = 0;

    ash_buf_append_cstr(out, "ash-fb ");
    ash_vt_u32(out, (uint32_t)fb->w);
    ash_buf_append_byte(out, 'x');
    ash_vt_u32(out, (uint32_t)fb->h);
    ash_buf_append_byte(out, '\n');

    ash_cursor cur = fb->cursor[fb->frame & 1u];
    ash_buf_append_cstr(out, "cursor: ");
    if (cur.style == ASH_CURSOR_HIDDEN || cur.x < 0 || cur.y < 0) {
        ash_buf_append_cstr(out, "hidden\n");
    } else {
        ash_vt_u32(out, (uint32_t)cur.x);
        ash_buf_append_byte(out, ',');
        ash_vt_u32(out, (uint32_t)cur.y);
        ash_buf_append_byte(out, ' ');
        ash_buf_append_cstr(out, cur.style == ASH_CURSOR_BAR ? "bar\n" : "block\n");
    }

    const ash_cell *buf = fb->buffers[fb->frame & 1u];
    for (int y = 0; y < fb->h; y++) {
        const ash_cell *row = buf + (size_t)y * (size_t)fb->w;
        ash_vt_u32(out, (uint32_t)y);
        ash_buf_append_cstr(out, " |");
        for (int x = 0; x < fb->w; x++)
            if (row[x].width >= 1)
                ash_buf_append(out, row[x].bytes, row[x].len);
        ash_buf_append_cstr(out, "| ");

        int x = 0;
        while (x < fb->w) {
            char k = snap_key(&row[x], fgs, bgs, attrs, &count);
            int s = x;
            while (x < fb->w &&
                   snap_key(&row[x], fgs, bgs, attrs, &count) == k)
                x++;
            if (s != 0)
                ash_buf_append_byte(out, ' ');
            ash_vt_u32(out, (uint32_t)s);
            ash_buf_append_byte(out, ':');
            ash_vt_u32(out, (uint32_t)(x - s));
            ash_buf_append_byte(out, (unsigned char)k);
        }
        ash_buf_append_byte(out, '\n');
    }

    ash_buf_append_cstr(out, "legend:\n");
    for (int i = 0; i < count; i++) {
        ash_buf_append_byte(out, ' ');
        ash_buf_append_byte(out,
            (unsigned char)(i < 26 ? 'A' + i : 'a' + i - 26));
        ash_buf_append_cstr(out, " fg=");
        snap_hex(out, fgs[i]);
        ash_buf_append_cstr(out, " bg=");
        snap_hex(out, bgs[i]);
        ash_buf_append_byte(out, ' ');
        snap_flags(out, attrs[i]);
        ash_buf_append_byte(out, '\n');
    }
}
