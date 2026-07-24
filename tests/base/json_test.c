#include <stdint.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash_test.h"

static ash_arena g_a;

static ash_status parse(const char *s, ash_json *out)
{
    return ash_json_parse(&g_a, s, strlen(s), out);
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

    ash_arena_destroy(&g_a);
    return ash_test_done();
}
