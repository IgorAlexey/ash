#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash_test.h"

static ash_arena g_a;

struct esc_case {
    const char          *name;
    const unsigned char *in;
    size_t               len;
    const char          *want;
    size_t               wantlen;
    const unsigned char *rt;
    size_t               rtlen;
};

#define WANT_LEN(s) (sizeof s - 1)
#define WANT(s)     s, WANT_LEN(s)

_Static_assert(WANT_LEN("\"a\0b\"") == 5, "WANT_LEN counts bytes");

static size_t want_len_of(const char *s, size_t n)
{
    (void)s;
    return n;
}

static ash_status parse(const char *s, ash_json *out)
{
    return ash_json_parse(&g_a, s, strlen(s), out);
}

static void hexdump(const void *p, size_t n)
{
    const unsigned char *b = p;
    for (size_t i = 0; i < n; i++)
        fprintf(stderr, "%s%02x", i ? " " : "", b[i]);
}

static void dump_mismatch(const char *kind, const char *name,
                          const void *want, size_t wn,
                          const void *got, size_t gn)
{
    fprintf(stderr, "%s case \"%s\": want [", kind, name);
    hexdump(want, wn);
    fprintf(stderr, "] got [");
    hexdump(got, gn);
    fprintf(stderr, "]\n");
}

static int str_is(const ash_json *v, const char *want)
{
    ash_slice sl;
    if (ash_json_str(v, &sl) != ASH_OK)
        return 0;
    return sl.len == strlen(want) && memcmp(sl.p, want, sl.len) == 0;
}

int main(void)
{
    ASH_CHECK(ash_arena_create(&g_a, "json", 1u << 20) == ASH_OK);

    ash_json v;

    ASH_CHECK(parse("null", &v) == ASH_OK && v.type == ASH_JSON_NULL);
    ASH_CHECK(parse("true", &v) == ASH_OK && v.type == ASH_JSON_BOOL && v.u.boolean == 1);
    ASH_CHECK(parse("false", &v) == ASH_OK && v.type == ASH_JSON_BOOL && v.u.boolean == 0);
    ASH_CHECK(parse("  \n\t 42 ", &v) == ASH_OK && v.type == ASH_JSON_NUMBER);

    int64_t n = 0;
    ASH_CHECK(parse("42", &v) == ASH_OK && ash_json_int64(&v, &n) == ASH_OK && n == 42);
    ASH_CHECK(parse("-7", &v) == ASH_OK && ash_json_int64(&v, &n) == ASH_OK && n == -7);
    ASH_CHECK(parse("0", &v) == ASH_OK && ash_json_int64(&v, &n) == ASH_OK && n == 0);
    ASH_CHECK(parse("9223372036854775807", &v) == ASH_OK &&
              ash_json_int64(&v, &n) == ASH_OK && n == INT64_MAX);
    ASH_CHECK(parse("-9223372036854775808", &v) == ASH_OK &&
              ash_json_int64(&v, &n) == ASH_OK && n == INT64_MIN);
    ASH_CHECK(parse("9223372036854775808", &v) == ASH_OK &&
              ash_json_int64(&v, &n) == ASH_ERR_RANGE);
    ASH_CHECK(parse("1.5", &v) == ASH_OK && ash_json_int64(&v, &n) == ASH_ERR_STATE);
    ASH_CHECK(parse("1e3", &v) == ASH_OK && ash_json_int64(&v, &n) == ASH_ERR_STATE);

    ASH_CHECK(parse("\"hello\"", &v) == ASH_OK && str_is(&v, "hello"));
    ASH_CHECK(parse("\"\"", &v) == ASH_OK && v.type == ASH_JSON_STRING && v.u.str.n == 0);
    ASH_CHECK(parse("\"a\\nb\\tc\\\"d\\\\e\\/f\"", &v) == ASH_OK && str_is(&v, "a\nb\tc\"d\\e/f"));
    ASH_CHECK(parse("\"\\u0041\\u00e9\"", &v) == ASH_OK && str_is(&v, "A\xc3\xa9"));
    ASH_CHECK(parse("\"\\ud83d\\ude00\"", &v) == ASH_OK && str_is(&v, "\xf0\x9f\x98\x80"));

    ASH_CHECK(parse("[]", &v) == ASH_OK && v.type == ASH_JSON_ARRAY && v.u.arr.n == 0);
    ASH_CHECK(parse("{}", &v) == ASH_OK && v.type == ASH_JSON_OBJECT && v.u.obj.n == 0);
    ASH_CHECK(parse("[1, 2, 3]", &v) == ASH_OK && v.type == ASH_JSON_ARRAY && v.u.arr.n == 3);
    ASH_CHECK(ash_json_int64(&v.u.arr.v[2], &n) == ASH_OK && n == 3);

    ASH_CHECK(parse("{\"a\": 1, \"b\": \"x\"}", &v) == ASH_OK && v.type == ASH_JSON_OBJECT);
    const ash_json *a = ash_json_get(&v, "a");
    const ash_json *b = ash_json_get(&v, "b");
    ASH_CHECK(a != NULL && ash_json_int64(a, &n) == ASH_OK && n == 1);
    ASH_CHECK(b != NULL && str_is(b, "x"));
    ASH_CHECK(ash_json_get(&v, "missing") == NULL);

    const char *sse =
        "{\"type\":\"content_block_delta\",\"index\":0,"
        "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello, world\"}}";
    ASH_CHECK(parse(sse, &v) == ASH_OK);
    const ash_json *delta = ash_json_get(&v, "delta");
    ASH_CHECK(delta != NULL);
    const ash_json *text = ash_json_get(delta, "text");
    ASH_CHECK(text != NULL && str_is(text, "Hello, world"));
    const ash_json *idx = ash_json_get(&v, "index");
    ASH_CHECK(idx != NULL && ash_json_int64(idx, &n) == ASH_OK && n == 0);

    ASH_CHECK(parse("", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("   ", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("42 43", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("\"unterminated", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("\"bad\\xescape\"", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("\"\\ud83d\"", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("\"\\udc00\"", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("[1, 2", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("[1 2]", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("{\"a\" 1}", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("{\"a\": 1,}", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("01", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("-", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("1.", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("1e", &v) == ASH_ERR_PARSE);
    ASH_CHECK(parse("nul", &v) == ASH_ERR_PARSE);

    char deep[200];
    memset(deep, '[', sizeof deep);
    ASH_CHECK(ash_json_parse(&g_a, deep, sizeof deep, &v) == ASH_ERR_PARSE);

    size_t over = (size_t)ASH_JSON_MAX_INPUT + 1;
    char *huge = ash_arena_alloc(&g_a, over, 1);
    memset(huge, '0', over);
    ASH_CHECK(ash_json_parse(&g_a, huge, over, &v) == ASH_ERR_NOSPACE);

    ash_buf big;
    ash_buf_init(&big, &g_a);
    ash_buf_append_byte(&big, '[');
    for (int i = 0; i < 50000; i++) {
        if (i)
            ash_buf_append_byte(&big, ',');
        ash_buf_append_byte(&big, '0');
    }
    ash_buf_append_byte(&big, ']');
    ASH_CHECK(ash_json_parse(&g_a, (const char *)big.data, big.len, &v) == ASH_OK &&
              v.type == ASH_JSON_ARRAY && v.u.arr.n == 50000);

    const struct esc_case esc_cases[] = {
        { "nul 0x00", (const unsigned char[]){ 0x00 }, 1, WANT("\"\\u0000\""),
          (const unsigned char[]){ 0x00 }, 1 },
        { "quote", (const unsigned char[]){ 0x22 }, 1, WANT("\"\\\"\""),
          (const unsigned char[]){ 0x22 }, 1 },
        { "backslash", (const unsigned char[]){ 0x5c }, 1, WANT("\"\\\\\""),
          (const unsigned char[]){ 0x5c }, 1 },
        { "newline", (const unsigned char[]){ 0x0a }, 1, WANT("\"\\n\""),
          (const unsigned char[]){ 0x0a }, 1 },
        { "carriage return", (const unsigned char[]){ 0x0d }, 1, WANT("\"\\r\""),
          (const unsigned char[]){ 0x0d }, 1 },
        { "tab", (const unsigned char[]){ 0x09 }, 1, WANT("\"\\t\""),
          (const unsigned char[]){ 0x09 }, 1 },
        { "backspace", (const unsigned char[]){ 0x08 }, 1, WANT("\"\\b\""),
          (const unsigned char[]){ 0x08 }, 1 },
        { "form feed", (const unsigned char[]){ 0x0c }, 1, WANT("\"\\f\""),
          (const unsigned char[]){ 0x0c }, 1 },
        { "c0 0x01", (const unsigned char[]){ 0x01 }, 1, WANT("\"\\u0001\""),
          (const unsigned char[]){ 0x01 }, 1 },
        { "c0 0x1f", (const unsigned char[]){ 0x1f }, 1, WANT("\"\\u001f\""),
          (const unsigned char[]){ 0x1f }, 1 },
        { "del 0x7f verbatim", (const unsigned char[]){ 0x7f }, 1,
          WANT("\"\x7f\""), (const unsigned char[]){ 0x7f }, 1 },
        { "ascii passthrough", (const unsigned char *)"hi there!", 9,
          WANT("\"hi there!\""), (const unsigned char *)"hi there!", 9 },
        { "2-byte c3 a9", (const unsigned char[]){ 0xc3, 0xa9 }, 2,
          WANT("\"\xc3\xa9\""), (const unsigned char[]){ 0xc3, 0xa9 }, 2 },
        { "3-byte e4 b8 ad", (const unsigned char[]){ 0xe4, 0xb8, 0xad }, 3,
          WANT("\"\xe4\xb8\xad\""),
          (const unsigned char[]){ 0xe4, 0xb8, 0xad }, 3 },
        { "4-byte f0 9f 98 80",
          (const unsigned char[]){ 0xf0, 0x9f, 0x98, 0x80 }, 4,
          WANT("\"\xf0\x9f\x98\x80\""),
          (const unsigned char[]){ 0xf0, 0x9f, 0x98, 0x80 }, 4 },
        { "overlong c0 80", (const unsigned char[]){ 0xc0, 0x80 }, 2,
          WANT("\"\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd }, 6 },
        { "overlong c1 bf", (const unsigned char[]){ 0xc1, 0xbf }, 2,
          WANT("\"\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd }, 6 },
        { "c2 80 is U+0080", (const unsigned char[]){ 0xc2, 0x80 }, 2,
          WANT("\"\xc2\x80\""), (const unsigned char[]){ 0xc2, 0x80 }, 2 },
        { "overlong e0 80 80", (const unsigned char[]){ 0xe0, 0x80, 0x80 }, 3,
          WANT("\"\\ufffd\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd,
                                   0xef, 0xbf, 0xbd }, 9 },
        { "overlong e0 9f bf", (const unsigned char[]){ 0xe0, 0x9f, 0xbf }, 3,
          WANT("\"\\ufffd\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd,
                                   0xef, 0xbf, 0xbd }, 9 },
        { "e0 a0 80 is U+0800", (const unsigned char[]){ 0xe0, 0xa0, 0x80 }, 3,
          WANT("\"\xe0\xa0\x80\""),
          (const unsigned char[]){ 0xe0, 0xa0, 0x80 }, 3 },
        { "ed 9f bf is U+D7FF", (const unsigned char[]){ 0xed, 0x9f, 0xbf }, 3,
          WANT("\"\xed\x9f\xbf\""),
          (const unsigned char[]){ 0xed, 0x9f, 0xbf }, 3 },
        { "surrogate ed a0 80", (const unsigned char[]){ 0xed, 0xa0, 0x80 }, 3,
          WANT("\"\\ufffd\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd,
                                   0xef, 0xbf, 0xbd }, 9 },
        { "ee 80 80 is U+E000", (const unsigned char[]){ 0xee, 0x80, 0x80 }, 3,
          WANT("\"\xee\x80\x80\""),
          (const unsigned char[]){ 0xee, 0x80, 0x80 }, 3 },
        { "overlong f0 80 80 80",
          (const unsigned char[]){ 0xf0, 0x80, 0x80, 0x80 }, 4,
          WANT("\"\\ufffd\\ufffd\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd,
                                   0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd }, 12 },
        { "overlong f0 8f bf bf",
          (const unsigned char[]){ 0xf0, 0x8f, 0xbf, 0xbf }, 4,
          WANT("\"\\ufffd\\ufffd\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd,
                                   0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd }, 12 },
        { "f0 90 80 80 is U+10000",
          (const unsigned char[]){ 0xf0, 0x90, 0x80, 0x80 }, 4,
          WANT("\"\xf0\x90\x80\x80\""),
          (const unsigned char[]){ 0xf0, 0x90, 0x80, 0x80 }, 4 },
        { "f4 8f bf bf is U+10FFFF",
          (const unsigned char[]){ 0xf4, 0x8f, 0xbf, 0xbf }, 4,
          WANT("\"\xf4\x8f\xbf\xbf\""),
          (const unsigned char[]){ 0xf4, 0x8f, 0xbf, 0xbf }, 4 },
        { "f4 90 80 80 is above U+10FFFF",
          (const unsigned char[]){ 0xf4, 0x90, 0x80, 0x80 }, 4,
          WANT("\"\\ufffd\\ufffd\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd,
                                   0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd }, 12 },
        { "lead f5", (const unsigned char[]){ 0xf5 }, 1, WANT("\"\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd }, 3 },
        { "lead fe", (const unsigned char[]){ 0xfe }, 1, WANT("\"\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd }, 3 },
        { "lead ff", (const unsigned char[]){ 0xff }, 1, WANT("\"\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd }, 3 },
        { "2-byte lead truncated at end",
          (const unsigned char[]){ 0x61, 0xc3 }, 2, WANT("\"a\\ufffd\""),
          (const unsigned char[]){ 0x61, 0xef, 0xbf, 0xbd }, 4 },
        { "3-byte lead with one continuation",
          (const unsigned char[]){ 0xe4, 0xb8 }, 2,
          WANT("\"\\ufffd\\ufffd\""),
          (const unsigned char[]){ 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd }, 6 },
        { "lone continuation 0x80", (const unsigned char[]){ 0x80 }, 1,
          WANT("\"\\ufffd\""), (const unsigned char[]){ 0xef, 0xbf, 0xbd }, 3 },
        { "empty input, non-null pointer", (const unsigned char *)"", 0,
          WANT("\"\""), (const unsigned char *)"", 0 },
        { "empty input, null pointer", NULL, 0, WANT("\"\""), NULL, 0 },
        { "mixed blob",
          (const unsigned char[]){ 0x61, 0x22, 0x5c, 0x0a, 0x09, 0x0d, 0x08,
                                   0x0c, 0x01, 0x1f, 0x7f, 0xc3, 0xa9, 0xff,
                                   0xf0, 0x9f, 0x98, 0x80, 0xed, 0xa0, 0x80,
                                   0x7a }, 22,
          WANT("\"a\\\"\\\\\\n\\t\\r\\b\\f\\u0001\\u001f\x7f\xc3\xa9\\ufffd"
               "\xf0\x9f\x98\x80\\ufffd\\ufffd\\ufffdz\""),
          (const unsigned char[]){ 0x61, 0x22, 0x5c, 0x0a, 0x09, 0x0d, 0x08,
                                   0x0c, 0x01, 0x1f, 0x7f, 0xc3, 0xa9, 0xef,
                                   0xbf, 0xbd, 0xf0, 0x9f, 0x98, 0x80, 0xef,
                                   0xbf, 0xbd, 0xef, 0xbf, 0xbd, 0xef, 0xbf,
                                   0xbd, 0x7a }, 30 },
    };

    ASH_CHECK(want_len_of(WANT("\"a\0b\"")) == 5);

    for (size_t i = 0; i < sizeof esc_cases / sizeof esc_cases[0]; i++) {
        const struct esc_case *c = &esc_cases[i];
        const char *in = (const char *)c->in;

        ash_buf q;
        ash_buf_init(&q, &g_a);
        ash_json_quote(&q, in, c->len);
        int ok = q.len == c->wantlen &&
                 memcmp(q.data, c->want, c->wantlen) == 0;
        if (!ok)
            dump_mismatch("quote", c->name, c->want, c->wantlen, q.data, q.len);
        ASH_CHECK(ok);

        ash_json rt;
        ash_slice rts = { NULL, 0 };
        int rtok = ash_json_parse(&g_a, (const char *)q.data, q.len, &rt) == ASH_OK &&
                   rt.type == ASH_JSON_STRING &&
                   ash_json_str(&rt, &rts) == ASH_OK &&
                   rts.len == c->rtlen &&
                   (c->rtlen == 0 || memcmp(rts.p, c->rt, c->rtlen) == 0);
        if (!rtok)
            dump_mismatch("round trip", c->name, c->rt, c->rtlen,
                          rts.p, rts.len);
        ASH_CHECK(rtok);
    }

    ash_arena_destroy(&g_a);
    return ash_test_done();
}
