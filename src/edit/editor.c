#include "ash/edit/editor.h"

#include <stdio.h>
#include <string.h>

#include "ash/edit/measure.h"

void ash_editor_init(ash_editor *ed, ash_arena *arena)
{
    ed->arena = arena;
    ash_gapbuf_init(&ed->buf, arena);
    ed->cursor = 0;
    ed->anchor = 0;
    ed->goal_col = -1;
    ed->top_line = 0;
    ed->left_col = 0;
    ed->tab_size = 4;
    ed->show_gutter = 1;
    ed->view = (ash_rect){ 0, 0, 0, 0 };
    ed->gutter_w = 0;
}

void ash_editor_set_text(ash_editor *ed, const void *text, size_t len)
{
    ash_gapbuf_set(&ed->buf, text, len);
    ed->cursor = 0;
    ed->anchor = 0;
    ed->goal_col = -1;
    ed->top_line = 0;
    ed->left_col = 0;
}

void ash_editor_text(const ash_editor *ed, ash_buf *out)
{
    ash_gapbuf_extract(&ed->buf, out);
}

size_t ash_editor_len(const ash_editor *ed)
{
    return ash_gapbuf_len(&ed->buf);
}

size_t ash_editor_cursor(const ash_editor *ed)
{
    return ed->cursor;
}

int ash_editor_selection(const ash_editor *ed, size_t *start, size_t *end)
{
    if (ed->anchor == ed->cursor)
        return 0;
    size_t a = ed->anchor, c = ed->cursor;
    if (start)
        *start = a < c ? a : c;
    if (end)
        *end = a < c ? c : a;
    return 1;
}

void ash_editor_set_cursor(ash_editor *ed, size_t off, int extend)
{
    size_t len = ash_gapbuf_len(&ed->buf);
    if (off > len)
        off = len;
    ed->cursor = off;
    if (!extend)
        ed->anchor = off;
    ed->goal_col = -1;
}

void ash_editor_line_col(const ash_editor *ed, size_t off, int *line, int *col)
{
    size_t ls = ash_measure_line_start(&ed->buf, off);
    if (line)
        *line = ash_measure_line_index(&ed->buf, off);
    if (col)
        *col = ash_measure_col(&ed->buf, ls, off, ed->tab_size);
}

static int cursor_col(const ash_editor *ed)
{
    size_t ls = ash_measure_line_start(&ed->buf, ed->cursor);
    return ash_measure_col(&ed->buf, ls, ed->cursor, ed->tab_size);
}

static void move_updown(ash_editor *ed, int delta, int extend)
{
    if (ed->goal_col < 0)
        ed->goal_col = cursor_col(ed);
    int line = ash_measure_line_index(&ed->buf, ed->cursor);
    int target = line + delta;
    if (target < 0)
        return;
    int last = ash_measure_line_index(&ed->buf, ash_gapbuf_len(&ed->buf));
    if (target > last)
        return;
    size_t ls = ash_measure_line_at(&ed->buf, target);
    size_t off = ash_measure_col_to_offset(&ed->buf, ls, ed->goal_col,
                                           ed->tab_size, NULL);
    ed->cursor = off;
    if (!extend)
        ed->anchor = off;
}

static int is_ws(uint8_t b)
{
    return b == ' ' || b == '\t' || b == '\n';
}

static size_t word_left(const ash_gapbuf *b, size_t c)
{
    while (c > 0 && is_ws(ash_gapbuf_at(b, ash_measure_prev(b, c))))
        c = ash_measure_prev(b, c);
    while (c > 0 && !is_ws(ash_gapbuf_at(b, ash_measure_prev(b, c))))
        c = ash_measure_prev(b, c);
    return c;
}

static size_t word_right(const ash_gapbuf *b, size_t c)
{
    ash_grapheme g;
    size_t len = ash_gapbuf_len(b);
    while (c < len && is_ws(ash_gapbuf_at(b, c))) {
        if (!ash_measure_next(b, c, 0, 1, &g))
            break;
        c = g.next;
    }
    while (c < len && !is_ws(ash_gapbuf_at(b, c))) {
        if (!ash_measure_next(b, c, 0, 1, &g))
            break;
        c = g.next;
    }
    return c;
}

void ash_editor_move(ash_editor *ed, ash_editcmd cmd, int extend)
{
    ash_gapbuf *b = &ed->buf;
    size_t len = ash_gapbuf_len(b);
    int reset_goal = 1;
    if (cmd == ASH_EC_LEFT) {
        ed->cursor = ash_measure_prev(b, ed->cursor);
    } else if (cmd == ASH_EC_RIGHT) {
        ash_grapheme g;
        if (ash_measure_next(b, ed->cursor, 0, ed->tab_size, &g))
            ed->cursor = g.next;
    } else if (cmd == ASH_EC_WORD_LEFT) {
        ed->cursor = word_left(b, ed->cursor);
    } else if (cmd == ASH_EC_WORD_RIGHT) {
        ed->cursor = word_right(b, ed->cursor);
    } else if (cmd == ASH_EC_HOME) {
        ed->cursor = ash_measure_line_start(b, ed->cursor);
    } else if (cmd == ASH_EC_END) {
        ed->cursor = ash_measure_line_end(b, ed->cursor);
    } else if (cmd == ASH_EC_DOC_HOME) {
        ed->cursor = 0;
    } else if (cmd == ASH_EC_DOC_END) {
        ed->cursor = len;
    } else if (cmd == ASH_EC_UP) {
        move_updown(ed, -1, extend);
        reset_goal = 0;
    } else if (cmd == ASH_EC_DOWN) {
        move_updown(ed, +1, extend);
        reset_goal = 0;
    } else {
        return;
    }
    if (!extend)
        ed->anchor = ed->cursor;
    if (reset_goal)
        ed->goal_col = -1;
}

static void erase_selection(ash_editor *ed)
{
    size_t s, e;
    if (!ash_editor_selection(ed, &s, &e))
        return;
    ash_gapbuf_erase(&ed->buf, s, e);
    ed->cursor = s;
    ed->anchor = s;
}

static void editor_insert(ash_editor *ed, const void *text, size_t len)
{
    erase_selection(ed);
    ash_gapbuf_insert(&ed->buf, ed->cursor, text, len);
    ed->cursor += len;
    ed->anchor = ed->cursor;
    ed->goal_col = -1;
}

static void editor_backspace(ash_editor *ed)
{
    if (ash_editor_selection(ed, NULL, NULL)) {
        erase_selection(ed);
        return;
    }
    if (ed->cursor == 0)
        return;
    size_t p = ash_measure_prev(&ed->buf, ed->cursor);
    ash_gapbuf_erase(&ed->buf, p, ed->cursor);
    ed->cursor = p;
    ed->anchor = p;
    ed->goal_col = -1;
}

static void editor_delete(ash_editor *ed)
{
    if (ash_editor_selection(ed, NULL, NULL)) {
        erase_selection(ed);
        return;
    }
    ash_grapheme g;
    if (ash_measure_next(&ed->buf, ed->cursor, 0, ed->tab_size, &g))
        ash_gapbuf_erase(&ed->buf, ed->cursor, g.next);
}

static void erase_range(ash_editor *ed, size_t s, size_t e)
{
    if (e <= s)
        return;
    ash_gapbuf_erase(&ed->buf, s, e);
    ed->cursor = s;
    ed->anchor = s;
    ed->goal_col = -1;
}

void ash_editor_apply(ash_editor *ed, ash_key k)
{
    ash_gapbuf *b = &ed->buf;
    switch (k.cmd) {
    case ASH_EC_INSERT:
    case ASH_EC_PASTE_CHUNK:
        editor_insert(ed, k.text, k.len);
        break;
    case ASH_EC_NEWLINE:
        editor_insert(ed, "\n", 1);
        break;
    case ASH_EC_BACKSPACE:
        editor_backspace(ed);
        break;
    case ASH_EC_DELETE:
        editor_delete(ed);
        break;
    case ASH_EC_BACKSPACE_WORD:
        erase_range(ed, word_left(b, ed->cursor), ed->cursor);
        break;
    case ASH_EC_DELETE_WORD:
        erase_range(ed, ed->cursor, word_right(b, ed->cursor));
        break;
    case ASH_EC_KILL_TO_END: {
        size_t e = ash_measure_line_end(b, ed->cursor);
        erase_range(ed, ed->cursor, e);
        break;
    }
    case ASH_EC_KILL_LINE: {
        size_t s = ash_measure_line_start(b, ed->cursor);
        size_t e = ash_measure_line_end(b, ed->cursor);
        erase_range(ed, s, e);
        break;
    }
    case ASH_EC_LEFT:
    case ASH_EC_RIGHT:
    case ASH_EC_UP:
    case ASH_EC_DOWN:
    case ASH_EC_WORD_LEFT:
    case ASH_EC_WORD_RIGHT:
    case ASH_EC_HOME:
    case ASH_EC_END:
    case ASH_EC_DOC_HOME:
    case ASH_EC_DOC_END:
        ash_editor_move(ed, k.cmd, 0);
        break;
    case ASH_EC_SELECT_LEFT:
    case ASH_EC_SELECT_RIGHT:
    case ASH_EC_SELECT_WORD_LEFT:
    case ASH_EC_SELECT_WORD_RIGHT:
    case ASH_EC_SELECT_UP:
    case ASH_EC_SELECT_DOWN:
    case ASH_EC_SELECT_HOME:
    case ASH_EC_SELECT_END:
    case ASH_EC_SELECT_DOC_HOME:
    case ASH_EC_SELECT_DOC_END:
    case ASH_EC_SELECT_ALL:
    case ASH_EC_UNDO:
    case ASH_EC_REDO:
    case ASH_EC_COPY:
    case ASH_EC_CUT:
    case ASH_EC_PASTE:
    case ASH_EC_NONE:
    case ASH_EC_SUBMIT:
    case ASH_EC_PASTE_BEGIN:
    case ASH_EC_PASTE_END:
    case ASH_EC_CANCEL:
    case ASH_EC_EOF:
        break;
    }
}

static int count_digits(int n)
{
    int d = 1;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

static int gutter_width(const ash_editor *ed)
{
    if (!ed->show_gutter)
        return 0;
    int total = ash_measure_line_index(&ed->buf, ash_gapbuf_len(&ed->buf)) + 1;
    return count_digits(total) + 1;
}

int ash_editor_hit(const ash_editor *ed, int screen_x, int screen_y, size_t *off)
{
    int row = screen_y - ed->view.y;
    if (row < 0 || row >= ed->view.h)
        return 0;
    int last = ash_measure_line_index(&ed->buf, ash_gapbuf_len(&ed->buf));
    int line = ed->top_line + row;
    if (line > last)
        line = last;
    if (line < 0)
        line = 0;
    size_t ls = ash_measure_line_at(&ed->buf, line);
    int col = (screen_x - (ed->view.x + ed->gutter_w)) + ed->left_col;
    if (col < 0)
        col = 0;
    size_t hit = ash_measure_col_to_offset(&ed->buf, ls, col, ed->tab_size, NULL);
    if (off)
        *off = hit;
    return 1;
}

int ash_editor_click(ash_editor *ed, int screen_x, int screen_y, int extend)
{
    size_t off;
    if (!ash_editor_hit(ed, screen_x, screen_y, &off))
        return 0;
    ed->cursor = off;
    if (!extend)
        ed->anchor = off;
    ed->goal_col = -1;
    return 1;
}

static void put_cluster(ash_fb *fb, int x, int y, ash_style st,
                        const ash_gapbuf *b, const ash_grapheme *g)
{
    uint8_t tmp[64];
    size_t clen = g->next - g->offset;
    if (clen > sizeof tmp)
        clen = sizeof tmp;
    size_t n = ash_gapbuf_copy(b, g->offset, tmp, clen);
    ash_fb_put_text(fb, x, y, st, tmp, n);
}

static void render_line(ash_editor *ed, ash_fb *fb, int y, int tx, int tw,
                        size_t ls, size_t le, ash_style st, ash_style sel,
                        size_t ss, size_t se)
{
    const ash_gapbuf *b = &ed->buf;
    int col = 0;
    size_t p = ls;
    int left = ed->left_col;
    ash_grapheme g;
    while (p < le && ash_measure_next(b, p, col, ed->tab_size, &g)) {
        if (g.first_cp == '\n')
            break;
        int in_sel = (p >= ss && p < se);
        ash_style use = in_sel ? sel : st;
        if (g.first_cp == '\t') {
            for (int i = 0; i < g.width; i++) {
                int c = col + i;
                int x = tx + (c - left);
                if (x >= tx && x < tx + tw)
                    ash_fb_put_text(fb, x, y, use, " ", 1);
            }
        } else if (g.width > 0 && col + g.width > left && col < left + tw) {
            put_cluster(fb, tx + (col - left), y, use, b, &g);
        }
        col += g.width;
        p = g.next;
    }
}

void ash_editor_render(ash_editor *ed, ash_fb *fb, ash_rect rect, ash_style st,
                       ash_style sel)
{
    ash_gapbuf *b = &ed->buf;
    ed->view = rect;
    int h = rect.h > 0 ? rect.h : 1;
    int gw = gutter_width(ed);
    ed->gutter_w = gw;
    int tx = rect.x + gw;
    int tw = rect.w - gw;
    if (tw < 1)
        tw = 1;

    int last = ash_measure_line_index(b, ash_gapbuf_len(b));
    int cline = ash_measure_line_index(b, ed->cursor);
    int ccol = cursor_col(ed);

    if (cline < ed->top_line)
        ed->top_line = cline;
    if (cline >= ed->top_line + h)
        ed->top_line = cline - h + 1;
    int maxtop = last + 1 - h;
    if (maxtop < 0)
        maxtop = 0;
    if (ed->top_line > maxtop)
        ed->top_line = maxtop;
    if (ed->top_line < 0)
        ed->top_line = 0;

    if (ccol < ed->left_col)
        ed->left_col = ccol;
    if (ccol >= ed->left_col + tw)
        ed->left_col = ccol - tw + 1;
    if (ed->left_col < 0)
        ed->left_col = 0;

    ash_fb_fill_rect(fb, rect, st, ' ');

    size_t ss = 0, se = 0;
    ash_editor_selection(ed, &ss, &se);

    ash_style gut = st;
    gut.attr = (uint16_t)(gut.attr | ASH_ATTR_ITALIC);

    for (int vr = 0; vr < h; vr++) {
        int line = ed->top_line + vr;
        int y = rect.y + vr;
        if (line > last)
            break;
        size_t lstart = ash_measure_line_at(b, line);
        size_t lend = ash_measure_line_end(b, lstart);

        if (gw > 0) {
            char num[16];
            int nn = snprintf(num, sizeof num, "%*d ", gw - 1, line + 1);
            if (nn > 0)
                ash_fb_put_text(fb, rect.x, y, gut, num, (size_t)nn);
        }

        if (ash_fb_clip_push(fb, (ash_rect){ tx, rect.y, tw, h })) {
            render_line(ed, fb, y, tx, tw, lstart, lend, st, sel, ss, se);
            ash_fb_clip_pop(fb);
        }
    }

    int cy = rect.y + (cline - ed->top_line);
    int cx = tx + (ccol - ed->left_col);
    if (cline >= ed->top_line && cline < ed->top_line + h &&
        cx >= tx && cx < tx + tw)
        ash_fb_set_cursor(fb, cx, cy, ASH_CURSOR_BAR);
}
