#include "ash/edit/diffview.h"

#include <stdio.h>
#include <string.h>

#define LCS_CELL_CAP 4000000u

typedef struct span {
    const char *p;
    size_t      len;
} span;

static int split_lines(ash_arena *a, const char *s, size_t len, span **out)
{
    int n = 1;
    for (size_t i = 0; i < len; i++)
        if (s[i] == '\n')
            n++;
    span *v = ash_array(a, span, (size_t)n);
    int k = 0;
    size_t start = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\n') {
            v[k].p = s + start;
            v[k].len = i - start;
            k++;
            start = i + 1;
        }
    }
    v[k].p = s + start;
    v[k].len = len - start;
    *out = v;
    return n;
}

static int span_eq(span a, span b)
{
    return a.len == b.len && (a.len == 0 || memcmp(a.p, b.p, a.len) == 0);
}

typedef struct raw_op {
    ash_diff_op op;
    span        s;
} raw_op;

static int dp_at(const int *dp, int stride, int i, int j)
{
    return dp[(size_t)i * (size_t)stride + (size_t)j];
}

static void dp_set(int *dp, int stride, int i, int j, int v)
{
    dp[(size_t)i * (size_t)stride + (size_t)j] = v;
}

static void lcs_middle(ash_arena *a, const span *A, int m, const span *B, int n,
                       raw_op *out, int *pk)
{
    int k = *pk;
    if (m == 0 || n == 0 ||
        (size_t)(m + 1) * (size_t)(n + 1) > LCS_CELL_CAP) {
        for (int i = 0; i < m; i++)
            out[k++] = (raw_op){ ASH_DIFF_DEL, A[i] };
        for (int j = 0; j < n; j++)
            out[k++] = (raw_op){ ASH_DIFF_ADD, B[j] };
        *pk = k;
        return;
    }

    int stride = n + 1;
    int *dp = ash_array(a, int, (size_t)(m + 1) * (size_t)stride);
    for (int j = 0; j <= n; j++)
        dp_set(dp, stride, m, j, 0);
    for (int i = m - 1; i >= 0; i--) {
        dp_set(dp, stride, i, n, 0);
        for (int j = n - 1; j >= 0; j--) {
            if (span_eq(A[i], B[j]))
                dp_set(dp, stride, i, j, dp_at(dp, stride, i + 1, j + 1) + 1);
            else {
                int down = dp_at(dp, stride, i + 1, j);
                int right = dp_at(dp, stride, i, j + 1);
                dp_set(dp, stride, i, j, down >= right ? down : right);
            }
        }
    }

    int i = 0, j = 0;
    while (i < m && j < n) {
        if (span_eq(A[i], B[j])) {
            out[k++] = (raw_op){ ASH_DIFF_EQ, A[i] };
            i++;
            j++;
        } else if (dp_at(dp, stride, i + 1, j) >= dp_at(dp, stride, i, j + 1)) {
            out[k++] = (raw_op){ ASH_DIFF_DEL, A[i] };
            i++;
        } else {
            out[k++] = (raw_op){ ASH_DIFF_ADD, B[j] };
            j++;
        }
    }
    while (i < m)
        out[k++] = (raw_op){ ASH_DIFF_DEL, A[i++] };
    while (j < n)
        out[k++] = (raw_op){ ASH_DIFF_ADD, B[j++] };
    *pk = k;
}

void ash_diff_compute(ash_arena *a, const char *old, size_t oldlen,
                      const char *neu, size_t newlen, ash_diff *out)
{
    span *A, *B;
    int am = split_lines(a, old, oldlen, &A);
    int bm = split_lines(a, neu, newlen, &B);

    int p = 0;
    while (p < am && p < bm && span_eq(A[p], B[p]))
        p++;
    int s = 0;
    while (s < am - p && s < bm - p && span_eq(A[am - 1 - s], B[bm - 1 - s]))
        s++;

    raw_op *raw = ash_array(a, raw_op, (size_t)(am + bm));
    int k = 0;
    for (int i = 0; i < p; i++)
        raw[k++] = (raw_op){ ASH_DIFF_EQ, A[i] };
    lcs_middle(a, A + p, am - s - p, B + p, bm - s - p, raw, &k);
    for (int i = 0; i < s; i++)
        raw[k++] = (raw_op){ ASH_DIFF_EQ, A[am - s + i] };

    ash_diff_line *lines = ash_array(a, ash_diff_line, (size_t)k);
    int on = 1, nn = 1, adds = 0, dels = 0;
    for (int idx = 0; idx < k; idx++) {
        ash_diff_line *l = &lines[idx];
        l->op = raw[idx].op;
        l->text = raw[idx].s.p;
        l->len = raw[idx].s.len;
        switch (raw[idx].op) {
        case ASH_DIFF_EQ:
            l->old_no = on++;
            l->new_no = nn++;
            break;
        case ASH_DIFF_DEL:
            l->old_no = on++;
            l->new_no = 0;
            dels++;
            break;
        case ASH_DIFF_ADD:
            l->old_no = 0;
            l->new_no = nn++;
            adds++;
            break;
        }
    }
    out->lines = lines;
    out->count = k;
    out->additions = adds;
    out->deletions = dels;
}

void ash_diffview_init(ash_diffview *dv, ash_arena *arena, const char *path)
{
    dv->arena = arena;
    dv->path = path ? path : "";
    dv->diff.lines = NULL;
    dv->diff.count = 0;
    dv->diff.additions = 0;
    dv->diff.deletions = 0;
    dv->context = 3;
    dv->scroll = 0;
    dv->gutter_w = 0;
}

void ash_diffview_set(ash_diffview *dv, const char *old, size_t oldlen,
                      const char *neu, size_t newlen)
{
    ash_diff_compute(dv->arena, old, oldlen, neu, newlen, &dv->diff);
    dv->scroll = 0;
}

int ash_diffview_propose(ash_diffview *dv, const char *content, size_t clen,
                         const ash_edit_spec *edits, int ne, const char **err)
{
    size_t newlen = 0;
    const char *neu = ash_edit_apply(dv->arena, content, clen, edits, ne,
                                     &newlen, err);
    if (neu == NULL)
        return 0;
    ash_diffview_set(dv, content, clen, neu, newlen);
    return 1;
}

typedef struct disp {
    int                  gap;
    const ash_diff_line *line;
} disp;

static int build_display(ash_diffview *dv, disp **out)
{
    int n = dv->diff.count;
    if (n == 0) {
        *out = NULL;
        return 0;
    }
    int ctx = dv->context < 0 ? 0 : dv->context;
    uint8_t *show = ash_array(dv->arena, uint8_t, (size_t)n);
    memset(show, 0, (size_t)n);
    for (int i = 0; i < n; i++) {
        if (dv->diff.lines[i].op == ASH_DIFF_EQ)
            continue;
        int lo = i - ctx < 0 ? 0 : i - ctx;
        int hi = i + ctx >= n ? n - 1 : i + ctx;
        for (int j = lo; j <= hi; j++)
            show[j] = 1;
    }

    disp *items = ash_array(dv->arena, disp, (size_t)(2 * n + 1));
    int ni = 0;
    int hidden = 0;
    for (int i = 0; i < n; i++) {
        if (show[i]) {
            if (hidden) {
                items[ni].gap = 1;
                items[ni].line = NULL;
                ni++;
                hidden = 0;
            }
            items[ni].gap = 0;
            items[ni].line = &dv->diff.lines[i];
            ni++;
        } else {
            hidden = 1;
        }
    }
    *out = items;
    return ni;
}

static int count_digits(int n)
{
    int d = 1;
    if (n < 0)
        n = 0;
    while (n >= 10) {
        n /= 10;
        d++;
    }
    return d;
}

ash_diffview_action ash_diffview_key(ash_diffview *dv, ash_key k, int view_h)
{
    int body = view_h - 2;
    if (body < 1)
        body = 1;
    if (k.cmd == ASH_EC_UP) {
        dv->scroll--;
    } else if (k.cmd == ASH_EC_DOWN) {
        dv->scroll++;
    } else if (k.cmd == ASH_EC_DOC_HOME) {
        dv->scroll = 0;
    } else if (k.cmd == ASH_EC_DOC_END) {
        dv->scroll += body;
    } else if (k.cmd == ASH_EC_SUBMIT) {
        return ASH_DIFFVIEW_ACCEPT;
    } else if (k.cmd == ASH_EC_CANCEL) {
        return ASH_DIFFVIEW_REJECT;
    } else if (k.cmd == ASH_EC_INSERT && k.len == 1) {
        char c = k.text[0];
        if (c == 'a' || c == 'A')
            return ASH_DIFFVIEW_ACCEPT;
        if (c == 'r' || c == 'R')
            return ASH_DIFFVIEW_REJECT;
        if (c == 'e' || c == 'E')
            return ASH_DIFFVIEW_EDIT;
    }
    if (dv->scroll < 0)
        dv->scroll = 0;
    return ASH_DIFFVIEW_NONE;
}

static void fill_row(ash_fb *fb, int x, int y, int w, ash_style st)
{
    ash_fb_fill_rect(fb, (ash_rect){ x, y, w, 1 }, st, ' ');
}

void ash_diffview_render(ash_diffview *dv, ash_fb *fb, ash_rect rect,
                         const ash_diffview_theme *theme)
{
    if (rect.w < 1 || rect.h < 1)
        return;

    disp *items;
    int ni = build_display(dv, &items);

    int maxno = 1;
    for (int i = 0; i < dv->diff.count; i++) {
        if (dv->diff.lines[i].old_no > maxno)
            maxno = dv->diff.lines[i].old_no;
        if (dv->diff.lines[i].new_no > maxno)
            maxno = dv->diff.lines[i].new_no;
    }
    int gw = count_digits(maxno);
    dv->gutter_w = gw;

    int header_y = rect.y;
    int hint_y = rect.y + rect.h - 1;
    int body_top = rect.y + 1;
    int body_h = rect.h - 2;
    if (body_h < 0)
        body_h = 0;

    ash_fb_fill_rect(fb, rect, theme->context, ' ');

    fill_row(fb, rect.x, header_y, rect.w, theme->header);
    char head[256];
    int hn = snprintf(head, sizeof head, " %s   +%d -%d", dv->path,
                      dv->diff.additions, dv->diff.deletions);
    if (hn > 0) {
        if (ash_fb_clip_push(fb, (ash_rect){ rect.x, header_y, rect.w, 1 })) {
            ash_fb_put_text(fb, rect.x, header_y, theme->header, head,
                            (size_t)hn);
            ash_fb_clip_pop(fb);
        }
    }

    int maxscroll = ni - body_h;
    if (maxscroll < 0)
        maxscroll = 0;
    if (dv->scroll > maxscroll)
        dv->scroll = maxscroll;
    if (dv->scroll < 0)
        dv->scroll = 0;

    if (ash_fb_clip_push(fb, (ash_rect){ rect.x, body_top, rect.w, body_h })) {
        for (int vr = 0; vr < body_h; vr++) {
            int idx = dv->scroll + vr;
            if (idx >= ni)
                break;
            int y = body_top + vr;
            disp *it = &items[idx];
            if (it->gap) {
                char pre[32];
                int pn = snprintf(pre, sizeof pre, " %*s ...", gw, "");
                if (pn > 0)
                    ash_fb_put_text(fb, rect.x, y, theme->gutter, pre,
                                    (size_t)pn);
                continue;
            }
            const ash_diff_line *l = it->line;
            ash_style row = l->op == ASH_DIFF_ADD ? theme->add
                          : l->op == ASH_DIFF_DEL ? theme->del
                                                  : theme->context;
            char marker = l->op == ASH_DIFF_ADD ? '+'
                        : l->op == ASH_DIFF_DEL ? '-'
                                                : ' ';
            int no = l->op == ASH_DIFF_ADD ? l->new_no : l->old_no;
            fill_row(fb, rect.x, y, rect.w, row);
            char pre[32];
            int pn = snprintf(pre, sizeof pre, "%c%*d ", marker, gw, no);
            if (pn > 0)
                ash_fb_put_text(fb, rect.x, y, row, pre, (size_t)pn);
            ash_fb_put_text(fb, rect.x + (pn > 0 ? pn : 0), y, row, l->text,
                            l->len);
        }
        ash_fb_clip_pop(fb);
    }

    fill_row(fb, rect.x, hint_y, rect.w, theme->hint);
    const char *hint = " [A]ccept  [E]dit  [R]eject";
    if (ash_fb_clip_push(fb, (ash_rect){ rect.x, hint_y, rect.w, 1 })) {
        ash_fb_put_text(fb, rect.x, hint_y, theme->hint, hint, strlen(hint));
        ash_fb_clip_pop(fb);
    }
}
