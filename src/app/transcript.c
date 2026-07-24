#include "ash/app/transcript.h"

#include <stdio.h>
#include <string.h>

#include "ash/fb/fb.h"

enum { ASH_TS_ROW_MAX = 1024 };
enum { TAB_WIDTH = 4 };

void ash_ts_init(ash_transcript *t, ash_arena *a)
{
    t->arena = a;
    t->blocks = NULL;
    t->count = 0;
    t->cap = 0;
    t->next_id = 0;
}

static ash_ts_block *grow(ash_transcript *t)
{
    if (t->count == t->cap) {
        size_t nc = t->cap ? t->cap * 2 : 16;
        ash_ts_block *nb = ash_array(t->arena, ash_ts_block, nc);
        if (t->count)
            memcpy(nb, t->blocks, t->count * sizeof *nb);
        t->blocks = nb;
        t->cap = nc;
    }
    return &t->blocks[t->count++];
}

uint64_t ash_ts_append(ash_transcript *t, ash_ts_kind kind,
                       const char *text, size_t text_len,
                       const char *title, size_t title_len)
{
    ash_ts_block *b = grow(t);
    b->kind = kind;
    b->id = t->next_id++;
    b->proj_seq = UINT64_MAX;
    ash_buf_init(&b->text, t->arena);
    ash_buf_init(&b->title, t->arena);
    if (text != NULL && text_len)
        ash_buf_append(&b->text, text, text_len);
    if (title != NULL && title_len)
        ash_buf_append(&b->title, title, title_len);
    return b->id;
}

void ash_ts_append_stream(ash_transcript *t, const char *text, size_t n)
{
    if (t->count == 0 || text == NULL || n == 0)
        return;
    ash_buf_append(&t->blocks[t->count - 1].text, text, n);
}

size_t ash_ts_count(const ash_transcript *t)
{
    return t->count;
}

const ash_ts_block *ash_ts_get(const ash_transcript *t, size_t i)
{
    if (i >= t->count)
        return NULL;
    return &t->blocks[i];
}

typedef struct row_builder {
    ash_cell cells[ASH_TS_ROW_MAX];
    size_t   n;
    int      colw;
} row_builder;

static void rb_pad(row_builder *rb, int count, ash_rgba bg)
{
    for (int i = 0; i < count && rb->n < ASH_TS_ROW_MAX; i++) {
        ash_cell c;
        memset(&c, 0, sizeof c);
        c.bytes[0] = ' ';
        c.len = 1;
        c.width = 1;
        c.bg = bg;
        rb->cells[rb->n++] = c;
    }
}

static void rb_glyph(row_builder *rb, ash_style st, int content,
                     const char *p, size_t adv, int cw, int tab)
{
    if (rb->n + (size_t)(cw == 2 ? 2 : 1) > ASH_TS_ROW_MAX)
        return;
    ash_cell c;
    memset(&c, 0, sizeof c);
    if (tab) {
        c.bytes[0] = ' ';
        c.len = 1;
    } else {
        size_t a = adv <= ASH_CELL_BYTES ? adv : 0;
        if (a) {
            memcpy(c.bytes, p, a);
            c.len = (uint8_t)a;
        } else {
            c.bytes[0] = '?';
            c.len = 1;
        }
    }
    c.width = (uint8_t)cw;
    c.attr = (uint16_t)(st.attr | (content ? ASH_ATTR_CONTENT : 0));
    c.fg = st.fg;
    c.bg = st.bg;
    rb->cells[rb->n++] = c;
    if (cw == 2) {
        ash_cell pad;
        memset(&pad, 0, sizeof pad);
        pad.attr = c.attr;
        pad.fg = st.fg;
        pad.bg = st.bg;
        rb->cells[rb->n++] = pad;
    }
    rb->colw += cw;
}

static void rb_combine(row_builder *rb, const char *p, size_t adv)
{
    if (rb->n == 0)
        return;
    ash_cell *prev = &rb->cells[rb->n - 1];
    if ((size_t)prev->len + adv <= ASH_CELL_BYTES) {
        memcpy(prev->bytes + prev->len, p, adv);
        prev->len = (uint8_t)(prev->len + adv);
    }
}

typedef void (*row_sink)(void *ctx, const ash_cell *cells, size_t n, int colw,
                         int cont);

static void wrap_line(ash_style st, int content, const char *s, size_t len,
                      int max_w, row_sink sink, void *ctx)
{
    row_builder rb;
    rb.n = 0;
    rb.colw = 0;
    int cont = 0;

    size_t off = 0;
    while (off < len) {
        uint32_t cp = 0;
        size_t adv = ash_utf8_decode((const uint8_t *)s + off, len - off, &cp);
        if (adv == 0) {
            off++;
            continue;
        }
        if (cp == '\r') {
            off += adv;
            continue;
        }
        int cw = cp == '\t' ? 1 : ash_char_width(cp);
        if (cw == 0) {
            rb_combine(&rb, s + off, adv);
            off += adv;
            continue;
        }
        int reps = cp == '\t' ? TAB_WIDTH - rb.colw % TAB_WIDTH : 1;
        for (int r = 0; r < reps; r++) {
            if (rb.colw + cw > max_w) {
                sink(ctx, rb.cells, rb.n, rb.colw, cont);
                rb.n = 0;
                rb.colw = 0;
                cont = 1;
            }
            rb_glyph(&rb, st, content, s + off, adv, cw, cp == '\t');
        }
        off += adv;
    }
    sink(ctx, rb.cells, rb.n, rb.colw, cont);
}

static void wrap_text(ash_style st, int content, const char *s, size_t len,
                      int max_w, row_sink sink, void *ctx)
{
    if (len == 0)
        return;
    size_t i = 0;
    for (;;) {
        size_t j = i;
        while (j < len && s[j] != '\n')
            j++;
        wrap_line(st, content, s + i, j - i, max_w, sink, ctx);
        if (j >= len)
            break;
        i = j + 1;
        if (i == len)
            break;
    }
}

static int pad_of(const ash_ts_opts *o)
{
    return o->pad_x < 0 ? 0 : o->pad_x;
}

static int usable_of(int width, int pad)
{
    int usable = width - 2 * pad;
    if (usable < 1)
        usable = 1;
    if (usable > ASH_TS_ROW_MAX - 2 * pad)
        usable = ASH_TS_ROW_MAX - 2 * pad;
    return usable;
}

typedef struct seg_ctx {
    ash_scrollback *sb;
    ash_rgba        bg;
    int             pad;
    int             usable;
    int             fill;
} seg_ctx;

static void seg_sink(void *v, const ash_cell *cells, size_t n, int colw,
                     int cont)
{
    seg_ctx *c = v;
    row_builder out;
    out.n = 0;
    out.colw = 0;
    rb_pad(&out, c->pad, c->bg);
    for (size_t i = 0; i < n && out.n < ASH_TS_ROW_MAX; i++)
        out.cells[out.n++] = cells[i];
    if (c->fill && colw < c->usable)
        rb_pad(&out, c->usable - colw, c->bg);
    rb_pad(&out, c->pad, c->bg);
    ash_sb_append_wrapped(c->sb, out.cells, out.n, cont);
}

static void emit_segment(ash_scrollback *sb, ash_style st, int content,
                         const char *s, size_t len, int width,
                         const ash_ts_opts *o, ash_rgba bg, int fill)
{
    int pad = pad_of(o);
    int usable = usable_of(width, pad);
    seg_ctx c = { sb, bg, pad, usable, fill };
    wrap_line(st, content, s, len, usable, seg_sink, &c);
}

static void emit_text(ash_scrollback *sb, ash_style st, int content,
                      const char *s, size_t len, int width,
                      const ash_ts_opts *o, ash_rgba bg, int fill)
{
    if (len == 0)
        return;
    int pad = pad_of(o);
    int usable = usable_of(width, pad);
    seg_ctx c = { sb, bg, pad, usable, fill };
    wrap_text(st, content, s, len, usable, seg_sink, &c);
}

static void emit_user_edge(ash_scrollback *sb, ash_rgba bg, int bw)
{
    row_builder rb;
    rb.n = 0;
    rb.colw = 0;
    rb_pad(&rb, bw, bg);
    ash_sb_append_wrapped(sb, rb.cells, rb.n, 0);
}

static void emit_user(ash_scrollback *sb, const ash_ts_opts *o, const char *s,
                      size_t len, int width)
{
    int pad = pad_of(o);
    int bw = usable_of(width, pad) + 2 * pad;
    ash_rgba bg = o->theme->user_msg.bg;
    emit_user_edge(sb, bg, bw);
    emit_text(sb, o->theme->user_msg, 1, s, len, width, o, bg, 1);
    emit_user_edge(sb, bg, bw);
}

static void emit_gap(ash_scrollback *sb)
{
    ash_cell none = { 0 };
    ash_sb_append_wrapped(sb, &none, 0, 0);
}

static void emit_prose(ash_scrollback *sb, ash_style st, const char *s,
                       size_t len, int width, const ash_ts_opts *o)
{
    if (len == 0)
        return;
    emit_gap(sb);
    emit_text(sb, st, 1, s, len, width, o, ASH_RGBA_DEFAULT, 0);
    emit_gap(sb);
}

static size_t count_lines(const char *s, size_t len)
{
    if (len == 0)
        return 0;
    size_t count = 0;
    size_t i = 0;
    for (;;) {
        size_t j = i;
        while (j < len && s[j] != '\n')
            j++;
        count++;
        if (j >= len)
            break;
        i = j + 1;
        if (i == len)
            break;
    }
    return count;
}

static size_t offset_of_line(const char *s, size_t len, size_t k)
{
    size_t off = 0;
    size_t idx = 0;
    for (size_t p = 0; p < len && idx < k; p++) {
        if (s[p] == '\n') {
            idx++;
            off = p + 1;
        }
    }
    return off;
}

static void emit_tool_out(ash_scrollback *sb, const ash_ts_opts *o,
                          const char *s, size_t len, int width)
{
    ash_style st = o->theme->tool_out;
    size_t total = count_lines(s, len);
    if (o->tools_expanded || total <= ASH_TS_TAIL_LINES) {
        emit_text(sb, st, 1, s, len, width, o, ASH_RGBA_DEFAULT, 0);
        return;
    }
    size_t hidden = total - ASH_TS_TAIL_LINES;
    char mark[64];
    int mn = snprintf(mark, sizeof mark,
                      "... %zu more lines (ctrl+o to expand)", hidden);
    if (mn > 0)
        emit_segment(sb, o->theme->marker, 0, mark, (size_t)mn, width, o,
                     ASH_RGBA_DEFAULT, 0);
    size_t off = offset_of_line(s, len, total - ASH_TS_TAIL_LINES);
    emit_text(sb, st, 1, s + off, len - off, width, o, ASH_RGBA_DEFAULT, 0);
}

static void emit_tool_head(ash_scrollback *sb, const ash_ts_opts *o,
                           const char *title, size_t tlen, int width)
{
    char hdr[ASH_TS_ROW_MAX];
    int hn = snprintf(hdr, sizeof hdr, "$ %.*s", (int)tlen, title);
    if (hn <= 0)
        return;
    size_t n = (size_t)hn < sizeof hdr ? (size_t)hn : sizeof hdr - 1;
    emit_segment(sb, o->theme->tool_head, 1, hdr, n, width, o,
                 ASH_RGBA_DEFAULT, 0);
}

static void emit_block(const ash_ts_block *b, ash_scrollback *sb, int width,
                       const ash_ts_opts *o)
{
    const char *s = (const char *)b->text.data;
    size_t len = b->text.len;

    switch (b->kind) {
    case ASH_TS_USER:
        emit_user(sb, o, s, len, width);
        return;
    case ASH_TS_TOOL_HEAD:
        emit_tool_head(sb, o, (const char *)b->title.data, b->title.len, width);
        return;
    case ASH_TS_TOOL_OUT:
        emit_tool_out(sb, o, s, len, width);
        return;
    case ASH_TS_AGENT:
        emit_prose(sb, o->theme->text, s, len, width, o);
        return;
    case ASH_TS_ERROR:
        emit_prose(sb, o->theme->error, s, len, width, o);
        return;
    case ASH_TS_INFO:
        emit_prose(sb, o->theme->text, s, len, width, o);
        return;
    }
}

void ash_ts_project(ash_transcript *t, ash_scrollback *sb, int width,
                    const ash_ts_opts *o)
{
    ash_sb_reset(sb);
    for (size_t i = 0; i < t->count; i++) {
        t->blocks[i].proj_seq = ash_sb_newest(sb);
        emit_block(&t->blocks[i], sb, width, o);
    }
}

void ash_ts_project_tail(ash_transcript *t, ash_scrollback *sb, int width,
                         const ash_ts_opts *o)
{
    if (t->count == 0)
        return;
    ash_ts_block *b = &t->blocks[t->count - 1];
    if (b->proj_seq == UINT64_MAX) {
        b->proj_seq = ash_sb_newest(sb);
    } else if (sb->count > 0 && b->proj_seq < ash_sb_oldest(sb)) {
        ash_ts_project(t, sb, width, o);
        return;
    } else {
        ash_sb_rewind_to(sb, b->proj_seq);
    }
    emit_block(b, sb, width, o);
}

size_t ash_ts_block_at_seq(const ash_transcript *t, uint64_t seq)
{
    size_t hit = 0;
    for (size_t i = 0; i < t->count; i++) {
        if (t->blocks[i].proj_seq != UINT64_MAX && t->blocks[i].proj_seq <= seq)
            hit = i;
    }
    return hit;
}

static const ash_theme DARK = {
    .text      = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, ASH_ATTR_NONE },
    .user_msg  = { 0xfff2ede8u, 0xff784f26u, ASH_ATTR_NONE },
    .tool_head = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, ASH_ATTR_BOLD },
    .tool_out  = { 0xff969696u, ASH_RGBA_DEFAULT, ASH_ATTR_NONE },
    .marker    = { 0xff969696u, ASH_RGBA_DEFAULT, ASH_ATTR_NONE },
    .error     = { 0xff5a5adcu, ASH_RGBA_DEFAULT, ASH_ATTR_NONE },
};

static const ash_theme LIGHT = {
    .text      = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, ASH_ATTR_NONE },
    .user_msg  = { 0xff2d1e14u, 0xfff5e1d2u, ASH_ATTR_NONE },
    .tool_head = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, ASH_ATTR_BOLD },
    .tool_out  = { 0xff6e6e6eu, ASH_RGBA_DEFAULT, ASH_ATTR_NONE },
    .marker    = { 0xff6e6e6eu, ASH_RGBA_DEFAULT, ASH_ATTR_NONE },
    .error     = { 0xff2828beu, ASH_RGBA_DEFAULT, ASH_ATTR_NONE },
};

const ash_theme *ash_theme_dark(void)
{
    return &DARK;
}

const ash_theme *ash_theme_light(void)
{
    return &LIGHT;
}

const ash_theme *ash_theme_select(const char *name)
{
    if (name != NULL && strcmp(name, "light") == 0)
        return &LIGHT;
    return &DARK;
}
