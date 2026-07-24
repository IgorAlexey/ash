#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/ui/footer.h"
#include "ash_test.h"

#ifndef ASH_GOLDEN_DIR
#define ASH_GOLDEN_DIR "."
#endif

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
    if (!f) {
        fprintf(stderr, "missing golden %s (rerun with ASH_GOLDEN_UPDATE=1)\n", path);
        return;
    }
    static unsigned char gold[65536];
    size_t gn = fread(gold, 1, sizeof gold, f);
    fclose(f);

    int ok = gn == snap->len && memcmp(gold, snap->data, gn) == 0;
    ASH_CHECK(ok);
    if (!ok)
        fprintf(stderr, "--- golden %s ---\n%.*s\n--- got ---\n%.*s\n",
                name, (int)gn, gold, (int)snap->len, (char *)snap->data);
}

static void snap(ash_fb *fb, ash_arena *a, const char *name)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_fb_snapshot(fb, &b);
    check_golden(name, &b);
}

static void tok(int64_t n, const char *want)
{
    char buf[16];
    ash_footer_fmt_tokens(n, buf, sizeof buf);
    ASH_CHECK_STREQ(buf, want);
}

static void test_format(void)
{
    tok(0, "0");
    tok(999, "999");
    tok(1000, "1.0k");
    tok(1500, "1.5k");
    tok(9999, "10.0k");
    tok(12345, "12k");
    tok(999999, "1000k");
    tok(1500000, "1.5M");
    tok(12000000, "12M");
}

static void test_accounting(void)
{
    ash_footer f;
    ash_footer_init(&f);

    ash_footer_add_usage(&f, 100, 50, 0.0010);
    ash_footer_add_usage(&f, 10, 5, 0.0005);
    ASH_CHECK(f.in_tokens == 110);
    ASH_CHECK(f.out_tokens == 55);
    ASH_CHECK(f.cost_usd > 0.00149 && f.cost_usd < 0.00151);

    ash_footer_set_context(&f, 1234, 200000);
    ASH_CHECK(f.context_used == 1234);
    ASH_CHECK(f.context_window == 200000);
    ash_footer_set_context(&f, -5, -9);
    ASH_CHECK(f.context_used == 0 && f.context_window == 0);
}

static void test_status(void)
{
    ash_footer f;
    ash_footer_init(&f);

    ash_footer_set_status(&f, "b", "beta");
    ash_footer_set_status(&f, "a", "alpha");
    ASH_CHECK(f.ext_count == 2);
    ash_footer_set_status(&f, "a", "alpha2");
    ASH_CHECK(f.ext_count == 2);
    ash_footer_set_status(&f, "b", NULL);
    ASH_CHECK(f.ext_count == 1);
    ASH_CHECK_STREQ(f.ext[0].key, "a");
    ASH_CHECK_STREQ(f.ext[0].text, "alpha2");
    ash_footer_clear_status(&f);
    ASH_CHECK(f.ext_count == 0);
}

static void write_file(const char *path, const char *s)
{
    FILE *fp = fopen(path, "wb");
    ASH_CHECK(fp != NULL);
    if (fp) {
        fwrite(s, 1, strlen(s), fp);
        fclose(fp);
    }
}

static void set_mtime(const char *path, time_t sec)
{
    struct timespec ts[2];
    ts[0].tv_sec = sec;
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = sec;
    ts[1].tv_nsec = 0;
    ASH_CHECK(utimensat(AT_FDCWD, path, ts, 0) == 0);
}

static void test_git(void)
{
    char base[] = "/tmp/ash_footer_XXXXXX";
    ASH_CHECK(mkdtemp(base) != NULL);
    char gitdir[128], head[256];
    snprintf(gitdir, sizeof gitdir, "%s/.git", base);
    snprintf(head, sizeof head, "%s/HEAD", gitdir);
    ASH_CHECK(mkdir(gitdir, 0777) == 0);
    write_file(head, "ref: refs/heads/main\n");
    set_mtime(head, 1000);

    ash_footer f;
    ash_footer_init(&f);
    ASH_CHECK(ash_footer_git_init(&f, base) == 1);
    ASH_CHECK(f.git_ready == 1);
    ASH_CHECK(ash_footer_git_poll(&f, 1000) == 1);
    ASH_CHECK_STREQ(f.branch, "main");

    ASH_CHECK(ash_footer_git_poll(&f, 1100) == 0);

    write_file(head, "ref: refs/heads/feature\n");
    set_mtime(head, 2000);
    ASH_CHECK(ash_footer_git_poll(&f, 2000) == 1);
    ASH_CHECK_STREQ(f.branch, "feature");

    ASH_CHECK(ash_footer_git_poll(&f, 3000) == 0);

    write_file(head, "0123456789abcdef0123456789abcdef01234567\n");
    set_mtime(head, 4000);
    ASH_CHECK(ash_footer_git_poll(&f, 4000) == 1);
    ASH_CHECK_STREQ(f.branch, "detached");

    char wt[] = "/tmp/ash_footer_XXXXXX";
    ASH_CHECK(mkdtemp(wt) != NULL);
    char wtgit[128], gc[256];
    snprintf(wtgit, sizeof wtgit, "%s/.git", wt);
    snprintf(gc, sizeof gc, "gitdir: %s\n", gitdir);
    write_file(wtgit, gc);
    write_file(head, "ref: refs/heads/wtbranch\n");
    set_mtime(head, 5000);

    ash_footer h;
    ash_footer_init(&h);
    ASH_CHECK(ash_footer_git_init(&h, wt) == 1);
    ASH_CHECK(ash_footer_git_poll(&h, 6000) == 1);
    ASH_CHECK_STREQ(h.branch, "wtbranch");

    char none[] = "/tmp/ash_footer_XXXXXX";
    ASH_CHECK(mkdtemp(none) != NULL);
    ash_footer g;
    ash_footer_init(&g);
    ASH_CHECK(ash_footer_git_init(&g, none) == 0);
    ASH_CHECK(ash_footer_git_poll(&g, 1000) == 0);

    unlink(head);
    unlink(wtgit);
    rmdir(gitdir);
    rmdir(base);
    rmdir(wt);
    rmdir(none);
}

static void test_render(ash_arena *a)
{
    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, a, def);

    ash_footer f;
    ash_footer_init(&f);
    ash_footer_set_provider(&f, "anthropic", "claude-sonnet-4");
    ash_footer_add_usage(&f, 12300, 3400, 0.123);
    ash_footer_set_context(&f, 45000, 200000);
    f.have_branch = 1;
    snprintf(f.branch, sizeof f.branch, "%s", "main");
    ash_footer_set_status(&f, "lsp", "2 warnings");

    ash_fb_begin(&fb, 80, 1);
    ash_footer_render(&f, &fb, (ash_rect){ 0, 0, 80, 1 });
    snap(&fb, a, "status_ok");

    ash_footer_set_context(&f, 192000, 200000);
    ash_fb_begin(&fb, 80, 1);
    ash_footer_render(&f, &fb, (ash_rect){ 0, 0, 80, 1 });
    snap(&fb, a, "status_hot");

    ash_footer g;
    ash_footer_init(&g);
    ash_footer_set_provider(&g, "deepseek", "deepseek-chat");
    ash_footer_add_usage(&g, 500, 120, 0.0);
    ash_footer_set_context(&g, 500, 0);
    ash_fb_begin(&fb, 60, 1);
    ash_footer_render(&g, &fb, (ash_rect){ 0, 0, 60, 1 });
    snap(&fb, a, "status_min");
}

static void test_render_widths(ash_arena *a)
{
    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, a, def);

    ash_footer f;
    ash_footer_init(&f);
    ash_footer_set_provider(&f, "anthropic", "claude-sonnet-4-5");
    ash_footer_add_usage(&f, 12300, 3400, 0.123);
    ash_footer_set_context(&f, 45000, 200000);
    f.have_branch = 1;
    snprintf(f.branch, sizeof f.branch, "%s", "master");

    const int widths[] = { 40, 65, 120 };
    const char *names[] = { "width_40", "width_65", "width_120" };
    for (int i = 0; i < 3; i++) {
        ash_fb_begin(&fb, widths[i], 1);
        ash_footer_render(&f, &fb, (ash_rect){ 0, 0, widths[i], 1 });
        snap(&fb, a, names[i]);
    }
}

static void test_style(ash_arena *a)
{
    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, a, def);

    ash_footer f;
    ash_footer_init(&f);
    ash_footer_set_provider(&f, "anthropic", "claude-sonnet-4");
    ash_footer_add_usage(&f, 12300, 3400, 0.123);
    ash_footer_set_context(&f, 45000, 200000);
    f.have_branch = 1;
    snprintf(f.branch, sizeof f.branch, "%s", "main");

    ash_fb_begin(&fb, 80, 1);
    ash_footer_render(&f, &fb, (ash_rect){ 0, 0, 80, 1 });

    const ash_cell *row = fb.buffers[fb.frame & 1u];
    ash_rgba green = ash_rgb(0x3f, 0xb9, 0x50);
    int reverse = 0, cost_green = 0;
    for (int x = 0; x < 80; x++) {
        if (row[x].attr & ASH_ATTR_REVERSE)
            reverse = 1;
        if (row[x].len == 1 && row[x].bytes[0] == '$' && row[x].fg == green)
            cost_green = 1;
    }
    ASH_CHECK(reverse == 0);
    ASH_CHECK(cost_green == 1);
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "footer", 1u << 16) == ASH_OK);

    test_format();
    test_accounting();
    test_status();
    test_git();
    test_render(&a);
    test_render_widths(&a);
    test_style(&a);

    ash_arena_destroy(&a);
    return ash_test_done();
}
