#include "ash/app/select.h"

void ash_sel_clear(ash_selection *s)
{
    s->active = 0;
    s->anchor_seq = 0;
    s->anchor_col = 0;
    s->point_seq = 0;
    s->point_col = 0;
}

int ash_sel_empty(const ash_selection *s)
{
    return !s->active ||
           (s->anchor_seq == s->point_seq && s->anchor_col == s->point_col);
}

static int before(uint64_t sa, int ca, uint64_t sb, int cb)
{
    if (sa != sb)
        return sa < sb;
    return ca < cb;
}

static void ordered(const ash_selection *s, uint64_t *lo_seq, int *lo_col,
                    uint64_t *hi_seq, int *hi_col)
{
    if (before(s->anchor_seq, s->anchor_col, s->point_seq, s->point_col)) {
        *lo_seq = s->anchor_seq;
        *lo_col = s->anchor_col;
        *hi_seq = s->point_seq;
        *hi_col = s->point_col;
    } else {
        *lo_seq = s->point_seq;
        *lo_col = s->point_col;
        *hi_seq = s->anchor_seq;
        *hi_col = s->anchor_col;
    }
}

int ash_sel_hittest(const ash_scrollback *sb, const ash_sb_viewport *vp,
                    int row, int col, uint64_t *seq, int *cell_col)
{
    if (row < vp->top_row || row >= vp->top_row + vp->rows)
        return 0;

    uint64_t s = vp->top_seq + (uint64_t)(row - vp->top_row);
    const ash_cell *cells = NULL;
    size_t n = 0;
    if (!ash_sb_line_at(sb, s, &cells, &n))
        return 0;

    int c = col;
    if (c < 0)
        c = 0;
    if (c > (int)n)
        c = (int)n;
    *seq = s;
    *cell_col = c;
    return 1;
}

void ash_sel_set(ash_selection *s, uint64_t seq, int col)
{
    s->active = 1;
    s->anchor_seq = seq;
    s->anchor_col = col;
    s->point_seq = seq;
    s->point_col = col;
}

void ash_sel_extend(ash_selection *s, uint64_t seq, int col)
{
    if (!s->active)
        return;
    s->point_seq = seq;
    s->point_col = col;
}

static int is_delim(uint8_t b)
{
    if (b == ' ' || b == '\t')
        return 1;
    if (b >= 0x80)
        return 0;
    return (b >= '!' && b <= '/') || (b >= ':' && b <= '@') ||
           (b >= '[' && b <= '`') || (b >= '{' && b <= '~');
}

static int word_char(const ash_cell *c)
{
    return (c->attr & ASH_ATTR_CONTENT) && c->len > 0 && !is_delim(c->bytes[0]);
}

void ash_sel_word(ash_selection *s, const ash_scrollback *sb,
                  uint64_t seq, int col)
{
    const ash_cell *cells = NULL;
    size_t n = 0;
    if (!ash_sb_line_at(sb, seq, &cells, &n) || n == 0) {
        ash_sel_set(s, seq, col);
        return;
    }

    int c = col;
    if (c >= (int)n)
        c = (int)n - 1;
    if (c < 0)
        c = 0;

    int lo = c;
    int hi = c;
    if (word_char(&cells[c])) {
        while (lo > 0 && word_char(&cells[lo - 1]))
            lo--;
        while (hi + 1 < (int)n && word_char(&cells[hi + 1]))
            hi++;
    }

    s->active = 1;
    s->anchor_seq = seq;
    s->anchor_col = lo;
    s->point_seq = seq;
    s->point_col = hi + 1;
}

void ash_sel_line(ash_selection *s, const ash_scrollback *sb,
                  uint64_t seq, int col)
{
    (void)col;
    uint64_t oldest = ash_sb_oldest(sb);
    uint64_t start = seq;
    while (start > oldest && ash_sb_cont(sb, start))
        start--;

    uint64_t last = ash_sb_newest(sb);
    uint64_t end = seq;
    while (end + 1 < last && ash_sb_cont(sb, end + 1))
        end++;

    const ash_cell *cells = NULL;
    size_t n = 0;
    (void)ash_sb_line_at(sb, end, &cells, &n);

    s->active = 1;
    s->anchor_seq = start;
    s->anchor_col = 0;
    s->point_seq = end;
    s->point_col = (int)n;
}

static int content_bounds(const ash_cell *cells, size_t n, int *first, int *last)
{
    int f = -1;
    int l = -1;
    for (int i = 0; i < (int)n; i++) {
        if (!(cells[i].attr & ASH_ATTR_CONTENT))
            continue;
        if (f < 0)
            f = i;
        l = i;
    }
    if (f < 0)
        return 0;
    *first = f;
    *last = l;
    return 1;
}

void ash_sel_apply(const ash_selection *s, const ash_scrollback *sb,
                   const ash_sb_viewport *vp, ash_fb *fb, ash_style style)
{
    if (ash_sel_empty(s))
        return;

    uint64_t lo_seq, hi_seq;
    int lo_col, hi_col;
    ordered(s, &lo_seq, &lo_col, &hi_seq, &hi_col);

    uint64_t top = vp->top_seq;
    uint64_t bot = top + (uint64_t)vp->rows;
    if (lo_seq < top)
        lo_seq = top;
    if (hi_seq >= bot)
        hi_seq = bot > 0 ? bot - 1 : top;

    for (uint64_t seq = lo_seq; seq <= hi_seq; seq++) {
        const ash_cell *cells = NULL;
        size_t n = 0;
        if (!ash_sb_line_at(sb, seq, &cells, &n))
            continue;

        int first, last;
        if (!content_bounds(cells, n, &first, &last))
            continue;

        int start = seq == lo_seq ? lo_col : 0;
        int end = seq == hi_seq ? hi_col : (int)n;
        if (start < first)
            start = first;
        if (end > last + 1)
            end = last + 1;
        if (start >= end)
            continue;

        int row = vp->top_row + (int)(seq - vp->top_seq);
        ash_fb_style_range(fb, start, row, end - start, style);
    }
}

size_t ash_sel_extract(const ash_selection *s, const ash_scrollback *sb,
                       ash_buf *out)
{
    if (ash_sel_empty(s))
        return 0;

    uint64_t lo_seq, hi_seq;
    int lo_col, hi_col;
    ordered(s, &lo_seq, &lo_col, &hi_seq, &hi_col);

    size_t begin = out->len;
    size_t logical_start = out->len;
    for (uint64_t seq = lo_seq; seq <= hi_seq; seq++) {
        const ash_cell *cells = NULL;
        size_t n = 0;
        (void)ash_sb_line_at(sb, seq, &cells, &n);

        int start = seq == lo_seq ? lo_col : 0;
        int end = seq == hi_seq ? hi_col : (int)n;
        if (start < 0)
            start = 0;
        if (end > (int)n)
            end = (int)n;
        if (start > 0 && start < (int)n && cells[start].len == 0)
            start--;

        if (n > 0) {
            int has_content = 0;
            for (size_t i = 0; i < n; i++) {
                if (cells[i].attr & ASH_ATTR_CONTENT) {
                    has_content = 1;
                    break;
                }
            }
            if (!has_content)
                continue;
        }

        for (int i = start; i < end; i++) {
            const ash_cell *c = &cells[i];
            if (!(c->attr & ASH_ATTR_CONTENT) || c->len == 0)
                continue;
            ash_buf_append(out, c->bytes, c->len);
        }

        int soft = seq < hi_seq && ash_sb_cont(sb, seq + 1);
        if (soft)
            continue;

        while (out->len > logical_start &&
               (out->data[out->len - 1] == ' ' ||
                out->data[out->len - 1] == '\t'))
            out->len--;
        if (seq < hi_seq)
            ash_buf_append_byte(out, '\n');
        logical_start = out->len;
    }

    return out->len - begin;
}

size_t ash_base64_encode(const void *in, size_t n, ash_buf *out)
{
    static const char T[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const uint8_t *p = in;
    size_t begin = out->len;
    size_t i = 0;

    for (; i + 3 <= n; i += 3) {
        uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8) | p[i + 2];
        ash_buf_append_byte(out, (unsigned char)T[(v >> 18) & 63u]);
        ash_buf_append_byte(out, (unsigned char)T[(v >> 12) & 63u]);
        ash_buf_append_byte(out, (unsigned char)T[(v >> 6) & 63u]);
        ash_buf_append_byte(out, (unsigned char)T[v & 63u]);
    }

    size_t rem = n - i;
    if (rem == 1) {
        uint32_t v = (uint32_t)p[i] << 16;
        ash_buf_append_byte(out, (unsigned char)T[(v >> 18) & 63u]);
        ash_buf_append_byte(out, (unsigned char)T[(v >> 12) & 63u]);
        ash_buf_append_byte(out, '=');
        ash_buf_append_byte(out, '=');
    } else if (rem == 2) {
        uint32_t v = ((uint32_t)p[i] << 16) | ((uint32_t)p[i + 1] << 8);
        ash_buf_append_byte(out, (unsigned char)T[(v >> 18) & 63u]);
        ash_buf_append_byte(out, (unsigned char)T[(v >> 12) & 63u]);
        ash_buf_append_byte(out, (unsigned char)T[(v >> 6) & 63u]);
        ash_buf_append_byte(out, '=');
    }

    return out->len - begin;
}
