#include "ash/ui/textarea.h"

#include <stdio.h>
#include <string.h>

#include "ash/base/poison.h"

enum { TA_G_NONE = 0, TA_G_INSERT, TA_G_DELETE, TA_G_DELWORD, TA_G_KILL };

static void ta_reset_undo(ash_textarea *ta);

void ash_textarea_init(ash_textarea *ta, ash_arena *arena, int width, int max_rows)
{
    ta->arena = arena;
    ta->data = NULL;
    ta->len = 0;
    ta->cap = 0;
    ta->cursor = 0;
    ta->sel = 0;
    ta->sel_on = 0;
    ta->width = width > 0 ? width : 1;
    ta->max_rows = max_rows;
    ta->scroll = 0;
    memset(ta->undo, 0, sizeof ta->undo);
    ta->undo_len = 0;
    ta->undo_cur = 0;
    ta->undo_group = TA_G_NONE;
    ta_reset_undo(ta);
}

void ash_textarea_set_width(ash_textarea *ta, int width)
{
    ta->width = width > 0 ? width : 1;
}

void ash_textarea_clear(ash_textarea *ta)
{
    ta->len = 0;
    ta->cursor = 0;
    ta->scroll = 0;
    ta->sel_on = 0;
    ta_reset_undo(ta);
}

size_t ash_textarea_len(const ash_textarea *ta)
{
    return ta->len;
}

size_t ash_textarea_cursor(const ash_textarea *ta)
{
    return ta->cursor;
}

int ash_textarea_at_end(const ash_textarea *ta)
{
    return ta->cursor == ta->len;
}

size_t ash_textarea_text(const ash_textarea *ta, ash_buf *out)
{
    if (ta->len)
        ash_buf_append(out, ta->data, ta->len);
    return ta->len;
}

static void ta_reserve(ash_textarea *ta, size_t need)
{
    if (need <= ta->cap)
        return;

    size_t want = ta->cap ? ta->cap : 64;
    while (want < need) {
        size_t next = want * 2;
        if (next < want) {
            want = need;
            break;
        }
        want = next;
    }

    uint8_t *nd = ash_arena_alloc(ta->arena, want, 16);
    if (ta->len)
        memcpy(nd, ta->data, ta->len);
    ta->data = nd;
    ta->cap = want;
}

static void ta_ensure(ash_textarea *ta, size_t extra)
{
    size_t need = ta->len + extra;
    if (need < ta->len)
        ash_die("textarea overflow: len %zu + %zu", ta->len, extra);
    ta_reserve(ta, need);
}

static void snap_store(ash_textarea *ta, ash_ta_snap *s)
{
    if (s->cap < ta->len) {
        size_t want = s->cap ? s->cap : 64;
        while (want < ta->len) {
            size_t next = want * 2;
            if (next < want) {
                want = ta->len;
                break;
            }
            want = next;
        }
        s->data = ash_arena_alloc(ta->arena, want, 16);
        s->cap = want;
    }
    if (ta->len)
        memcpy(s->data, ta->data, ta->len);
    s->len = ta->len;
    s->cursor = ta->cursor;
}

static void snap_load(ash_textarea *ta, const ash_ta_snap *s)
{
    ta_reserve(ta, s->len);
    if (s->len)
        memcpy(ta->data, s->data, s->len);
    ta->len = s->len;
    ta->cursor = s->cursor <= s->len ? s->cursor : s->len;
    ta->sel_on = 0;
}

static void ta_reset_undo(ash_textarea *ta)
{
    ta->undo_len = 1;
    ta->undo_cur = 0;
    ta->undo_group = TA_G_NONE;
    snap_store(ta, &ta->undo[0]);
}

static void ta_commit(ash_textarea *ta, int kind)
{
    if (kind != TA_G_NONE && kind == ta->undo_group &&
        ta->undo_cur == ta->undo_len - 1) {
        snap_store(ta, &ta->undo[ta->undo_cur]);
        return;
    }
    if (ta->undo_cur + 1 >= ASH_TA_UNDO_CAP) {
        ash_ta_snap first = ta->undo[0];
        memmove(ta->undo, ta->undo + 1,
                sizeof(ash_ta_snap) * (ASH_TA_UNDO_CAP - 1));
        ta->undo[ASH_TA_UNDO_CAP - 1] = first;
        ta->undo_cur--;
    }
    ta->undo_cur++;
    ta->undo_len = ta->undo_cur + 1;
    snap_store(ta, &ta->undo[ta->undo_cur]);
    ta->undo_group = kind;
}

static void ta_do_undo(ash_textarea *ta)
{
    if (ta->undo_cur <= 0)
        return;
    ta->undo_cur--;
    snap_load(ta, &ta->undo[ta->undo_cur]);
    ta->undo_group = TA_G_NONE;
}

static void ta_do_redo(ash_textarea *ta)
{
    if (ta->undo_cur + 1 >= ta->undo_len)
        return;
    ta->undo_cur++;
    snap_load(ta, &ta->undo[ta->undo_cur]);
    ta->undo_group = TA_G_NONE;
}

static int ta_has_sel(const ash_textarea *ta)
{
    return ta->sel_on && ta->sel != ta->cursor;
}

static void sel_range(const ash_textarea *ta, size_t *lo, size_t *hi)
{
    if (ta->sel <= ta->cursor) {
        *lo = ta->sel;
        *hi = ta->cursor;
    } else {
        *lo = ta->cursor;
        *hi = ta->sel;
    }
}

static void del_sel(ash_textarea *ta)
{
    if (!ta_has_sel(ta)) {
        ta->sel_on = 0;
        return;
    }
    size_t lo, hi;
    sel_range(ta, &lo, &hi);
    memmove(ta->data + lo, ta->data + hi, ta->len - hi);
    ta->len -= hi - lo;
    ta->cursor = lo;
    ta->sel_on = 0;
}

static size_t prev_char(const ash_textarea *ta, size_t i)
{
    if (i == 0)
        return 0;
    size_t k = i;
    do {
        k--;
    } while (k > 0 && (ta->data[k] & 0xC0) == 0x80);
    return k;
}

static size_t next_char(const ash_textarea *ta, size_t i)
{
    if (i >= ta->len)
        return ta->len;
    size_t k = i + 1;
    while (k < ta->len && (ta->data[k] & 0xC0) == 0x80)
        k++;
    return k;
}

void ash_textarea_insert(ash_textarea *ta, const void *utf8, size_t len)
{
    if (len == 0)
        return;
    if (ta_has_sel(ta))
        del_sel(ta);
    ta_ensure(ta, len);
    memmove(ta->data + ta->cursor + len, ta->data + ta->cursor,
            ta->len - ta->cursor);
    memcpy(ta->data + ta->cursor, utf8, len);
    ta->len += len;
    ta->cursor += len;
    ta_commit(ta, TA_G_INSERT);
}

void ash_textarea_set(ash_textarea *ta, const void *utf8, size_t len)
{
    ta->len = 0;
    ta->cursor = 0;
    ta->scroll = 0;
    ta->sel_on = 0;
    if (len)
        ash_textarea_insert(ta, utf8, len);
    ta_reset_undo(ta);
}

int ash_textarea_backspace(ash_textarea *ta)
{
    if (ta_has_sel(ta)) {
        del_sel(ta);
        ta_commit(ta, TA_G_DELETE);
        return 0;
    }
    if (ta->cursor == 0)
        return 0;
    size_t k = prev_char(ta, ta->cursor);
    uint32_t cp = 0;
    ash_utf8_decode(ta->data + k, ta->cursor - k, &cp);
    int w = ash_char_width(cp);
    if (w < 0)
        w = 0;
    memmove(ta->data + k, ta->data + ta->cursor, ta->len - ta->cursor);
    ta->len -= ta->cursor - k;
    ta->cursor = k;
    ta_commit(ta, TA_G_DELETE);
    return w;
}

int ash_textarea_has_selection(const ash_textarea *ta)
{
    return ta_has_sel(ta);
}

size_t ash_textarea_selection(const ash_textarea *ta, ash_buf *out)
{
    if (!ta_has_sel(ta))
        return 0;
    size_t lo, hi;
    sel_range(ta, &lo, &hi);
    ash_buf_append(out, ta->data + lo, hi - lo);
    return hi - lo;
}

void ash_textarea_delete_selection(ash_textarea *ta)
{
    if (!ta_has_sel(ta))
        return;
    del_sel(ta);
    ta_commit(ta, TA_G_DELETE);
}

static void del_forward(ash_textarea *ta)
{
    if (ta->cursor >= ta->len)
        return;
    size_t e = next_char(ta, ta->cursor);
    memmove(ta->data + ta->cursor, ta->data + e, ta->len - e);
    ta->len -= e - ta->cursor;
}

static int is_ws(uint8_t b)
{
    return b == ' ' || b == '\t' || b == '\n';
}

static void word_left(ash_textarea *ta)
{
    size_t c = ta->cursor;
    while (c > 0 && is_ws(ta->data[prev_char(ta, c)]))
        c = prev_char(ta, c);
    while (c > 0 && !is_ws(ta->data[prev_char(ta, c)]))
        c = prev_char(ta, c);
    ta->cursor = c;
}

static void word_right(ash_textarea *ta)
{
    size_t c = ta->cursor;
    while (c < ta->len && is_ws(ta->data[c]))
        c = next_char(ta, c);
    while (c < ta->len && !is_ws(ta->data[c]))
        c = next_char(ta, c);
    ta->cursor = c;
}

static void del_word_back(ash_textarea *ta)
{
    size_t old = ta->cursor;
    word_left(ta);
    size_t nc = ta->cursor;
    if (old == nc)
        return;
    memmove(ta->data + nc, ta->data + old, ta->len - old);
    ta->len -= old - nc;
}

static void del_word_fwd(ash_textarea *ta)
{
    size_t old = ta->cursor;
    word_right(ta);
    size_t e = ta->cursor;
    ta->cursor = old;
    if (e == old)
        return;
    memmove(ta->data + old, ta->data + e, ta->len - e);
    ta->len -= e - old;
}

static size_t line_start(const ash_textarea *ta, size_t c)
{
    while (c > 0 && ta->data[c - 1] != '\n')
        c--;
    return c;
}

static size_t line_end(const ash_textarea *ta, size_t c)
{
    while (c < ta->len && ta->data[c] != '\n')
        c++;
    return c;
}

static void kill_to_end(ash_textarea *ta)
{
    size_t e = line_end(ta, ta->cursor);
    if (e == ta->cursor)
        return;
    memmove(ta->data + ta->cursor, ta->data + e, ta->len - e);
    ta->len -= e - ta->cursor;
}

static void kill_line(ash_textarea *ta)
{
    size_t s = line_start(ta, ta->cursor);
    size_t e = line_end(ta, ta->cursor);
    ta->cursor = s;
    if (e == s)
        return;
    memmove(ta->data + s, ta->data + e, ta->len - e);
    ta->len -= e - s;
}

typedef void (*ta_rowcb)(void *ud, int row, size_t off, size_t end, int cols);

static int ta_layout(const ash_textarea *ta, ta_rowcb cb, void *ud)
{
    const uint8_t *d = ta->data;
    size_t len = ta->len;
    int width = ta->width > 0 ? ta->width : 1;
    int row = 0;
    size_t rowstart = 0;
    int col = 0;
    size_t brk = 0;
    int brkcol = 0;
    size_t i = 0;

    while (i < len) {
        uint8_t b = d[i];
        if (b == '\n') {
            if (cb)
                cb(ud, row, rowstart, i, col);
            row++;
            i++;
            rowstart = i;
            col = 0;
            brk = 0;
            continue;
        }

        uint32_t cp = 0;
        size_t adv = ash_utf8_decode(d + i, len - i, &cp);
        if (adv == 0)
            adv = 1;
        int w = ash_char_width(cp);
        if (w < 0)
            w = 0;

        if (col > 0 && col + w > width) {
            if (brk > rowstart) {
                if (cb)
                    cb(ud, row, rowstart, brk, brkcol);
                col -= brkcol;
                rowstart = brk;
            } else {
                if (cb)
                    cb(ud, row, rowstart, i, col);
                col = 0;
                rowstart = i;
            }
            row++;
            brk = 0;
        }

        col += w;
        i += adv;

        if (adv == 1 && (b == ' ' || b == '\t')) {
            brk = i;
            brkcol = col;
        }
    }

    if (cb)
        cb(ud, row, rowstart, len, col);
    return row + 1;
}

int ash_textarea_rows(const ash_textarea *ta)
{
    return ta_layout(ta, NULL, NULL);
}

static int col_between(const ash_textarea *ta, size_t a, size_t b)
{
    int c = 0;
    size_t i = a;
    while (i < b) {
        uint32_t cp = 0;
        size_t adv = ash_utf8_decode(ta->data + i, b - i, &cp);
        if (adv == 0)
            adv = 1;
        int w = ash_char_width(cp);
        if (w < 0)
            w = 0;
        c += w;
        i += adv;
    }
    return c;
}

struct rc_ctx {
    const ash_textarea *ta;
    size_t              cursor;
    int                 found;
    int                 row;
    int                 col;
    int                 last_row;
    int                 last_cols;
};

static void rc_cb(void *ud, int row, size_t off, size_t end, int cols)
{
    struct rc_ctx *c = ud;
    c->last_row = row;
    c->last_cols = cols;
    if (c->found)
        return;

    const ash_textarea *ta = c->ta;
    int owns = c->cursor >= off && c->cursor < end;
    if (!owns && c->cursor == end &&
        (end == ta->len || ta->data[end] == '\n'))
        owns = 1;
    if (owns) {
        c->row = row;
        c->col = col_between(ta, off, c->cursor);
        c->found = 1;
    }
}

void ash_textarea_cursor_rc(const ash_textarea *ta, int *row, int *col)
{
    struct rc_ctx c = { ta, ta->cursor, 0, 0, 0, 0, 0 };
    ta_layout(ta, rc_cb, &c);
    if (!c.found) {
        c.row = c.last_row;
        c.col = c.last_cols;
    }
    if (row)
        *row = c.row;
    if (col)
        *col = c.col;
}

struct at_ctx {
    const ash_textarea *ta;
    int                 trow;
    int                 tcol;
    size_t              result;
    int                 done;
};

static void at_cb(void *ud, int row, size_t off, size_t end, int cols)
{
    (void)cols;
    struct at_ctx *c = ud;
    if (c->done || row != c->trow)
        return;

    int cc = 0;
    size_t i = off;
    size_t res = off;
    while (i < end) {
        uint32_t cp = 0;
        size_t adv = ash_utf8_decode(c->ta->data + i, end - i, &cp);
        if (adv == 0)
            adv = 1;
        int w = ash_char_width(cp);
        if (w < 0)
            w = 0;
        if (cc + w > c->tcol)
            break;
        cc += w;
        i += adv;
        res = i;
    }
    c->result = res;
    c->done = 1;
}

static int move_row(ash_textarea *ta, int delta)
{
    int r = 0, c = 0;
    ash_textarea_cursor_rc(ta, &r, &c);
    int rows = ash_textarea_rows(ta);
    int t = r + delta;
    if (t < 0 || t >= rows)
        return 0;
    struct at_ctx a = { ta, t, c, ta->cursor, 0 };
    ta_layout(ta, at_cb, &a);
    ta->cursor = a.result;
    return 1;
}

static void sel_start(ash_textarea *ta)
{
    if (!ta->sel_on) {
        ta->sel = ta->cursor;
        ta->sel_on = 1;
    }
    ta->undo_group = TA_G_NONE;
}

static void move_plain(ash_textarea *ta)
{
    ta->sel_on = 0;
    ta->undo_group = TA_G_NONE;
}

void ash_textarea_apply(ash_textarea *ta, ash_key k)
{
    switch (k.cmd) {
    case ASH_EC_INSERT:
    case ASH_EC_PASTE_CHUNK:
        ash_textarea_insert(ta, k.text, k.len);
        break;
    case ASH_EC_NEWLINE:
        ash_textarea_insert(ta, "\n", 1);
        break;
    case ASH_EC_BACKSPACE:
        (void)ash_textarea_backspace(ta);
        break;
    case ASH_EC_DELETE:
        if (ta_has_sel(ta))
            del_sel(ta);
        else
            del_forward(ta);
        ta_commit(ta, TA_G_DELETE);
        break;
    case ASH_EC_BACKSPACE_WORD:
        if (ta_has_sel(ta))
            del_sel(ta);
        else
            del_word_back(ta);
        ta_commit(ta, TA_G_DELWORD);
        break;
    case ASH_EC_DELETE_WORD:
        if (ta_has_sel(ta))
            del_sel(ta);
        else
            del_word_fwd(ta);
        ta_commit(ta, TA_G_DELWORD);
        break;
    case ASH_EC_KILL_TO_END:
        if (ta_has_sel(ta))
            del_sel(ta);
        else
            kill_to_end(ta);
        ta_commit(ta, TA_G_KILL);
        break;
    case ASH_EC_KILL_LINE:
        if (ta_has_sel(ta))
            del_sel(ta);
        else
            kill_line(ta);
        ta_commit(ta, TA_G_KILL);
        break;
    case ASH_EC_LEFT:
        if (ta->sel_on) {
            size_t lo, hi;
            sel_range(ta, &lo, &hi);
            ta->cursor = lo;
        } else {
            ta->cursor = prev_char(ta, ta->cursor);
        }
        move_plain(ta);
        break;
    case ASH_EC_RIGHT:
        if (ta->sel_on) {
            size_t lo, hi;
            sel_range(ta, &lo, &hi);
            ta->cursor = hi;
        } else {
            ta->cursor = next_char(ta, ta->cursor);
        }
        move_plain(ta);
        break;
    case ASH_EC_WORD_LEFT:
        move_plain(ta);
        word_left(ta);
        break;
    case ASH_EC_WORD_RIGHT:
        move_plain(ta);
        word_right(ta);
        break;
    case ASH_EC_HOME:
        move_plain(ta);
        ta->cursor = line_start(ta, ta->cursor);
        break;
    case ASH_EC_END:
        move_plain(ta);
        ta->cursor = line_end(ta, ta->cursor);
        break;
    case ASH_EC_DOC_HOME:
        move_plain(ta);
        ta->cursor = 0;
        break;
    case ASH_EC_DOC_END:
        move_plain(ta);
        ta->cursor = ta->len;
        break;
    case ASH_EC_UP:
        move_plain(ta);
        (void)move_row(ta, -1);
        break;
    case ASH_EC_DOWN:
        move_plain(ta);
        (void)move_row(ta, +1);
        break;
    case ASH_EC_SELECT_LEFT:
        sel_start(ta);
        ta->cursor = prev_char(ta, ta->cursor);
        break;
    case ASH_EC_SELECT_RIGHT:
        sel_start(ta);
        ta->cursor = next_char(ta, ta->cursor);
        break;
    case ASH_EC_SELECT_WORD_LEFT:
        sel_start(ta);
        word_left(ta);
        break;
    case ASH_EC_SELECT_WORD_RIGHT:
        sel_start(ta);
        word_right(ta);
        break;
    case ASH_EC_SELECT_UP:
        sel_start(ta);
        (void)move_row(ta, -1);
        break;
    case ASH_EC_SELECT_DOWN:
        sel_start(ta);
        (void)move_row(ta, +1);
        break;
    case ASH_EC_SELECT_HOME:
        sel_start(ta);
        ta->cursor = line_start(ta, ta->cursor);
        break;
    case ASH_EC_SELECT_END:
        sel_start(ta);
        ta->cursor = line_end(ta, ta->cursor);
        break;
    case ASH_EC_SELECT_DOC_HOME:
        sel_start(ta);
        ta->cursor = 0;
        break;
    case ASH_EC_SELECT_DOC_END:
        sel_start(ta);
        ta->cursor = ta->len;
        break;
    case ASH_EC_SELECT_ALL:
        ta->sel = 0;
        ta->sel_on = 1;
        ta->cursor = ta->len;
        ta->undo_group = TA_G_NONE;
        break;
    case ASH_EC_UNDO:
        ta_do_undo(ta);
        break;
    case ASH_EC_REDO:
        ta_do_redo(ta);
        break;
    case ASH_EC_NONE:
    case ASH_EC_SUBMIT:
    case ASH_EC_COPY:
    case ASH_EC_CUT:
    case ASH_EC_PASTE:
    case ASH_EC_PASTE_BEGIN:
    case ASH_EC_PASTE_END:
    case ASH_EC_CANCEL:
    case ASH_EC_EOF:
        break;
    }
}

int ash_textarea_height(const ash_textarea *ta)
{
    int rows = ash_textarea_rows(ta);
    if (ta->max_rows > 0 && rows > ta->max_rows)
        return ta->max_rows;
    return rows;
}

struct draw_ctx {
    const ash_textarea *ta;
    ash_fb             *fb;
    ash_rect            rect;
    int                 scroll;
    int                 h;
    ash_style           st;
    int                 has_sel;
    size_t              sel_lo;
    size_t              sel_hi;
};

static void draw_cb(void *ud, int row, size_t off, size_t end, int cols)
{
    (void)cols;
    struct draw_ctx *c = ud;
    int vr = row - c->scroll;
    if (vr < 0 || vr >= c->h)
        return;
    ash_fb_put_text(c->fb, c->rect.x, c->rect.y + vr, c->st,
                    c->ta->data + off, end - off);

    if (!c->has_sel)
        return;
    size_t a = off > c->sel_lo ? off : c->sel_lo;
    size_t b = end < c->sel_hi ? end : c->sel_hi;
    if (a >= b)
        return;
    int sc = col_between(c->ta, off, a);
    int w = col_between(c->ta, a, b);
    if (w > 0)
        ash_fb_style_range(c->fb, c->rect.x + sc, c->rect.y + vr, w,
                           ash_selection_style());
}

void ash_textarea_render(ash_textarea *ta, ash_fb *fb, ash_rect rect, ash_style st)
{
    if (rect.w > 0)
        ta->width = rect.w;
    int h = rect.h > 0 ? rect.h : 1;

    int cr = 0, cc = 0;
    ash_textarea_cursor_rc(ta, &cr, &cc);
    int rows = ash_textarea_rows(ta);

    if (cr < ta->scroll)
        ta->scroll = cr;
    if (cr >= ta->scroll + h)
        ta->scroll = cr - h + 1;
    int maxscroll = rows > h ? rows - h : 0;
    if (ta->scroll > maxscroll)
        ta->scroll = maxscroll;
    if (ta->scroll < 0)
        ta->scroll = 0;

    struct draw_ctx d = { ta, fb, rect, ta->scroll, h, st, 0, 0, 0 };
    if (ta_has_sel(ta)) {
        d.has_sel = 1;
        sel_range(ta, &d.sel_lo, &d.sel_hi);
    }
    ta_layout(ta, draw_cb, &d);

    int vr = cr - ta->scroll;
    if (vr >= 0 && vr < h)
        ash_fb_set_cursor(fb, rect.x + cc, rect.y + vr, ASH_CURSOR_BAR);
}

int ash_input_rule_hidden(ash_input_rule r, int below)
{
    int n = below ? r.total_rows - r.first_visible - r.visible_rows
                  : r.first_visible;
    return n > 0 ? n : 0;
}

void ash_input_rule_text(ash_input_rule r, int below, ash_buf *out)
{
    int width = r.width > 0 ? r.width : 0;
    int hidden = ash_input_rule_hidden(r, below);
    int col = 0;

    if (hidden > 0) {
        char label[64];
        const char *arrow = below ? "\xe2\x86\x93" : "\xe2\x86\x91";
        int n = snprintf(label, sizeof label,
                         "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80 %s %d more ",
                         arrow, hidden);
        size_t i = 0;
        while (n > 0 && i < (size_t)n && col < width) {
            uint32_t cp = 0;
            size_t adv = ash_utf8_decode(label + i, (size_t)n - i, &cp);
            if (adv == 0)
                adv = 1;
            int cw = ash_char_width(cp);
            if (cw < 0)
                cw = 0;
            if (cw > 0 && col + cw > width)
                break;
            ash_buf_append(out, label + i, adv);
            col += cw;
            i += adv;
        }
    }

    while (col < width) {
        ash_buf_append(out, "\xe2\x94\x80", 3);
        col++;
    }
}

ash_style ash_input_border_style(void)
{
    ash_style s = { ash_rgb(128, 128, 128), ASH_RGBA_DEFAULT, ASH_ATTR_NONE };
    return s;
}

void ash_input_bar_render(ash_textarea *ta, ash_fb *fb, ash_rect rect,
                          ash_style text_st, ash_style border_st,
                          ash_buf *scratch)
{
    if (rect.w <= 0 || rect.h <= 0)
        return;

    int pad = rect.w >= 3 ? 1 : 0;
    int inner_w = rect.w - 2 * pad;
    if (inner_w < 1)
        inner_w = 1;
    int inner_h = rect.h - 2;
    if (inner_h < 1)
        inner_h = 1;

    ash_rect inner = { rect.x + pad, rect.y + 1, inner_w, inner_h };
    ash_textarea_render(ta, fb, inner, text_st);

    ash_input_rule r = { ta->scroll, inner_h, ash_textarea_rows(ta), rect.w };

    scratch->len = 0;
    ash_input_rule_text(r, 0, scratch);
    ash_fb_put_text(fb, rect.x, rect.y, border_st, scratch->data, scratch->len);

    scratch->len = 0;
    ash_input_rule_text(r, 1, scratch);
    ash_fb_put_text(fb, rect.x, rect.y + rect.h - 1, border_st,
                    scratch->data, scratch->len);
}

void ash_history_init(ash_history *h, ash_arena *arena, size_t cap)
{
    h->arena = arena;
    h->cap = cap ? cap : 1;
    h->items = ash_array(arena, const char *, h->cap);
    h->lens = ash_array(arena, uint32_t, h->cap);
    h->count = 0;
    h->pos = 0;
}

void ash_history_push(ash_history *h, const void *text, size_t len)
{
    if (len == 0) {
        h->pos = (long)h->count;
        return;
    }
    char *c = ash_array(h->arena, char, len + 1);
    memcpy(c, text, len);
    c[len] = 0;
    size_t slot = h->count % h->cap;
    h->items[slot] = c;
    h->lens[slot] = (uint32_t)len;
    h->count++;
    h->pos = (long)h->count;
}

void ash_history_reset(ash_history *h)
{
    h->pos = (long)h->count;
}

const char *ash_history_prev(ash_history *h, size_t *len)
{
    size_t avail = h->count < h->cap ? h->count : h->cap;
    if (avail == 0)
        return NULL;
    long lo = (long)h->count - (long)avail;
    long p = h->pos;
    if (p > (long)h->count)
        p = (long)h->count;
    if (p <= lo)
        return NULL;
    p--;
    h->pos = p;
    size_t slot = (size_t)p % h->cap;
    if (len)
        *len = h->lens[slot];
    return h->items[slot];
}

const char *ash_history_next(ash_history *h, size_t *len)
{
    if (h->pos >= (long)h->count) {
        if (len)
            *len = 0;
        return NULL;
    }
    h->pos++;
    if (h->pos >= (long)h->count) {
        if (len)
            *len = 0;
        return NULL;
    }
    size_t slot = (size_t)h->pos % h->cap;
    if (len)
        *len = h->lens[slot];
    return h->items[slot];
}
