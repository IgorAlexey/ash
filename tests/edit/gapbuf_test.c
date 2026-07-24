#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/edit/gapbuf.h"
#include "ash_test.h"

static int buf_is(const ash_gapbuf *b, ash_arena *a, const char *s)
{
    ash_buf out;
    ash_buf_init(&out, a);
    ash_gapbuf_extract(b, &out);
    size_t n = strlen(s);
    return out.len == n && (n == 0 || memcmp(out.data, s, n) == 0);
}

static void test_basic(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "gb", 1u << 16) == ASH_OK);
    ash_gapbuf b;
    ash_gapbuf_init(&b, &a);

    ASH_CHECK(ash_gapbuf_len(&b) == 0);
    ash_gapbuf_insert(&b, 0, "hello", 5);
    ASH_CHECK(ash_gapbuf_len(&b) == 5);
    ASH_CHECK(buf_is(&b, &a, "hello"));

    ash_gapbuf_insert(&b, 5, " world", 6);
    ASH_CHECK(buf_is(&b, &a, "hello world"));

    ash_gapbuf_insert(&b, 5, ",", 1);
    ASH_CHECK(buf_is(&b, &a, "hello, world"));

    ash_gapbuf_erase(&b, 0, 7);
    ASH_CHECK(buf_is(&b, &a, "world"));

    ash_gapbuf_replace(&b, 0, 5, "abc", 3);
    ASH_CHECK(buf_is(&b, &a, "abc"));

    ash_gapbuf_set(&b, "reset", 5);
    ASH_CHECK(buf_is(&b, &a, "reset"));

    ash_gapbuf_clear(&b);
    ASH_CHECK(ash_gapbuf_len(&b) == 0);
    ASH_CHECK(buf_is(&b, &a, ""));

    ash_arena_destroy(&a);
}

static void test_read_at(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "gb", 1u << 16) == ASH_OK);
    ash_gapbuf b;
    ash_gapbuf_init(&b, &a);
    ash_gapbuf_insert(&b, 0, "abcdef", 6);

    ash_gapbuf_insert(&b, 3, "XY", 2);
    ASH_CHECK(buf_is(&b, &a, "abcXYdef"));
    ASH_CHECK(ash_gapbuf_at(&b, 0) == 'a');
    ASH_CHECK(ash_gapbuf_at(&b, 3) == 'X');
    ASH_CHECK(ash_gapbuf_at(&b, 7) == 'f');
    ASH_CHECK(ash_gapbuf_at(&b, 8) == 0);

    uint8_t tmp[16];
    size_t n = ash_gapbuf_copy(&b, 2, tmp, 4);
    ASH_CHECK(n == 4 && memcmp(tmp, "cXYd", 4) == 0);

    ash_arena_destroy(&a);
}

static void test_grow(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "gb", 1u << 16) == ASH_OK);
    ash_gapbuf b;
    ash_gapbuf_init(&b, &a);

    char ref[4001];
    size_t rlen = 0;
    for (int i = 0; i < 4000; i++) {
        char c = (char)('a' + (i % 26));
        size_t pos = rlen ? ((size_t)i * 7u) % (rlen + 1) : 0;
        ash_gapbuf_insert(&b, pos, &c, 1);
        memmove(ref + pos + 1, ref + pos, rlen - pos);
        ref[pos] = c;
        rlen++;
    }
    ASH_CHECK(ash_gapbuf_len(&b) == rlen);
    ash_buf out;
    ash_buf_init(&out, &a);
    ash_gapbuf_extract(&b, &out);
    ASH_CHECK(out.len == rlen && memcmp(out.data, ref, rlen) == 0);

    ash_arena_destroy(&a);
}

static void test_replace_model(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "gb", 1u << 16) == ASH_OK);
    ash_gapbuf b;
    ash_gapbuf_init(&b, &a);

    char ref[512];
    size_t rlen = 0;
    ash_gapbuf_set(&b, "the quick brown fox", 19);
    memcpy(ref, "the quick brown fox", 19);
    rlen = 19;

    struct { size_t s, e; const char *ins; } ops[] = {
        { 4, 9, "slow" }, { 0, 3, "A" }, { 100, 100, "!" },
    };
    for (size_t i = 0; i < sizeof ops / sizeof ops[0]; i++) {
        size_t s = ops[i].s, e = ops[i].e;
        if (s > rlen) s = rlen;
        if (e > rlen) e = rlen;
        size_t il = strlen(ops[i].ins);
        ash_gapbuf_replace(&b, ops[i].s, ops[i].e, ops[i].ins, il);
        memmove(ref + s + il, ref + e, rlen - e);
        memcpy(ref + s, ops[i].ins, il);
        rlen = rlen - (e - s) + il;
    }
    ash_buf out;
    ash_buf_init(&out, &a);
    ash_gapbuf_extract(&b, &out);
    ASH_CHECK(out.len == rlen && memcmp(out.data, ref, rlen) == 0);

    ash_arena_destroy(&a);
}

int main(void)
{
    test_basic();
    test_read_at();
    test_grow();
    test_replace_model();
    return ash_test_done();
}
