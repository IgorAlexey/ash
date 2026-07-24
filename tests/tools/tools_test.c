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
    test_grep(&a);
    test_ls(&a);

    ash_arena_destroy(&a);
    return ash_test_done();
}
