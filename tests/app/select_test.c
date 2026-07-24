#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/app/select.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/fb.h"
#include "ash/fb/scrollback.h"
#include "ash_test.h"

#ifndef ASH_GOLDEN_DIR
#define ASH_GOLDEN_DIR "."
#endif

enum { LINE_MAX = 128 };

static void mkcell(ash_cell *c, const char *bytes, int len, int width, int content)
{
    memset(c, 0, sizeof *c);
    memcpy(c->bytes, bytes, (size_t)len);
    c->len = (uint8_t)len;
    c->width = (uint8_t)width;
    c->attr = content ? ASH_ATTR_CONTENT : 0;
    c->fg = ASH_RGBA_DEFAULT;
    c->bg = ASH_RGBA_DEFAULT;
}

static void put_ascii(ash_scrollback *sb, const char *s, int cont)
{
    ash_cell line[LINE_MAX];
    size_t n = 0;
    for (const char *p = s; *p; p++)
        mkcell(&line[n++], p, 1, 1, 1);
    ash_sb_append_wrapped(sb, line, n, cont);
}

static const char *extract(ash_selection *sel, ash_scrollback *sb, ash_arena *a)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_sel_extract(sel, sb, &b);
    ash_buf_append_byte(&b, 0);
    return (const char *)b.data;
}

static void select_all(ash_selection *sel, ash_scrollback *sb)
{
    uint64_t lo = ash_sb_oldest(sb);
    uint64_t hi = ash_sb_newest(sb) - 1;
    const ash_cell *cells;
    size_t n;
    (void)ash_sb_line_at(sb, hi, &cells, &n);
    ash_sel_set(sel, lo, 0);
    ash_sel_extend(sel, hi, (int)n);
}

static void test_trailing_strip(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "abc  ", 0);
    ash_selection sel;
    ash_sel_clear(&sel);
    select_all(&sel, &sb);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "abc");
}

static void test_padding_excluded(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);

    ash_cell line[LINE_MAX];
    size_t n = 0;
    mkcell(&line[n++], "a", 1, 1, 1);
    mkcell(&line[n++], "b", 1, 1, 1);
    mkcell(&line[n++], "#", 1, 1, 0);
    mkcell(&line[n++], "#", 1, 1, 0);
    mkcell(&line[n++], "c", 1, 1, 1);
    mkcell(&line[n++], "d", 1, 1, 1);
    ash_sb_append_wrapped(&sb, line, n, 0);

    ash_selection sel;
    ash_sel_clear(&sel);
    select_all(&sel, &sb);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "abcd");
}

static void test_softwrap_join(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "foobar", 0);
    put_ascii(&sb, "baz", 1);

    ash_selection sel;
    ash_sel_clear(&sel);
    select_all(&sel, &sb);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "foobarbaz");
}

static void test_softwrap_keeps_inner_space(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "foo   ", 0);
    put_ascii(&sb, "bar", 1);

    ash_selection sel;
    ash_sel_clear(&sel);
    select_all(&sel, &sb);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "foo   bar");
}

static void test_multiline_newline(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "one", 0);
    put_ascii(&sb, "two", 0);
    put_ascii(&sb, "three", 0);

    ash_selection sel;
    ash_sel_clear(&sel);
    select_all(&sel, &sb);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "one\ntwo\nthree");
}

static void put_decor(ash_scrollback *sb, int width)
{
    ash_cell line[LINE_MAX];
    for (int i = 0; i < width; i++)
        mkcell(&line[i], " ", 1, 1, 0);
    ash_sb_append_wrapped(sb, line, (size_t)width, 0);
}

static void test_decor_rows_skipped(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_decor(&sb, 24);
    put_ascii(&sb, " hi", 0);
    put_decor(&sb, 24);
    ash_sb_append_wrapped(&sb, NULL, 0, 0);
    put_ascii(&sb, "    code", 0);

    ash_selection sel;
    ash_sel_clear(&sel);
    select_all(&sel, &sb);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), " hi\n\n    code");
}

static void test_real_blank_lines_kept(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "one", 0);
    ash_sb_append_wrapped(&sb, NULL, 0, 0);
    ash_sb_append_wrapped(&sb, NULL, 0, 0);
    put_ascii(&sb, "two", 0);

    ash_selection sel;
    ash_sel_clear(&sel);
    select_all(&sel, &sb);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "one\n\n\ntwo");
}

static void test_partial_columns(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "hello", 0);
    put_ascii(&sb, "world", 0);

    ash_selection sel;
    ash_sel_clear(&sel);
    ash_sel_set(&sel, ash_sb_oldest(&sb), 2);
    ash_sel_extend(&sel, ash_sb_oldest(&sb) + 1, 3);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "llo\nwor");
}

static void test_grapheme_straddle(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);

    ash_cell line[LINE_MAX];
    size_t n = 0;
    mkcell(&line[n++], "\xe4\xb8\xad", 3, 2, 1);
    mkcell(&line[n++], "", 0, 0, 1);
    mkcell(&line[n++], "A", 1, 1, 1);
    ash_sb_append_wrapped(&sb, line, n, 0);

    uint64_t s = ash_sb_oldest(&sb);

    ash_selection sel;
    ash_sel_clear(&sel);
    ash_sel_set(&sel, s, 0);
    ash_sel_extend(&sel, s, 1);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "\xe4\xb8\xad");

    ash_sel_set(&sel, s, 1);
    ash_sel_extend(&sel, s, 3);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "\xe4\xb8\xad" "A");
}

static void test_word_and_line(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "foo bar baz", 0);

    uint64_t s = ash_sb_oldest(&sb);
    ash_selection sel;
    ash_sel_clear(&sel);
    ash_sel_word(&sel, &sb, s, 5);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "bar");

    ash_sb_reset(&sb);
    put_ascii(&sb, "aaa", 0);
    put_ascii(&sb, "bbb", 1);
    put_ascii(&sb, "ccc", 1);
    put_ascii(&sb, "ddd", 0);

    ash_sel_line(&sel, &sb, ash_sb_oldest(&sb) + 1, 0);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "aaabbbccc");
}

static void test_hittest(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "hello", 0);
    put_ascii(&sb, "world", 0);

    ash_sb_viewport vp = { ash_sb_oldest(&sb), 0, 2, 80 };
    uint64_t seq;
    int col;
    ASH_CHECK(ash_sel_hittest(&sb, &vp, 1, 3, &seq, &col));
    ASH_CHECK(seq == ash_sb_oldest(&sb) + 1 && col == 3);
    ASH_CHECK(ash_sel_hittest(&sb, &vp, 0, 99, &seq, &col));
    ASH_CHECK(col == 5);
    ASH_CHECK(!ash_sel_hittest(&sb, &vp, 5, 0, &seq, &col));
}

static void test_stability_under_append(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 65536 * sizeof(ash_cell), 512);
    put_ascii(&sb, "keep-this-line", 0);
    put_ascii(&sb, "other", 0);

    uint64_t s = ash_sb_oldest(&sb);
    ash_selection sel;
    ash_sel_clear(&sel);
    ash_sel_set(&sel, s, 0);
    ash_sel_extend(&sel, s, 14);
    const char *before = extract(&sel, &sb, a);
    ASH_CHECK_STREQ(before, "keep-this-line");

    for (int i = 0; i < 200; i++)
        put_ascii(&sb, "new output line churning below", 0);

    ASH_CHECK(sel.anchor_seq == s && sel.point_seq == s);
    ASH_CHECK_STREQ(extract(&sel, &sb, a), "keep-this-line");
}

static void test_empty_no_copy(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "abc", 0);

    ash_selection sel;
    ash_sel_clear(&sel);
    ASH_CHECK(ash_sel_extract(&sel, &sb, NULL) == 0);

    ash_sel_set(&sel, ash_sb_oldest(&sb), 2);
    ASH_CHECK(ash_sel_empty(&sel));
}

static void test_base64(ash_arena *a)
{
    struct {
        const char *in;
        const char *out;
    } v[] = {
        { "", "" },
        { "f", "Zg==" },
        { "fo", "Zm8=" },
        { "foo", "Zm9v" },
        { "foob", "Zm9vYg==" },
        { "hello world", "aGVsbG8gd29ybGQ=" },
    };
    for (size_t i = 0; i < sizeof v / sizeof v[0]; i++) {
        ash_buf b;
        ash_buf_init(&b, a);
        ash_base64_encode(v[i].in, strlen(v[i].in), &b);
        ash_buf_append_byte(&b, 0);
        ASH_CHECK_STREQ((const char *)b.data, v[i].out);
    }
}

static void draw_cells(ash_fb *fb, int y, const ash_cell *cells, size_t n)
{
    int x = 0;
    for (size_t i = 0; i < n && x < fb->w; i++) {
        const ash_cell *c = &cells[i];
        if (c->width == 0 || c->len == 0)
            continue;
        ash_style st = { c->fg, c->bg, c->attr };
        ash_fb_put_text(fb, x, y, st, c->bytes, c->len);
        x += c->width;
    }
}

static void check_golden(const char *name, const ash_buf *snap)
{
    char path[512];
    int n = snprintf(path, sizeof path, "%s/%s.golden", ASH_GOLDEN_DIR, name);
    ASH_CHECK(n > 0 && (size_t)n < sizeof path);

    if (getenv("ASH_GOLDEN_UPDATE")) {
        FILE *f = fopen(path, "wb");
        ASH_CHECK(f != NULL);
        if (f) {
            fwrite(snap->data, 1, snap->len, f);
            fclose(f);
        }
        return;
    }

    FILE *f = fopen(path, "rb");
    ASH_CHECK(f != NULL);
    if (!f)
        return;
    static unsigned char gold[65536];
    size_t gn = fread(gold, 1, sizeof gold, f);
    fclose(f);
    int ok = gn == snap->len && memcmp(gold, snap->data, gn) == 0;
    ASH_CHECK(ok);
    if (!ok)
        fprintf(stderr, "--- golden %s ---\n%.*s\n--- got ---\n%.*s\n", name,
                (int)gn, gold, (int)snap->len, (char *)snap->data);
}

static void test_golden_highlight(ash_arena *a)
{
    ash_scrollback sb;
    ash_sb_init(&sb, a, 4096 * sizeof(ash_cell), 64);
    put_ascii(&sb, "hello", 0);
    put_ascii(&sb, "world", 0);

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, a, def);
    ash_fb_begin(&fb, 8, 3);

    ash_sb_viewport vp = { ash_sb_oldest(&sb), 0, 2, 8 };
    for (int r = 0; r < 2; r++) {
        const ash_cell *cells;
        size_t n;
        (void)ash_sb_line_at(&sb, vp.top_seq + (uint64_t)r, &cells, &n);
        draw_cells(&fb, r, cells, n);
    }

    ash_selection sel;
    ash_sel_clear(&sel);
    ash_sel_set(&sel, vp.top_seq, 2);
    ash_sel_extend(&sel, vp.top_seq + 1, 4);
    ash_sel_apply(&sel, &sb, &vp, &fb, ash_selection_style());

    ash_style sty = ash_selection_style();
    const ash_cell *row0 = fb.buffers[fb.frame];
    ASH_CHECK(row0[2].bg == sty.bg && row0[2].fg == sty.fg);
    ASH_CHECK(row0[4].bg == sty.bg);
    ASH_CHECK(row0[1].bg != sty.bg);
    ASH_CHECK(!(row0[2].attr & ASH_ATTR_REVERSE));

    ash_buf snap;
    ash_buf_init(&snap, a);
    ash_fb_snapshot(&fb, &snap);
    check_golden("select_highlight", &snap);
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "select", 1u << 20) == ASH_OK);

    test_trailing_strip(&a);
    test_padding_excluded(&a);
    test_softwrap_join(&a);
    test_softwrap_keeps_inner_space(&a);
    test_multiline_newline(&a);
    test_decor_rows_skipped(&a);
    test_real_blank_lines_kept(&a);
    test_partial_columns(&a);
    test_grapheme_straddle(&a);
    test_word_and_line(&a);
    test_hittest(&a);
    test_stability_under_append(&a);
    test_empty_no_copy(&a);
    test_base64(&a);
    test_golden_highlight(&a);

    ash_arena_destroy(&a);
    return ash_test_done();
}
