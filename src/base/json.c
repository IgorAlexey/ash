#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/poison.h"

#define ASH_JSON_MAX_DEPTH 128

struct jp {
    const char *p;
    const char *end;
    ash_arena  *a;
};

struct jvec {
    ash_json *v;
    size_t    n;
    size_t    cap;
};

struct jmvec {
    ash_json_member *v;
    size_t           n;
    size_t           cap;
};

static void jvec_push(ash_arena *a, struct jvec *vec, ash_json item)
{
    if (vec->n == vec->cap) {
        size_t nc = vec->cap ? vec->cap * 2 : 8;
        ash_json *nv = ash_array(a, ash_json, nc);
        if (vec->n)
            memcpy(nv, vec->v, vec->n * sizeof *nv);
        vec->v = nv;
        vec->cap = nc;
    }
    vec->v[vec->n++] = item;
}

static void jmvec_push(ash_arena *a, struct jmvec *vec, ash_json_member item)
{
    if (vec->n == vec->cap) {
        size_t nc = vec->cap ? vec->cap * 2 : 8;
        ash_json_member *nv = ash_array(a, ash_json_member, nc);
        if (vec->n)
            memcpy(nv, vec->v, vec->n * sizeof *nv);
        vec->v = nv;
        vec->cap = nc;
    }
    vec->v[vec->n++] = item;
}

static void skip_ws(struct jp *j)
{
    while (j->p < j->end) {
        char c = *j->p;
        if (c != ' ' && c != '\t' && c != '\n' && c != '\r')
            break;
        j->p++;
    }
}

static int hexval(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static ash_status read_hex4(struct jp *j, unsigned *out)
{
    if (j->end - j->p < 4)
        return ash_fail(ASH_ERR_PARSE, "json: truncated \\u escape");
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        int h = hexval(j->p[i]);
        if (h < 0)
            return ash_fail(ASH_ERR_PARSE, "json: bad hex in \\u escape");
        v = (v << 4) | (unsigned)h;
    }
    j->p += 4;
    *out = v;
    return ASH_OK;
}

static void append_utf8(ash_buf *b, unsigned cp)
{
    if (cp < 0x80u) {
        ash_buf_append_byte(b, (unsigned char)cp);
    } else if (cp < 0x800u) {
        ash_buf_append_byte(b, (unsigned char)(0xC0u | (cp >> 6)));
        ash_buf_append_byte(b, (unsigned char)(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        ash_buf_append_byte(b, (unsigned char)(0xE0u | (cp >> 12)));
        ash_buf_append_byte(b, (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu)));
        ash_buf_append_byte(b, (unsigned char)(0x80u | (cp & 0x3Fu)));
    } else {
        ash_buf_append_byte(b, (unsigned char)(0xF0u | (cp >> 18)));
        ash_buf_append_byte(b, (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu)));
        ash_buf_append_byte(b, (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu)));
        ash_buf_append_byte(b, (unsigned char)(0x80u | (cp & 0x3Fu)));
    }
}

static ash_status decode_u(struct jp *j, ash_buf *b)
{
    unsigned hi = 0;
    ASH_TRY(read_hex4(j, &hi));
    if (hi >= 0xD800u && hi <= 0xDBFFu) {
        if (j->end - j->p < 2 || j->p[0] != '\\' || j->p[1] != 'u')
            return ash_fail(ASH_ERR_PARSE, "json: lone high surrogate");
        j->p += 2;
        unsigned lo = 0;
        ASH_TRY(read_hex4(j, &lo));
        if (lo < 0xDC00u || lo > 0xDFFFu)
            return ash_fail(ASH_ERR_PARSE, "json: bad low surrogate");
        unsigned cp = 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u);
        append_utf8(b, cp);
        return ASH_OK;
    }
    if (hi >= 0xDC00u && hi <= 0xDFFFu)
        return ash_fail(ASH_ERR_PARSE, "json: lone low surrogate");
    append_utf8(b, hi);
    return ASH_OK;
}

static ash_status parse_string_raw(struct jp *j, const char **outp, size_t *outn)
{
    if (j->p >= j->end || *j->p != '"')
        return ash_fail(ASH_ERR_PARSE, "json: expected string");
    j->p++;
    ash_buf b;
    ash_buf_init(&b, j->a);
    while (j->p < j->end) {
        unsigned char c = (unsigned char)*j->p;
        if (c == '"') {
            j->p++;
            *outp = (const char *)b.data;
            *outn = b.len;
            return ASH_OK;
        }
        if (c < 0x20)
            return ash_fail(ASH_ERR_PARSE, "json: control char in string");
        if (c != '\\') {
            ash_buf_append_byte(&b, c);
            j->p++;
            continue;
        }
        j->p++;
        if (j->p >= j->end)
            return ash_fail(ASH_ERR_PARSE, "json: truncated escape");
        char e = *j->p++;
        switch (e) {
        case '"':  ash_buf_append_byte(&b, '"');  break;
        case '\\': ash_buf_append_byte(&b, '\\'); break;
        case '/':  ash_buf_append_byte(&b, '/');  break;
        case 'b':  ash_buf_append_byte(&b, '\b'); break;
        case 'f':  ash_buf_append_byte(&b, '\f'); break;
        case 'n':  ash_buf_append_byte(&b, '\n'); break;
        case 'r':  ash_buf_append_byte(&b, '\r'); break;
        case 't':  ash_buf_append_byte(&b, '\t'); break;
        case 'u':  ASH_TRY(decode_u(j, &b));      break;
        default:   return ash_fail(ASH_ERR_PARSE, "json: bad escape");
        }
    }
    return ash_fail(ASH_ERR_PARSE, "json: unterminated string");
}

static ash_status parse_number(struct jp *j, ash_json *out)
{
    const char *start = j->p;
    if (j->p < j->end && *j->p == '-')
        j->p++;
    if (j->p >= j->end)
        return ash_fail(ASH_ERR_PARSE, "json: bad number");
    if (*j->p == '0') {
        j->p++;
    } else if (*j->p >= '1' && *j->p <= '9') {
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
            j->p++;
    } else {
        return ash_fail(ASH_ERR_PARSE, "json: bad number");
    }
    if (j->p < j->end && *j->p == '.') {
        j->p++;
        if (j->p >= j->end || *j->p < '0' || *j->p > '9')
            return ash_fail(ASH_ERR_PARSE, "json: bad number fraction");
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
            j->p++;
    }
    if (j->p < j->end && (*j->p == 'e' || *j->p == 'E')) {
        j->p++;
        if (j->p < j->end && (*j->p == '+' || *j->p == '-'))
            j->p++;
        if (j->p >= j->end || *j->p < '0' || *j->p > '9')
            return ash_fail(ASH_ERR_PARSE, "json: bad number exponent");
        while (j->p < j->end && *j->p >= '0' && *j->p <= '9')
            j->p++;
    }
    size_t nlen = (size_t)(j->p - start);
    char *num = ash_array(j->a, char, nlen);
    memcpy(num, start, nlen);
    out->type = ASH_JSON_NUMBER;
    out->u.num.p = num;
    out->u.num.n = nlen;
    return ASH_OK;
}

static ash_status parse_lit(struct jp *j, const char *lit, size_t n,
                            ash_json_type t, int boolv, ash_json *out)
{
    if ((size_t)(j->end - j->p) < n || memcmp(j->p, lit, n) != 0)
        return ash_fail(ASH_ERR_PARSE, "json: bad literal");
    j->p += n;
    out->type = t;
    out->u.boolean = boolv;
    return ASH_OK;
}

static ash_status parse_value(struct jp *j, int depth, ash_json *out);

static ash_status parse_array(struct jp *j, int depth, ash_json *out)
{
    j->p++;
    struct jvec vec = { NULL, 0, 0 };
    skip_ws(j);
    if (j->p < j->end && *j->p == ']') {
        j->p++;
        out->type = ASH_JSON_ARRAY;
        out->u.arr.v = NULL;
        out->u.arr.n = 0;
        return ASH_OK;
    }
    for (;;) {
        ash_json item;
        ASH_TRY(parse_value(j, depth + 1, &item));
        jvec_push(j->a, &vec, item);
        skip_ws(j);
        if (j->p >= j->end)
            return ash_fail(ASH_ERR_PARSE, "json: unterminated array");
        char c = *j->p++;
        if (c == ',') {
            skip_ws(j);
            continue;
        }
        if (c == ']')
            break;
        return ash_fail(ASH_ERR_PARSE, "json: expected ',' or ']'");
    }
    out->type = ASH_JSON_ARRAY;
    out->u.arr.v = vec.v;
    out->u.arr.n = vec.n;
    return ASH_OK;
}

static ash_status parse_object(struct jp *j, int depth, ash_json *out)
{
    j->p++;
    struct jmvec vec = { NULL, 0, 0 };
    skip_ws(j);
    if (j->p < j->end && *j->p == '}') {
        j->p++;
        out->type = ASH_JSON_OBJECT;
        out->u.obj.v = NULL;
        out->u.obj.n = 0;
        return ASH_OK;
    }
    for (;;) {
        skip_ws(j);
        ash_json_member m;
        ASH_TRY(parse_string_raw(j, &m.key, &m.klen));
        skip_ws(j);
        if (j->p >= j->end || *j->p != ':')
            return ash_fail(ASH_ERR_PARSE, "json: expected ':'");
        j->p++;
        ASH_TRY(parse_value(j, depth + 1, &m.val));
        jmvec_push(j->a, &vec, m);
        skip_ws(j);
        if (j->p >= j->end)
            return ash_fail(ASH_ERR_PARSE, "json: unterminated object");
        char c = *j->p++;
        if (c == ',')
            continue;
        if (c == '}')
            break;
        return ash_fail(ASH_ERR_PARSE, "json: expected ',' or '}'");
    }
    out->type = ASH_JSON_OBJECT;
    out->u.obj.v = vec.v;
    out->u.obj.n = vec.n;
    return ASH_OK;
}

static ash_status parse_value(struct jp *j, int depth, ash_json *out)
{
    if (depth > ASH_JSON_MAX_DEPTH)
        return ash_fail(ASH_ERR_PARSE, "json: max nesting depth exceeded");
    skip_ws(j);
    if (j->p >= j->end)
        return ash_fail(ASH_ERR_PARSE, "json: unexpected end of input");
    char c = *j->p;
    switch (c) {
    case '{':
        return parse_object(j, depth, out);
    case '[':
        return parse_array(j, depth, out);
    case '"':
        out->type = ASH_JSON_STRING;
        return parse_string_raw(j, &out->u.str.p, &out->u.str.n);
    case 't':
        return parse_lit(j, "true", 4, ASH_JSON_BOOL, 1, out);
    case 'f':
        return parse_lit(j, "false", 5, ASH_JSON_BOOL, 0, out);
    case 'n':
        return parse_lit(j, "null", 4, ASH_JSON_NULL, 0, out);
    default:
        if (c == '-' || (c >= '0' && c <= '9'))
            return parse_number(j, out);
        return ash_fail(ASH_ERR_PARSE, "json: unexpected character");
    }
}

ash_status ash_json_parse(ash_arena *a, const char *buf, size_t len,
                          ash_json *out)
{
    if (a == NULL || out == NULL || (buf == NULL && len != 0))
        return ash_fail(ASH_ERR_RANGE, "ash_json_parse: bad arguments");
    if (len > ASH_JSON_MAX_INPUT)
        return ash_fail(ASH_ERR_NOSPACE, "json: input of %zu exceeds %u byte budget",
                        len, (unsigned)ASH_JSON_MAX_INPUT);
    struct jp j = { buf, buf + len, a };
    ASH_TRY(parse_value(&j, 0, out));
    skip_ws(&j);
    if (j.p != j.end)
        return ash_fail(ASH_ERR_PARSE, "json: trailing garbage after value");
    return ASH_OK;
}

const ash_json *ash_json_get(const ash_json *v, const char *key)
{
    if (v == NULL || v->type != ASH_JSON_OBJECT || key == NULL)
        return NULL;
    size_t klen = strlen(key);
    for (size_t i = 0; i < v->u.obj.n; i++) {
        const ash_json_member *m = &v->u.obj.v[i];
        if (m->klen == klen && (klen == 0 || memcmp(m->key, key, klen) == 0))
            return &m->val;
    }
    return NULL;
}

ash_status ash_json_str(const ash_json *v, ash_slice *out)
{
    if (v == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_json_str: bad arguments");
    if (v->type != ASH_JSON_STRING)
        return ash_fail(ASH_ERR_STATE, "ash_json_str: value is not a string");
    *out = ash_slice_make(v->u.str.p, v->u.str.n);
    return ASH_OK;
}

ash_status ash_json_int64(const ash_json *v, int64_t *out)
{
    if (v == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_json_int64: bad arguments");
    if (v->type != ASH_JSON_NUMBER)
        return ash_fail(ASH_ERR_STATE, "ash_json_int64: value is not a number");

    const char *p = v->u.num.p;
    size_t n = v->u.num.n;
    for (size_t i = 0; i < n; i++)
        if (p[i] == '.' || p[i] == 'e' || p[i] == 'E')
            return ash_fail(ASH_ERR_STATE, "ash_json_int64: value is not integral");

    size_t i = 0;
    int neg = 0;
    if (i < n && p[i] == '-') {
        neg = 1;
        i++;
    }
    if (i >= n)
        return ash_fail(ASH_ERR_PARSE, "ash_json_int64: empty number");

    uint64_t lim = neg ? (uint64_t)INT64_MAX + 1u : (uint64_t)INT64_MAX;
    uint64_t mag = 0;
    for (; i < n; i++) {
        unsigned d = (unsigned)(p[i] - '0');
        if (mag > (lim - d) / 10u)
            return ash_fail(ASH_ERR_RANGE, "ash_json_int64: out of range");
        mag = mag * 10u + d;
    }
    if (neg)
        *out = (mag == (uint64_t)INT64_MAX + 1u) ? INT64_MIN : -(int64_t)mag;
    else
        *out = (int64_t)mag;
    return ASH_OK;
}

static size_t utf8_seq_len(const unsigned char *p, size_t n)
{
    if (n == 0)
        return 0;
    unsigned char c = p[0];
    if (c < 0x80)
        return 1;
    size_t len;
    unsigned char lo = 0x80, hi = 0xbf;
    if (c >= 0xc2 && c <= 0xdf) {
        len = 2;
    } else if (c >= 0xe0 && c <= 0xef) {
        len = 3;
        if (c == 0xe0)
            lo = 0xa0;
        else if (c == 0xed)
            hi = 0x9f;
    } else if (c >= 0xf0 && c <= 0xf4) {
        len = 4;
        if (c == 0xf0)
            lo = 0x90;
        else if (c == 0xf4)
            hi = 0x8f;
    } else {
        return 0;
    }
    if (n < len)
        return 0;
    if (p[1] < lo || p[1] > hi)
        return 0;
    for (size_t i = 2; i < len; i++)
        if (p[i] < 0x80 || p[i] > 0xbf)
            return 0;
    return len;
}

static const char hexd[] = "0123456789abcdef";

static void esc_bytes(ash_buf *b, const char *p, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        switch (c) {
        case '"':  ash_buf_append(b, "\\\"", 2); break;
        case '\\': ash_buf_append(b, "\\\\", 2); break;
        case '\n': ash_buf_append(b, "\\n", 2); break;
        case '\r': ash_buf_append(b, "\\r", 2); break;
        case '\t': ash_buf_append(b, "\\t", 2); break;
        case '\b': ash_buf_append(b, "\\b", 2); break;
        case '\f': ash_buf_append(b, "\\f", 2); break;
        default:
            if (c < 0x20) {
                const char u[6] = { '\\', 'u', '0', '0',
                                    hexd[c >> 4], hexd[c & 0xf] };
                ash_buf_append(b, u, sizeof u);
            } else if (c < 0x80) {
                ash_buf_append_byte(b, c);
            } else {
                size_t len = utf8_seq_len((const unsigned char *)p + i, n - i);
                if (len > 0) {
                    ash_buf_append(b, p + i, len);
                    i += len - 1;
                } else {
                    ash_buf_append(b, "\\ufffd", 6);
                }
            }
        }
    }
}

void ash_json_quote(ash_buf *b, const char *p, size_t n)
{
    ash_buf_append_byte(b, '"');
    esc_bytes(b, p, n);
    ash_buf_append_byte(b, '"');
}

void ash_json_quote_cstr(ash_buf *b, const char *s)
{
    assert(s != NULL);
    ash_json_quote(b, s, strlen(s));
}
