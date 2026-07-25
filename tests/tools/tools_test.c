#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/tools/tools.h"
#include "ash_test.h"

static char g_dir[] = "/tmp/ash-tools-XXXXXX";

static ash_tool_result run(ash_arena *a, const char *name, const char *json)
{
    const ash_tool *t = ash_tool_find(name);
    ASH_CHECK(t != NULL && t->run != NULL);
    ash_tool_result res = { 0 };
    ASH_CHECK(ash_tool_dispatch(t, a, json, strlen(json), &res) == ASH_OK);
    return res;
}

static int spew(const char *path, const void *data, size_t len)
{
    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return 0;
    const char *p = data;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w <= 0) {
            close(fd);
            return 0;
        }
        off += (size_t)w;
    }
    close(fd);
    return 1;
}

static char *slurp(ash_arena *a, const char *path, size_t *len)
{
    int fd = open(path, O_RDONLY);
    ASH_CHECK(fd >= 0);
    ash_buf b;
    ash_buf_init(&b, a);
    char tmp[4096];
    ssize_t n;
    while ((n = read(fd, tmp, sizeof tmp)) > 0)
        ash_buf_append(&b, tmp, (size_t)n);
    close(fd);
    ash_buf_append_byte(&b, 0);
    b.len--;
    if (len)
        *len = b.len;
    return (char *)b.data;
}

static const char *jpath(ash_arena *a, const char *name)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_buf_append_cstr(&b, g_dir);
    ash_buf_append_byte(&b, '/');
    ash_buf_append_cstr(&b, name);
    ash_buf_append_byte(&b, 0);
    return (const char *)b.data;
}

static void test_write_read(ash_arena *a)
{
    const char *p = jpath(a, "hello.txt");
    ash_buf j;
    ash_buf_init(&j, a);
    ash_buf_append_cstr(&j, "{\"path\":\"");
    ash_buf_append_cstr(&j, p);
    ash_buf_append_cstr(&j, "\",\"content\":\"line1\\nline2\\nline3\\n\"}");
    ash_buf_append_byte(&j, 0);
    ash_tool_result w = run(a, "write", (const char *)j.data);
    ASH_CHECK(!w.is_error && strstr(w.content, "Successfully wrote") != NULL);

    size_t flen = 0;
    char *got = slurp(a, p, &flen);
    ASH_CHECK(strcmp(got, "line1\nline2\nline3\n") == 0);

    ash_buf r;
    ash_buf_init(&r, a);
    ash_buf_append_cstr(&r, "{\"path\":\"");
    ash_buf_append_cstr(&r, p);
    ash_buf_append_cstr(&r, "\"}");
    ash_buf_append_byte(&r, 0);
    ash_tool_result rr = run(a, "read", (const char *)r.data);
    ASH_CHECK(!rr.is_error && strstr(rr.content, "line2") != NULL);
}

static void test_read_offset_limit(ash_arena *a)
{
    const char *p = jpath(a, "big.txt");
    ash_buf b;
    ash_buf_init(&b, a);
    for (int i = 1; i <= 5000; i++) {
        char ln[32];
        int n = snprintf(ln, sizeof ln, "L%d\n", i);
        ash_buf_append(&b, ln, (size_t)n);
    }
    ASH_CHECK(spew(p, b.data, b.len));

    ash_buf r;
    ash_buf_init(&r, a);
    ash_buf_append_cstr(&r, "{\"path\":\"");
    ash_buf_append_cstr(&r, p);
    ash_buf_append_cstr(&r, "\",\"offset\":10,\"limit\":3}");
    ash_buf_append_byte(&r, 0);
    ash_tool_result rr = run(a, "read", (const char *)r.data);
    ASH_CHECK(!rr.is_error);
    ASH_CHECK(strstr(rr.content, "L10\nL11\nL12") != NULL);
    ASH_CHECK(strstr(rr.content, "more lines in file") != NULL);
    ASH_CHECK(strstr(rr.content, "offset=13") != NULL);

    ash_buf r2;
    ash_buf_init(&r2, a);
    ash_buf_append_cstr(&r2, "{\"path\":\"");
    ash_buf_append_cstr(&r2, p);
    ash_buf_append_cstr(&r2, "\"}");
    ash_buf_append_byte(&r2, 0);
    ash_tool_result rr2 = run(a, "read", (const char *)r2.data);
    ASH_CHECK(!rr2.is_error);
    ASH_CHECK(strstr(rr2.content, "Showing lines 1-2000 of 5001") != NULL);
    ASH_CHECK(strstr(rr2.content, "offset=2001") != NULL);
}

static void test_read_offset_error(ash_arena *a)
{
    const char *p = jpath(a, "hello.txt");
    ash_buf r;
    ash_buf_init(&r, a);
    ash_buf_append_cstr(&r, "{\"path\":\"");
    ash_buf_append_cstr(&r, p);
    ash_buf_append_cstr(&r, "\",\"offset\":9999}");
    ash_buf_append_byte(&r, 0);
    ash_tool_result rr = run(a, "read", (const char *)r.data);
    ASH_CHECK(rr.is_error && strstr(rr.content, "beyond end of file") != NULL);
}

static const char *wrote(ash_arena *a, const char *name, const char *content)
{
    const char *p = jpath(a, name);
    ASH_CHECK(spew(p, content, strlen(content)));
    return p;
}

static void test_edit_exact(ash_arena *a)
{
    const char *p = wrote(a, "e1.txt", "alpha\nbeta\ngamma\n");
    ash_buf j;
    ash_buf_init(&j, a);
    ash_buf_append_cstr(&j, "{\"path\":\"");
    ash_buf_append_cstr(&j, p);
    ash_buf_append_cstr(&j, "\",\"edits\":[{\"oldText\":\"beta\",\"newText\":\"BETA\"}]}");
    ash_buf_append_byte(&j, 0);
    ash_tool_result e = run(a, "edit", (const char *)j.data);
    ASH_CHECK(!e.is_error && strstr(e.content, "replaced 1 block") != NULL);
    char *got = slurp(a, p, NULL);
    ASH_CHECK(strcmp(got, "alpha\nBETA\ngamma\n") == 0);
}

static void test_edit_fuzzy_preserves(ash_arena *a)
{
    const char *p = wrote(a, "e2.txt", "keep me   \nold\xE2\x80\x99line\ntail\n");
    ash_buf j;
    ash_buf_init(&j, a);
    ash_buf_append_cstr(&j, "{\"path\":\"");
    ash_buf_append_cstr(&j, p);
    ash_buf_append_cstr(&j,
        "\",\"edits\":[{\"oldText\":\"old'line\",\"newText\":\"new\"}]}");
    ash_buf_append_byte(&j, 0);
    ash_tool_result e = run(a, "edit", (const char *)j.data);
    ASH_CHECK(!e.is_error && strstr(e.content, "replaced 1 block") != NULL);
    char *got = slurp(a, p, NULL);
    ASH_CHECK(strcmp(got, "keep me   \nnew\ntail\n") == 0);
}

static void test_edit_errors(ash_arena *a)
{
    const char *p = wrote(a, "e3.txt", "one\ntwo\ntwo\nthree\n");
    ash_buf j;
    ash_buf_init(&j, a);
    ash_buf_append_cstr(&j, "{\"path\":\"");
    ash_buf_append_cstr(&j, p);
    ash_buf_append_cstr(&j, "\",\"edits\":[{\"oldText\":\"two\",\"newText\":\"2\"}]}");
    ash_buf_append_byte(&j, 0);
    ash_tool_result dup = run(a, "edit", (const char *)j.data);
    ASH_CHECK(dup.is_error && strstr(dup.content, "not unique") != NULL);

    ash_buf j2;
    ash_buf_init(&j2, a);
    ash_buf_append_cstr(&j2, "{\"path\":\"");
    ash_buf_append_cstr(&j2, p);
    ash_buf_append_cstr(&j2, "\",\"edits\":[{\"oldText\":\"absent\",\"newText\":\"x\"}]}");
    ash_buf_append_byte(&j2, 0);
    ash_tool_result nf = run(a, "edit", (const char *)j2.data);
    ASH_CHECK(nf.is_error && strstr(nf.content, "could not find") != NULL);
}

static const char *edit_json(ash_arena *a, const char *path, const char *old,
                             const char *neu)
{
    ash_buf j;
    ash_buf_init(&j, a);
    ash_buf_append_cstr(&j, "{\"path\":\"");
    ash_buf_append_cstr(&j, path);
    ash_buf_append_cstr(&j, "\",\"edits\":[{\"oldText\":\"");
    ash_buf_append_cstr(&j, old);
    ash_buf_append_cstr(&j, "\",\"newText\":\"");
    ash_buf_append_cstr(&j, neu);
    ash_buf_append_cstr(&j, "\"}]}");
    ash_buf_append_byte(&j, 0);
    return (const char *)j.data;
}

static int same(ash_arena *a, const char *path, const char *want, size_t wlen)
{
    size_t got_len = 0;
    const char *got = slurp(a, path, &got_len);
    return got_len == wlen && memcmp(got, want, wlen) == 0;
}

enum { CONFIRM_CAP = 256 };

typedef struct confirm_log {
    int         calls;
    int         accept;
    const char *subst;
    size_t      subst_len;
    char        path[CONFIRM_CAP];
    char        old[CONFIRM_CAP];
    size_t      olen;
    char        neu[CONFIRM_CAP];
    size_t      nlen;
} confirm_log;

static void copy_n(char *dst, const char *src, size_t len)
{
    size_t max = (size_t)CONFIRM_CAP - 1;
    size_t n = len < max ? len : max;
    memcpy(dst, src, n);
    dst[n] = 0;
}

static int confirm_hook(void *ud, const char *path, const char *old,
                        size_t olen, const char *neu, size_t nlen,
                        const char **edited, size_t *edited_len)
{
    confirm_log *lg = ud;
    lg->calls++;
    copy_n(lg->path, path, strlen(path));
    copy_n(lg->old, old, olen);
    lg->olen = olen;
    copy_n(lg->neu, neu, nlen);
    lg->nlen = nlen;
    if (lg->accept && lg->subst != NULL) {
        *edited = lg->subst;
        *edited_len = lg->subst_len;
    }
    return lg->accept;
}

static void test_edit_no_hook(ash_arena *a)
{
    const char *p = wrote(a, "c1.txt", "alpha\nbeta\ngamma\n");
    ash_tool_result e = run(a, "edit", edit_json(a, p, "beta", "BETA"));
    ASH_CHECK(!e.is_error && strstr(e.content, "replaced 1 block") != NULL);
    ASH_CHECK(same(a, p, "alpha\nBETA\ngamma\n", 17));
    unlink(p);
}

static void test_edit_hook_reject(ash_arena *a)
{
    const char *p = wrote(a, "c2.txt", "alpha\nbeta\ngamma\n");
    confirm_log lg = { 0 };
    lg.accept = 0;
    ash_tools_set_confirm(confirm_hook, &lg);
    ash_tool_result e = run(a, "edit", edit_json(a, p, "beta", "BETA"));
    ash_tools_set_confirm(NULL, NULL);

    ASH_CHECK(lg.calls == 1);
    ASH_CHECK(e.is_error);
    ASH_CHECK(strstr(e.content, p) != NULL);
    ASH_CHECK(same(a, p, "alpha\nbeta\ngamma\n", 17));
    unlink(p);
}

static void test_edit_hook_accept(ash_arena *a)
{
    const char *p = wrote(a, "c3.txt", "alpha\nbeta\ngamma\n");
    confirm_log lg = { 0 };
    lg.accept = 1;
    ash_tools_set_confirm(confirm_hook, &lg);
    ash_tool_result e = run(a, "edit", edit_json(a, p, "beta", "BETA"));
    ash_tools_set_confirm(NULL, NULL);

    ASH_CHECK(lg.calls == 1);
    ASH_CHECK(!e.is_error && strstr(e.content, "replaced 1 block") != NULL);
    ASH_CHECK(same(a, p, "alpha\nBETA\ngamma\n", 17));
    unlink(p);
}

static void test_edit_hook_substitutes(ash_arena *a)
{
    const char *p = wrote(a, "c4.txt", "alpha\nbeta\ngamma\n");
    confirm_log lg = { 0 };
    lg.accept = 1;
    lg.subst = "HOST WROTE THIS\n";
    lg.subst_len = 16;
    ash_tools_set_confirm(confirm_hook, &lg);
    ash_tool_result e = run(a, "edit", edit_json(a, p, "beta", "BETA"));
    ash_tools_set_confirm(NULL, NULL);

    ASH_CHECK(lg.calls == 1);
    ASH_CHECK(!e.is_error && strstr(e.content, "replaced 1 block") != NULL);
    ASH_CHECK(same(a, p, "HOST WROTE THIS\n", 16));
    unlink(p);
}

static void test_edit_hook_args(ash_arena *a)
{
    const char *p = wrote(a, "c5.txt", "alpha\nbeta\ngamma\n");
    confirm_log lg = { 0 };
    lg.accept = 1;
    ash_tools_set_confirm(confirm_hook, &lg);
    ash_tool_result e = run(a, "edit", edit_json(a, p, "beta", "BETA"));
    ash_tools_set_confirm(NULL, NULL);

    ASH_CHECK(!e.is_error);
    ASH_CHECK(lg.calls == 1);
    ASH_CHECK_STREQ(lg.path, p);
    ASH_CHECK_STREQ(lg.old, "alpha\nbeta\ngamma\n");
    ASH_CHECK(lg.olen == 17);
    ASH_CHECK_STREQ(lg.neu, "alpha\nBETA\ngamma\n");
    ASH_CHECK(lg.nlen == 17);
    unlink(p);
}

static void test_edit_hook_uninstall(ash_arena *a)
{
    const char *p = wrote(a, "c6.txt", "alpha\nbeta\ngamma\n");
    confirm_log lg = { 0 };
    lg.accept = 0;
    ash_tools_set_confirm(confirm_hook, &lg);
    ash_tools_set_confirm(NULL, NULL);

    ash_tool_result e = run(a, "edit", edit_json(a, p, "beta", "BETA"));
    ASH_CHECK(lg.calls == 0);
    ASH_CHECK(!e.is_error && strstr(e.content, "replaced 1 block") != NULL);
    ASH_CHECK(same(a, p, "alpha\nBETA\ngamma\n", 17));
    unlink(p);
}

static void test_edit_hook_crlf_bom(ash_arena *a)
{
    const char *p = wrote(a, "c7.txt",
                          "\xEF\xBB\xBF" "alpha\r\nbeta\r\ngamma\r\n");
    confirm_log lg = { 0 };
    lg.accept = 1;
    ash_tools_set_confirm(confirm_hook, &lg);
    ash_tool_result e = run(a, "edit", edit_json(a, p, "beta", "BETA"));

    ASH_CHECK(!e.is_error);
    ASH_CHECK(lg.calls == 1);
    ASH_CHECK_STREQ(lg.old, "alpha\nbeta\ngamma\n");
    ASH_CHECK(lg.olen == 17);
    ASH_CHECK_STREQ(lg.neu, "alpha\nBETA\ngamma\n");
    ASH_CHECK(same(a, p, "\xEF\xBB\xBF" "alpha\r\nBETA\r\ngamma\r\n", 23));

    const char *q = wrote(a, "c8.txt",
                          "\xEF\xBB\xBF" "alpha\r\nbeta\r\ngamma\r\n");
    confirm_log lg2 = { 0 };
    lg2.accept = 1;
    lg2.subst = "HOST WROTE THIS\n";
    lg2.subst_len = 16;
    ash_tools_set_confirm(confirm_hook, &lg2);
    ash_tool_result e2 = run(a, "edit", edit_json(a, q, "beta", "BETA"));
    ash_tools_set_confirm(NULL, NULL);

    ASH_CHECK(!e2.is_error);
    ASH_CHECK(same(a, q, "\xEF\xBB\xBF" "HOST WROTE THIS\r\n", 20));
    unlink(p);
    unlink(q);
}

static void test_grep(ash_arena *a)
{
    (void)wrote(a, "g1.txt", "hello world\nfoo bar\nHELLO there\n");
    ash_buf j;
    ash_buf_init(&j, a);
    ash_buf_append_cstr(&j, "{\"pattern\":\"hello\",\"path\":\"");
    ash_buf_append_cstr(&j, jpath(a, "g1.txt"));
    ash_buf_append_cstr(&j, "\"}");
    ash_buf_append_byte(&j, 0);
    ash_tool_result r = run(a, "grep", (const char *)j.data);
    ASH_CHECK(!r.is_error);
    ASH_CHECK(strstr(r.content, ":1:hello world") != NULL);
    ASH_CHECK(strstr(r.content, "HELLO there") == NULL);
}

static void test_ls(ash_arena *a)
{
    ash_buf j;
    ash_buf_init(&j, a);
    ash_buf_append_cstr(&j, "{\"path\":\"");
    ash_buf_append_cstr(&j, g_dir);
    ash_buf_append_cstr(&j, "\"}");
    ash_buf_append_byte(&j, 0);
    ash_tool_result r = run(a, "ls", (const char *)j.data);
    ASH_CHECK(!r.is_error);
    ASH_CHECK(strstr(r.content, "hello.txt") != NULL);
    ASH_CHECK(strstr(r.content, "big.txt") != NULL);
}

int main(void)
{
    ASH_CHECK(mkdtemp(g_dir) != NULL);
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "tools", 1u << 20) == ASH_OK);

    test_write_read(&a);
    test_read_offset_limit(&a);
    test_read_offset_error(&a);
    test_edit_exact(&a);
    test_edit_fuzzy_preserves(&a);
    test_edit_errors(&a);
    test_edit_no_hook(&a);
    test_edit_hook_reject(&a);
    test_edit_hook_accept(&a);
    test_edit_hook_substitutes(&a);
    test_edit_hook_args(&a);
    test_edit_hook_uninstall(&a);
    test_edit_hook_crlf_bom(&a);
    test_grep(&a);
    test_ls(&a);

    ash_arena_destroy(&a);
    return ash_test_done();
}
