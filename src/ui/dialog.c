#include "ash/ui/dialog.h"

#include <string.h>

#include "ash/base/buf.h"
#include "ash/ui/fuzzy.h"

ash_dialog_result ash_dialog_confirm(ash_ctx *c, const char *title,
                                     const char *message, const char *accept,
                                     const char *reject)
{
    ash_dialog_result r = ASH_DIALOG_NONE;
    ash_modal_begin(c, "confirm", title);
    ash_block_begin(c, "content");
    ash_inherit_focus(c);
    ash_attr_padding(c, (ash_padding){ 1, 1, 1, 1 });

    ash_label(c, "msg", message);

    ash_button_style bs = ash_button_default();
    if (ash_button(c, "accept", accept, bs))
        r = ASH_DIALOG_ACCEPT;
    ash_inherit_focus(c);
    if (ash_button(c, "reject", reject, bs))
        r = ASH_DIALOG_REJECT;
    ash_inherit_focus(c);

    ash_block_end(c);
    if (ash_modal_end(c))
        r = ASH_DIALOG_REJECT;
    return r;
}

ash_dialog_result ash_dialog_prompt(ash_ctx *c, const char *title,
                                    const char *message, ash_textarea *input,
                                    const char *accept, const char *reject)
{
    ash_dialog_result r = ASH_DIALOG_NONE;
    ash_modal_begin(c, "prompt", title);
    ash_block_begin(c, "content");
    ash_inherit_focus(c);
    ash_attr_padding(c, (ash_padding){ 1, 1, 1, 1 });

    ash_label(c, "msg", message);

    ash_block_begin(c, "field");
    ash_attr_border(c);
    ash_textarea_widget(c, "input", input);
    ash_inherit_focus(c);
    ash_block_end(c);

    ash_button_style bs = ash_button_default();
    if (ash_button(c, "accept", accept, bs))
        r = ASH_DIALOG_ACCEPT;
    ash_inherit_focus(c);
    if (ash_button(c, "reject", reject, bs))
        r = ASH_DIALOG_REJECT;
    ash_inherit_focus(c);

    ash_block_end(c);
    if (ash_modal_end(c))
        r = ASH_DIALOG_REJECT;
    return r;
}

void ash_picker_init(ash_picker *pk, ash_arena *scratch, ash_arena *query_arena,
                     const char *const *items, int nitems, int query_width)
{
    ash_textarea_init(&pk->query, query_arena, query_width, 1);
    pk->items = items;
    pk->nitems = nitems;
    pk->scratch = scratch;
    pk->sel = 0;
    pk->scroll = 0;
}

static int picker_rank(ash_picker *pk, int *ord, int cap)
{
    ash_arena_mark mk = ash_arena_mark_get(pk->scratch);
    ash_buf q;
    ash_buf_init(&q, pk->scratch);
    ash_textarea_text(&pk->query, &q);

    int n = 0;
    if (q.len == 0) {
        for (int i = 0; i < pk->nitems && n < cap; i++)
            ord[n++] = i;
        ash_arena_rewind(pk->scratch, mk);
        return n;
    }

    size_t na = pk->nitems > 0 ? (size_t)pk->nitems : 1;
    int32_t *scv = ash_array(pk->scratch, int32_t, na);
    int *idx = ash_array(pk->scratch, int, na);
    for (int i = 0; i < pk->nitems; i++) {
        ash_arena_mark im = ash_arena_mark_get(pk->scratch);
        ash_fuzzy_match m;
        int32_t s = ash_fuzzy_score(pk->scratch, pk->items[i],
                                    strlen(pk->items[i]), (const char *)q.data,
                                    q.len, 1, &m);
        ash_arena_rewind(pk->scratch, im);
        if (s > 0) {
            idx[n] = i;
            scv[n] = s;
            n++;
        }
    }
    for (int i = 1; i < n; i++) {
        int ii = idx[i];
        int32_t ss = scv[i];
        int j = i - 1;
        while (j >= 0 && scv[j] < ss) {
            idx[j + 1] = idx[j];
            scv[j + 1] = scv[j];
            j--;
        }
        idx[j + 1] = ii;
        scv[j + 1] = ss;
    }
    int outn = n < cap ? n : cap;
    for (int i = 0; i < outn; i++)
        ord[i] = idx[i];
    ash_arena_rewind(pk->scratch, mk);
    return outn;
}

int ash_picker_handle(ash_picker *pk, const ash_input_event *ev)
{
    if (ev == NULL)
        return ASH_PICKER_NONE;

    if (ev->kind == ASH_EV_KEY) {
        if (ev->key == 27)
            return ASH_PICKER_CANCEL;
        if (ev->key == ASH_KEY_UP) {
            if (pk->sel > 0)
                pk->sel--;
            return ASH_PICKER_NONE;
        }
        if (ev->key == ASH_KEY_DOWN) {
            pk->sel++;
            return ASH_PICKER_NONE;
        }
        if (ev->key == 13) {
            size_t na = pk->nitems > 0 ? (size_t)pk->nitems : 1;
            ash_arena_mark mk = ash_arena_mark_get(pk->scratch);
            int *ord = ash_array(pk->scratch, int, na);
            int n = picker_rank(pk, ord, pk->nitems);
            int result = pk->sel >= 0 && pk->sel < n ? ord[pk->sel]
                                                     : ASH_PICKER_NONE;
            ash_arena_rewind(pk->scratch, mk);
            return result;
        }
    }

    ash_key k = ash_key_map(ev);
    if (k.cmd != ASH_EC_NONE) {
        ash_textarea_apply(&pk->query, k);
        pk->sel = 0;
        pk->scroll = 0;
    }
    return ASH_PICKER_NONE;
}

static int disp_width(const char *p, size_t len)
{
    int w = 0;
    size_t i = 0;
    while (i < len) {
        uint32_t cp;
        size_t a = ash_utf8_decode(p + i, len - i, &cp);
        if (a == 0)
            break;
        int cw = ash_char_width(cp);
        if (cw > 0)
            w += cw;
        i += a;
    }
    return w;
}

static void draw_border(ash_fb *fb, ash_rect r, ash_style st)
{
    if (r.w < 2 || r.h < 2)
        return;
    int x0 = r.x, y0 = r.y, x1 = r.x + r.w - 1, y1 = r.y + r.h - 1;
    ash_fb_put_text(fb, x0, y0, st, "\xE2\x94\x8C", 3);
    ash_fb_put_text(fb, x1, y0, st, "\xE2\x94\x90", 3);
    ash_fb_put_text(fb, x0, y1, st, "\xE2\x94\x94", 3);
    ash_fb_put_text(fb, x1, y1, st, "\xE2\x94\x98", 3);
    for (int x = x0 + 1; x < x1; x++) {
        ash_fb_put_text(fb, x, y0, st, "\xE2\x94\x80", 3);
        ash_fb_put_text(fb, x, y1, st, "\xE2\x94\x80", 3);
    }
    for (int y = y0 + 1; y < y1; y++) {
        ash_fb_put_text(fb, x0, y, st, "\xE2\x94\x82", 3);
        ash_fb_put_text(fb, x1, y, st, "\xE2\x94\x82", 3);
    }
}

void ash_picker_render(ash_picker *pk, ash_fb *fb, ash_rect rect)
{
    if (rect.w < 4 || rect.h < 4)
        return;

    ash_style base = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_style sel = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, ASH_ATTR_REVERSE };

    ash_fb_fill_rect(fb, rect, base, ' ');
    draw_border(fb, rect, base);

    int ix = rect.x + 1;
    int iw = rect.w - 2;
    ash_rect inner = { ix, rect.y + 1, iw, rect.h - 2 };

    size_t na = pk->nitems > 0 ? (size_t)pk->nitems : 1;
    ash_arena_mark mk = ash_arena_mark_get(pk->scratch);
    int *ord = ash_array(pk->scratch, int, na);
    int n = picker_rank(pk, ord, pk->nitems);

    if (pk->sel >= n)
        pk->sel = n > 0 ? n - 1 : 0;
    if (pk->sel < 0)
        pk->sel = 0;

    int rows = rect.h - 4;
    if (rows < 0)
        rows = 0;
    if (pk->sel < pk->scroll)
        pk->scroll = pk->sel;
    if (rows > 0 && pk->sel >= pk->scroll + rows)
        pk->scroll = pk->sel - rows + 1;
    if (pk->scroll < 0)
        pk->scroll = 0;

    int savetop = fb->clip_top;
    if (ash_fb_clip_push(fb, inner)) {
        ash_buf q;
        ash_buf_init(&q, pk->scratch);
        ash_textarea_text(&pk->query, &q);
        int qx = ix;
        ash_fb_put_text(fb, qx, rect.y + 1, base, "> ", 2);
        qx += 2;
        ash_fb_put_text(fb, qx, rect.y + 1, base, q.data, q.len);
        int qw = disp_width((const char *)q.data, q.len);
        int cx = qx + qw;
        if (cx > ix + iw - 1)
            cx = ix + iw - 1;
        ash_fb_set_cursor(fb, cx, rect.y + 1, ASH_CURSOR_BAR);

        for (int r = 0; r < rows && r + pk->scroll < n; r++) {
            int item = ord[r + pk->scroll];
            int y = rect.y + 3 + r;
            ash_style st = (r + pk->scroll == pk->sel) ? sel : base;
            ash_rect line = { ix, y, iw, 1 };
            ash_fb_fill_rect(fb, line, st, ' ');
            ash_fb_put_text(fb, ix, y, st, pk->items[item],
                            strlen(pk->items[item]));
        }
        ash_fb_clip_pop(fb);
    }
    fb->clip_top = savetop;

    for (int x = rect.x + 1; x < rect.x + rect.w - 1; x++)
        ash_fb_put_text(fb, x, rect.y + 2, base, "\xE2\x94\x80", 3);

    ash_arena_rewind(pk->scratch, mk);
}
