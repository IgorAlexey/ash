#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ash/app/transcript.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/fb.h"
#include "ash/fb/scrollback.h"
#include "ash_test.h"

#ifndef ASH_GOLDEN_DIR
#define ASH_GOLDEN_DIR "."
#endif

enum { SB_BUDGET = 1u << 18, SB_LINES = 4096 };

static void sb_make(ash_scrollback *sb, ash_arena *a)
{
    ash_sb_init(sb, a, SB_BUDGET, SB_LINES);
}

static ash_ts_opts opts_of(int expanded, int pad_x, const ash_theme *th)
{
    ash_ts_opts o = { expanded, pad_x, th };
    return o;
}

static int line_at(ash_scrollback *sb, uint64_t seq, const ash_cell **c,
                   size_t *n)
{
    return ash_sb_line_at(sb, seq, c, n);
}

static int is_content(const ash_cell *c)
{
    return (c->attr & ASH_ATTR_CONTENT) != 0;
}

static int cells_equal(const ash_cell *a, const ash_cell *b, size_t n)
{
    return memcmp(a, b, n * sizeof(ash_cell)) == 0;
}

static int sb_equal(ash_scrollback *a, ash_scrollback *b)
{
    if (ash_sb_count(a) != ash_sb_count(b))
        return 0;
    if (ash_sb_oldest(a) != ash_sb_oldest(b))
        return 0;
    if (ash_sb_newest(a) != ash_sb_newest(b))
        return 0;
    for (uint64_t s = ash_sb_oldest(a); s < ash_sb_newest(a); s++) {
        const ash_cell *ca, *cb;
        size_t na, nb;
        if (!line_at(a, s, &ca, &na) || !line_at(b, s, &cb, &nb))
            return 0;
        if (na != nb || !cells_equal(ca, cb, na))
            return 0;
    }
    return 1;
}

static void test_purity(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "t", 1u << 20) == ASH_OK);
    ash_transcript t;
    ash_ts_init(&t, &a);
    ash_ts_append(&t, ASH_TS_INFO, "welcome to ash", 14, NULL, 0);
    ash_ts_append(&t, ASH_TS_USER, "hello there", 11, NULL, 0);
    ash_ts_append(&t, ASH_TS_TOOL_HEAD, NULL, 0, "ls -la", 6);
    ash_ts_append(&t, ASH_TS_TOOL_OUT, "one\ntwo\nthree", 13, NULL, 0);
    ash_ts_append(&t, ASH_TS_AGENT, "the answer is 42", 16, NULL, 0);

    ash_scrollback s1, s2;
    sb_make(&s1, &a);
    sb_make(&s2, &a);
    ash_ts_opts o = opts_of(0, 1, ash_theme_dark());
    ash_ts_project(&t, &s1, 24, &o);
    ash_ts_project(&t, &s2, 24, &o);
    ASH_CHECK(sb_equal(&s1, &s2));
    ash_arena_destroy(&a);
}

static size_t mkfill(char *buf, size_t cap, int count)
{
    size_t off = 0;
    for (int i = 1; i <= count && off + 8 < cap; i++)
        off += (size_t)snprintf(buf + off, cap - off, "L%02d\n", i);
    return off;
}

static int find_marker(ash_scrollback *sb, uint64_t *seq_out)
{
    for (uint64_t s = ash_sb_oldest(sb); s < ash_sb_newest(sb); s++) {
        const ash_cell *c;
        size_t n;
        if (!line_at(sb, s, &c, &n))
            break;
        for (size_t i = 0; i + 4 < n; i++) {
            if (c[i].bytes[0] == 'm' && c[i + 1].bytes[0] == 'o' &&
                c[i + 2].bytes[0] == 'r' && c[i + 3].bytes[0] == 'e') {
                if (seq_out)
                    *seq_out = s;
                return 1;
            }
        }
    }
    return 0;
}

static void test_truncation(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "t", 1u << 20) == ASH_OK);
    char buf[512];

    ash_transcript t20;
    ash_ts_init(&t20, &a);
    size_t n20 = mkfill(buf, sizeof buf, 20);
    ash_ts_append(&t20, ASH_TS_TOOL_OUT, buf, n20, NULL, 0);
    ash_scrollback s20;
    sb_make(&s20, &a);
    ash_ts_opts o = opts_of(0, 1, ash_theme_dark());
    ash_ts_project(&t20, &s20, 40, &o);
    ASH_CHECK(!find_marker(&s20, NULL));
    ASH_CHECK(ash_sb_count(&s20) == 20);

    ash_transcript t21;
    ash_ts_init(&t21, &a);
    size_t n21 = mkfill(buf, sizeof buf, 21);
    ash_ts_append(&t21, ASH_TS_TOOL_OUT, buf, n21, NULL, 0);
    ash_scrollback s21;
    sb_make(&s21, &a);
    ash_ts_project(&t21, &s21, 40, &o);
    uint64_t mseq = 0;
    ASH_CHECK(find_marker(&s21, &mseq));
    ASH_CHECK(ash_sb_count(&s21) == 21);

    const ash_cell *mc;
    size_t mn;
    ASH_CHECK(line_at(&s21, mseq, &mc, &mn));
    int marker_content = 0;
    for (size_t i = 0; i < mn; i++)
        if (is_content(&mc[i]))
            marker_content = 1;
    ASH_CHECK(!marker_content);

    const ash_cell *fc;
    size_t fn;
    ASH_CHECK(line_at(&s21, mseq + 1, &fc, &fn));
    ASH_CHECK(fn >= 3 && fc[1].bytes[0] == 'L' && fc[2].bytes[0] == '0' &&
              fc[3].bytes[0] == '2');

    ash_scrollback sx;
    sb_make(&sx, &a);
    ash_ts_opts ox = opts_of(1, 1, ash_theme_dark());
    ash_ts_project(&t21, &sx, 40, &ox);
    ASH_CHECK(!find_marker(&sx, NULL));
    ASH_CHECK(ash_sb_count(&sx) == 21);

    ash_arena_destroy(&a);
}

static void test_user_block(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "t", 1u << 20) == ASH_OK);
    ash_transcript t;
    ash_ts_init(&t, &a);
    ash_ts_append(&t, ASH_TS_USER, "hi", 2, NULL, 0);
    ash_scrollback sb;
    sb_make(&sb, &a);
    ash_ts_opts o = opts_of(0, 1, ash_theme_dark());
    ash_ts_project(&t, &sb, 12, &o);

    ASH_CHECK(ash_sb_count(&sb) == 3);
    ash_rgba bg = ash_theme_dark()->user_msg.bg;
    ash_rgba def = ASH_RGBA_DEFAULT;

    const ash_cell *c;
    size_t n;
    (void)def;
    ASH_CHECK(line_at(&sb, 0, &c, &n) && n == 12);
    ASH_CHECK(c[0].bg == bg && !is_content(&c[0]));
    ASH_CHECK(c[n - 1].bg == bg && !is_content(&c[n - 1]));
    ASH_CHECK(line_at(&sb, 2, &c, &n) && n == 12);
    ASH_CHECK(c[0].bg == bg && !is_content(&c[0]));
    ASH_CHECK(c[n - 1].bg == bg && !is_content(&c[n - 1]));

    ASH_CHECK(line_at(&sb, 1, &c, &n) && n == 12);
    ASH_CHECK(c[0].bg == bg && !is_content(&c[0]));
    int content = 0;
    size_t first_content = n;
    char typed[8];
    int tn = 0;
    for (size_t i = 0; i < n; i++) {
        if (is_content(&c[i])) {
            ASH_CHECK(c[i].bg == bg);
            if (i < first_content)
                first_content = i;
            content++;
            if (tn < 7)
                typed[tn++] = (char)c[i].bytes[0];
        }
    }
    typed[tn] = 0;
    ASH_CHECK(content == 2);
    ASH_CHECK_STREQ(typed, "hi");
    ASH_CHECK(first_content == 1);
    ASH_CHECK(c[n - 1].bg == bg && !is_content(&c[n - 1]));

    ash_arena_destroy(&a);
}

static size_t first_content_idx(const ash_cell *c, size_t n)
{
    for (size_t i = 0; i < n; i++)
        if (is_content(&c[i]))
            return i;
    return n;
}

static size_t last_content_idx(const ash_cell *c, size_t n)
{
    size_t hit = n;
    for (size_t i = 0; i < n; i++)
        if (is_content(&c[i]))
            hit = i;
    return hit;
}

static void test_insets(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "t", 1u << 20) == ASH_OK);
    ash_rgba def = ASH_RGBA_DEFAULT;
    ash_rgba ubg = ash_theme_dark()->user_msg.bg;
    const int W = 20;
    const int PAD = 2;

    ash_transcript t;
    ash_ts_init(&t, &a);
    ash_ts_append(&t, ASH_TS_USER, "hi", 2, NULL, 0);
    ash_ts_append(&t, ASH_TS_AGENT, "hi", 2, NULL, 0);
    ash_ts_append(&t, ASH_TS_INFO, "hi", 2, NULL, 0);

    ash_scrollback sb;
    sb_make(&sb, &a);
    ash_ts_opts o = opts_of(0, PAD, ash_theme_dark());
    ash_ts_project(&t, &sb, W, &o);

    uint64_t user_row = ash_ts_get(&t, 0)->proj_seq + 1;
    uint64_t agent_row = ash_ts_get(&t, 1)->proj_seq + 1;
    uint64_t info_row = ash_ts_get(&t, 2)->proj_seq + 1;

    const ash_cell *c;
    size_t n;

    ASH_CHECK(line_at(&sb, agent_row, &c, &n));
    ASH_CHECK(first_content_idx(c, n) == (size_t)PAD);
    ASH_CHECK(last_content_idx(c, n) == n - 1 - (size_t)PAD);
    for (int i = 0; i < PAD; i++)
        ASH_CHECK(c[i].bg == def && !is_content(&c[i]));

    ASH_CHECK(line_at(&sb, info_row, &c, &n));
    ASH_CHECK(first_content_idx(c, n) == (size_t)PAD);
    ASH_CHECK(last_content_idx(c, n) == n - 1 - (size_t)PAD);
    for (int i = 0; i < PAD; i++)
        ASH_CHECK(c[i].bg == def && !is_content(&c[i]));

    (void)def;
    ASH_CHECK(line_at(&sb, user_row, &c, &n) && n == (size_t)W);
    ASH_CHECK(first_content_idx(c, n) == (size_t)PAD);
    ASH_CHECK(last_content_idx(c, n) == (size_t)PAD + 1);
    for (size_t i = 0; i < n; i++)
        ASH_CHECK(c[i].bg == ubg);

    ash_arena_destroy(&a);
}

static void test_streaming(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "t", 1u << 20) == ASH_OK);
    ash_transcript t;
    ash_ts_init(&t, &a);
    ash_scrollback sb;
    sb_make(&sb, &a);
    ash_ts_opts o = opts_of(0, 1, ash_theme_dark());

    ash_ts_append(&t, ASH_TS_INFO, "banner line", 11, NULL, 0);
    ash_ts_project_tail(&t, &sb, 40, &o);
    ash_ts_append(&t, ASH_TS_AGENT, "hel", 3, NULL, 0);
    ash_ts_project_tail(&t, &sb, 40, &o);

    const ash_cell *c0;
    size_t n0;
    ASH_CHECK(line_at(&sb, 1, &c0, &n0));
    ash_cell saved[64];
    ASH_CHECK(n0 <= 64);
    memcpy(saved, c0, n0 * sizeof(ash_cell));

    ash_ts_append_stream(&t, "lo world", 8);
    ash_ts_project_tail(&t, &sb, 40, &o);

    const ash_cell *c0b;
    size_t n0b;
    ASH_CHECK(line_at(&sb, 1, &c0b, &n0b));
    ASH_CHECK(n0b == n0 && cells_equal(saved, c0b, n0));

    const ash_cell *c1;
    size_t n1;
    ASH_CHECK(line_at(&sb, 4, &c1, &n1));
    char got[32];
    int gn = 0;
    for (size_t i = 0; i < n1 && gn < 31; i++)
        if (is_content(&c1[i]))
            got[gn++] = (char)c1[i].bytes[0];
    got[gn] = 0;
    ASH_CHECK_STREQ(got, "hello world");

    ash_arena_destroy(&a);
}

static void test_padx(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "t", 1u << 20) == ASH_OK);
    ash_ts_opts o1 = opts_of(0, 1, ash_theme_dark());
    ash_ts_opts o0 = opts_of(0, 0, ash_theme_dark());

    ash_transcript t;
    ash_ts_init(&t, &a);
    ash_ts_append(&t, ASH_TS_AGENT, "x", 1, NULL, 0);
    ash_scrollback s1;
    sb_make(&s1, &a);
    ash_ts_project(&t, &s1, 10, &o1);
    const ash_cell *c;
    size_t n;
    ASH_CHECK(line_at(&s1, 1, &c, &n));
    ASH_CHECK(!is_content(&c[0]) && c[0].bytes[0] == ' ');
    ASH_CHECK(is_content(&c[1]) && c[1].bytes[0] == 'x');

    ash_scrollback s0;
    sb_make(&s0, &a);
    ash_ts_project(&t, &s0, 10, &o0);
    ASH_CHECK(line_at(&s0, 1, &c, &n));
    ASH_CHECK(is_content(&c[0]) && c[0].bytes[0] == 'x');

    ash_transcript tw;
    ash_ts_init(&tw, &a);
    ash_ts_append(&tw, ASH_TS_AGENT, "aaaaaaaaa", 9, NULL, 0);
    ash_scrollback sw;
    sb_make(&sw, &a);
    ash_ts_project(&tw, &sw, 10, &o1);
    ASH_CHECK(ash_sb_count(&sw) == 4);
    ASH_CHECK(line_at(&sw, 1, &c, &n));
    int first = 0;
    for (size_t i = 0; i < n; i++)
        if (is_content(&c[i]))
            first++;
    ASH_CHECK(first == 8);
    ASH_CHECK(line_at(&sw, 2, &c, &n));
    int second = 0;
    for (size_t i = 0; i < n; i++)
        if (is_content(&c[i]))
            second++;
    ASH_CHECK(second == 1);

    ash_arena_destroy(&a);
}

static void test_anchor(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "t", 1u << 20) == ASH_OK);
    ash_transcript t;
    ash_ts_init(&t, &a);
    ash_ts_append(&t, ASH_TS_INFO, "aaa", 3, NULL, 0);
    ash_ts_append(&t, ASH_TS_INFO, "bbb", 3, NULL, 0);
    char buf[512];
    size_t nn = mkfill(buf, sizeof buf, 30);
    ash_ts_append(&t, ASH_TS_TOOL_OUT, buf, nn, NULL, 0);
    ash_ts_append(&t, ASH_TS_AGENT, "tail", 4, NULL, 0);

    ash_scrollback sb;
    sb_make(&sb, &a);
    ash_ts_opts collapsed = opts_of(0, 1, ash_theme_dark());
    ash_ts_project(&t, &sb, 40, &collapsed);

    uint64_t s3 = ash_ts_get(&t, 3)->proj_seq;
    ASH_CHECK(ash_ts_block_at_seq(&t, s3) == 3);
    uint64_t s2 = ash_ts_get(&t, 2)->proj_seq;
    ASH_CHECK(ash_ts_block_at_seq(&t, s2 + 1) == 2);

    ash_ts_opts expanded = opts_of(1, 1, ash_theme_dark());
    ash_ts_project(&t, &sb, 40, &expanded);
    ASH_CHECK(ash_ts_block_at_seq(&t, ash_ts_get(&t, 3)->proj_seq) == 3);
    ASH_CHECK(ash_ts_get(&t, 3)->proj_seq > ash_ts_get(&t, 2)->proj_seq);

    ash_arena_destroy(&a);
}

static void test_contrast(void)
{
    const ash_theme *themes[2] = { ash_theme_dark(), ash_theme_light() };
    for (int i = 0; i < 2; i++) {
        float lt = ash_rgba_lightness(themes[i]->user_msg.fg);
        float lb = ash_rgba_lightness(themes[i]->user_msg.bg);
        float d = lt > lb ? lt - lb : lb - lt;
        ASH_CHECK(d > 0.3f);
    }
}

static void render_frame(ash_fb *fb, ash_scrollback *sb, int w, int h,
                         ash_arena *a, ash_buf *snap)
{
    ash_fb_begin(fb, w, h);
    int y = 0;
    for (uint64_t s = ash_sb_oldest(sb); s < ash_sb_newest(sb) && y < h;
         s++, y++) {
        const ash_cell *cells;
        size_t n;
        if (!line_at(sb, s, &cells, &n))
            break;
        int x = 0;
        for (size_t i = 0; i < n && x < w; i++) {
            const ash_cell *c = &cells[i];
            if (c->width == 0 || c->len == 0)
                continue;
            ash_style st = { c->fg, c->bg, c->attr };
            ash_fb_put_text(fb, x, y, st, c->bytes, c->len);
            x += c->width;
        }
    }
    ash_buf_init(snap, a);
    ash_fb_snapshot(fb, snap);
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
        fprintf(stderr, "golden %s mismatch (%zu vs %zu)\n", name, gn,
                snap->len);
}

static void golden_case(ash_arena *a, const char *name, const ash_theme *th,
                        int expanded)
{
    ash_transcript t;
    ash_ts_init(&t, a);
    ash_ts_append(&t, ASH_TS_USER, "hello world", 11, NULL, 0);
    ash_ts_append(&t, ASH_TS_TOOL_HEAD, NULL, 0, "grep foo", 8);
    char buf[512];
    size_t nn = mkfill(buf, sizeof buf, 23);
    ash_ts_append(&t, ASH_TS_TOOL_OUT, buf, nn, NULL, 0);

    ash_scrollback sb;
    sb_make(&sb, a);
    ash_ts_opts o = opts_of(expanded, 1, th);
    ash_ts_project(&t, &sb, 24, &o);

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, a, def);
    ash_buf snap;
    render_frame(&fb, &sb, 24, 30, a, &snap);
    check_golden(name, &snap);
}

static void test_golden(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "g", 1u << 20) == ASH_OK);
    golden_case(&a, "collapsed_dark", ash_theme_dark(), 0);
    golden_case(&a, "expanded_dark", ash_theme_dark(), 1);
    golden_case(&a, "collapsed_light", ash_theme_light(), 0);
    golden_case(&a, "expanded_light", ash_theme_light(), 1);
    ash_arena_destroy(&a);
}

int main(void)
{
    test_purity();
    test_truncation();
    test_user_block();
    test_insets();
    test_streaming();
    test_padx();
    test_anchor();
    test_contrast();
    test_golden();
    return ash_test_done();
}
