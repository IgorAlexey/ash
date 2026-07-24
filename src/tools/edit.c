#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

enum { EDIT_MAX = 64 };

static uint32_t utf8_decode(const char *s, size_t len, size_t *adv)
{
    unsigned char c = (unsigned char)s[0];
    if (c < 0x80) {
        *adv = 1;
        return c;
    }
    if ((c & 0xE0) == 0xC0 && len >= 2 && (s[1] & 0xC0) == 0x80) {
        *adv = 2;
        return (uint32_t)((c & 0x1Fu) << 6 | ((unsigned char)s[1] & 0x3Fu));
    }
    if ((c & 0xF0) == 0xE0 && len >= 3 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80) {
        *adv = 3;
        return (uint32_t)((c & 0x0Fu) << 12 | ((unsigned char)s[1] & 0x3Fu) << 6 |
                          ((unsigned char)s[2] & 0x3Fu));
    }
    if ((c & 0xF8) == 0xF0 && len >= 4 && (s[1] & 0xC0) == 0x80 &&
        (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) {
        *adv = 4;
        return (uint32_t)((c & 0x07u) << 18 | ((unsigned char)s[1] & 0x3Fu) << 12 |
                          ((unsigned char)s[2] & 0x3Fu) << 6 |
                          ((unsigned char)s[3] & 0x3Fu));
    }
    *adv = 1;
    return c;
}

static int fold_char(uint32_t cp)
{
    switch (cp) {
    case 0x2018: case 0x2019: case 0x201A: case 0x201B:
        return '\'';
    case 0x201C: case 0x201D: case 0x201E: case 0x201F:
        return '"';
    case 0x2010: case 0x2011: case 0x2012: case 0x2013:
    case 0x2014: case 0x2015: case 0x2212:
        return '-';
    case 0x00A0: case 0x2002: case 0x2003: case 0x2004:
    case 0x2005: case 0x2006: case 0x2007: case 0x2008:
    case 0x2009: case 0x200A: case 0x202F: case 0x205F:
    case 0x3000:
        return ' ';
    default:
        return -1;
    }
}

static int is_trailing_ws(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\f' || c == '\v';
}

const char *ash_edit_normalize(ash_arena *a, const char *s, size_t len,
                               size_t *out_len)
{
    ash_buf repl;
    ash_buf_init(&repl, a);
    size_t i = 0;
    while (i < len) {
        size_t adv = 1;
        uint32_t cp = utf8_decode(s + i, len - i, &adv);
        int folded = adv > 1 ? fold_char(cp) : -1;
        if (folded >= 0)
            ash_buf_append_byte(&repl, (unsigned char)folded);
        else
            ash_buf_append(&repl, s + i, adv);
        i += adv;
    }

    ash_buf out;
    ash_buf_init(&out, a);
    const char *r = (const char *)repl.data;
    size_t rlen = repl.len;
    size_t ls = 0;
    while (ls <= rlen) {
        size_t le = ls;
        while (le < rlen && r[le] != '\n')
            le++;
        size_t end = le;
        while (end > ls && is_trailing_ws(r[end - 1]))
            end--;
        ash_buf_append(&out, r + ls, end - ls);
        if (le < rlen)
            ash_buf_append_byte(&out, '\n');
        if (le >= rlen)
            break;
        ls = le + 1;
    }
    ash_buf_append_byte(&out, 0);
    out.len--;
    if (out_len)
        *out_len = out.len;
    return (const char *)out.data;
}

static const char *mem_find(const char *hay, size_t hlen, const char *needle,
                            size_t nlen)
{
    if (nlen == 0)
        return NULL;
    return (const char *)memmem(hay, hlen, needle, nlen);
}

ash_edit_match ash_edit_find(ash_arena *a, const char *content, size_t clen,
                             const char *old, size_t olen)
{
    ash_edit_match m = { 0 };
    const char *hit = mem_find(content, clen, old, olen);
    if (hit != NULL) {
        m.found = 1;
        m.fuzzy = 0;
        m.index = (size_t)(hit - content);
        m.len = olen;
        m.haystack = content;
        m.haystack_len = clen;
        return m;
    }
    size_t fclen = 0, folen = 0;
    const char *fc = ash_edit_normalize(a, content, clen, &fclen);
    const char *fo = ash_edit_normalize(a, old, olen, &folen);
    const char *fhit = mem_find(fc, fclen, fo, folen);
    if (fhit == NULL)
        return m;
    m.found = 1;
    m.fuzzy = 1;
    m.index = (size_t)(fhit - fc);
    m.len = folen;
    m.haystack = fc;
    m.haystack_len = fclen;
    return m;
}

size_t ash_edit_count(ash_arena *a, const char *content, size_t clen,
                      const char *old, size_t olen)
{
    size_t fclen = 0, folen = 0;
    const char *fc = ash_edit_normalize(a, content, clen, &fclen);
    const char *fo = ash_edit_normalize(a, old, olen, &folen);
    if (folen == 0)
        return 0;
    size_t count = 0, off = 0;
    while (off <= fclen) {
        const char *hit = mem_find(fc + off, fclen - off, fo, folen);
        if (hit == NULL)
            break;
        count++;
        off = (size_t)(hit - fc) + folen;
    }
    return count;
}

static const char *normalize_lf(ash_arena *a, const char *s, size_t len,
                                size_t *out_len)
{
    ash_buf b;
    ash_buf_init(&b, a);
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '\r') {
            ash_buf_append_byte(&b, '\n');
            if (i + 1 < len && s[i + 1] == '\n')
                i++;
        } else {
            ash_buf_append_byte(&b, (unsigned char)s[i]);
        }
    }
    ash_buf_append_byte(&b, 0);
    b.len--;
    if (out_len)
        *out_len = b.len;
    return (const char *)b.data;
}

static size_t *line_offsets(ash_arena *a, const char *s, size_t len,
                            size_t *nlines)
{
    size_t n = 1;
    for (size_t i = 0; i < len; i++)
        if (s[i] == '\n')
            n++;
    size_t *off = ash_array(a, size_t, n + 1);
    size_t k = 0;
    off[k++] = 0;
    for (size_t i = 0; i < len; i++)
        if (s[i] == '\n')
            off[k++] = i + 1;
    off[k] = len;
    *nlines = n;
    return off;
}

struct match {
    size_t index;
    size_t len;
    const char *new_text;
    size_t new_len;
};

static int cmp_match(const void *x, const void *y)
{
    const struct match *a = x, *b = y;
    if (a->index < b->index)
        return -1;
    if (a->index > b->index)
        return 1;
    return 0;
}

static void apply_forward(ash_buf *out, const char *base, size_t start,
                          size_t end, const struct match *ms, int count)
{
    size_t cursor = start;
    for (int i = 0; i < count; i++) {
        ash_buf_append(out, base + cursor, ms[i].index - cursor);
        ash_buf_append(out, ms[i].new_text, ms[i].new_len);
        cursor = ms[i].index + ms[i].len;
    }
    ash_buf_append(out, base + cursor, end - cursor);
}

static const char *apply_preserving(ash_arena *a, const char *orig,
                                    size_t olen, const char *base, size_t blen,
                                    struct match *ms, int count, size_t *out_len)
{
    size_t on = 0, bn = 0;
    size_t *ooff = line_offsets(a, orig, olen, &on);
    size_t *boff = line_offsets(a, base, blen, &bn);
    if (on != bn)
        return NULL;

    struct group {
        size_t start_line;
        size_t end_line;
        struct match ms[EDIT_MAX];
        int count;
    } groups[EDIT_MAX];
    int ng = 0;

    for (int i = 0; i < count; i++) {
        size_t rs = ms[i].index;
        size_t re = ms[i].index + ms[i].len;
        size_t start_line = bn;
        for (size_t l = 0; l < bn; l++) {
            if (rs >= boff[l] && rs < boff[l + 1]) {
                start_line = l;
                break;
            }
        }
        if (start_line == bn)
            return NULL;
        size_t end_line = start_line;
        while (end_line < bn && boff[end_line + 1] < re)
            end_line++;
        if (end_line >= bn)
            return NULL;
        end_line += 1;

        if (ng > 0 && start_line < groups[ng - 1].end_line) {
            struct group *g = &groups[ng - 1];
            if (end_line > g->end_line)
                g->end_line = end_line;
            g->ms[g->count++] = ms[i];
        } else {
            struct group *g = &groups[ng++];
            g->start_line = start_line;
            g->end_line = end_line;
            g->count = 0;
            g->ms[g->count++] = ms[i];
        }
    }

    ash_buf out;
    ash_buf_init(&out, a);
    size_t oidx = 0;
    for (int i = 0; i < ng; i++) {
        struct group *g = &groups[i];
        ash_buf_append(&out, orig + ooff[oidx], ooff[g->start_line] - ooff[oidx]);
        size_t gs = boff[g->start_line];
        size_t ge = boff[g->end_line];
        apply_forward(&out, base, gs, ge, g->ms, g->count);
        oidx = g->end_line;
    }
    ash_buf_append(&out, orig + ooff[oidx], olen - ooff[oidx]);
    ash_buf_append_byte(&out, 0);
    out.len--;
    if (out_len)
        *out_len = out.len;
    return (const char *)out.data;
}

const char *ash_edit_apply(ash_arena *a, const char *norm, size_t nlen,
                           const ash_edit_spec *edits, int ne, size_t *out_len,
                           const char **err)
{
    if (err)
        *err = NULL;
    if (ne <= 0 || ne > EDIT_MAX) {
        if (err)
            *err = "edit: too many replacements in one call";
        return NULL;
    }
    for (int i = 0; i < ne; i++) {
        if (edits[i].olen == 0) {
            if (err)
                *err = "edit: oldText must not be empty";
            return NULL;
        }
    }

    int used_fuzzy = 0;
    for (int i = 0; i < ne; i++) {
        ash_edit_match m = ash_edit_find(a, norm, nlen, edits[i].old,
                                         edits[i].olen);
        if (m.fuzzy)
            used_fuzzy = 1;
    }
    size_t blen = nlen;
    const char *base = norm;
    if (used_fuzzy)
        base = ash_edit_normalize(a, norm, nlen, &blen);

    struct match matched[EDIT_MAX];
    for (int i = 0; i < ne; i++) {
        ash_edit_match m = ash_edit_find(a, base, blen, edits[i].old,
                                         edits[i].olen);
        if (!m.found) {
            if (err)
                *err = "edit: could not find oldText in file";
            return NULL;
        }
        size_t occ = ash_edit_count(a, base, blen, edits[i].old, edits[i].olen);
        if (occ > 1) {
            if (err)
                *err = "edit: oldText is not unique; add more context";
            return NULL;
        }
        matched[i].index = m.index;
        matched[i].len = m.len;
        matched[i].new_text = edits[i].neu;
        matched[i].new_len = edits[i].nlen;
    }

    qsort(matched, (size_t)ne, sizeof matched[0], cmp_match);
    for (int i = 1; i < ne; i++) {
        if (matched[i - 1].index + matched[i - 1].len > matched[i].index) {
            if (err)
                *err = "edit: edits overlap; merge them";
            return NULL;
        }
    }

    size_t newlen = 0;
    const char *newc;
    if (used_fuzzy) {
        newc = apply_preserving(a, norm, nlen, base, blen, matched, ne, &newlen);
        if (newc == NULL) {
            if (err)
                *err = "edit: could not map fuzzy edit onto file";
            return NULL;
        }
    } else {
        ash_buf b;
        ash_buf_init(&b, a);
        apply_forward(&b, base, 0, blen, matched, ne);
        ash_buf_append_byte(&b, 0);
        b.len--;
        newc = (const char *)b.data;
        newlen = b.len;
    }

    if (newlen == nlen && memcmp(newc, norm, nlen) == 0) {
        if (err)
            *err = "edit: replacement produced identical content";
        return NULL;
    }

    if (out_len)
        *out_len = newlen;
    return newc;
}

ash_status ash_tool_edit(ash_arena *out, const ash_json *args,
                         ash_tool_result *res)
{
    const char *path = NULL;
    if (!tools_arg_str(args, "path", out, &path, NULL)) {
        tools_error(out, res, "edit: missing 'path'");
        return ASH_OK;
    }

    ash_edit_spec edits[EDIT_MAX];
    int ne = 0;
    const ash_json *arr = ash_json_get(args, "edits");
    if (arr != NULL && arr->type == ASH_JSON_ARRAY) {
        for (size_t i = 0; i < arr->u.arr.n && ne < EDIT_MAX; i++) {
            const ash_json *e = &arr->u.arr.v[i];
            const char *o, *n;
            size_t ol, nl;
            if (!tools_arg_str(e, "oldText", out, &o, &ol) ||
                !tools_arg_str(e, "newText", out, &n, &nl)) {
                tools_error(out, res, "edit: each edit needs oldText and newText");
                return ASH_OK;
            }
            edits[ne].old = normalize_lf(out, o, ol, &edits[ne].olen);
            edits[ne].neu = normalize_lf(out, n, nl, &edits[ne].nlen);
            ne++;
        }
    } else {
        const char *o, *n;
        size_t ol, nl;
        if (tools_arg_str(args, "oldText", out, &o, &ol) &&
            tools_arg_str(args, "newText", out, &n, &nl)) {
            edits[0].old = normalize_lf(out, o, ol, &edits[0].olen);
            edits[0].neu = normalize_lf(out, n, nl, &edits[0].nlen);
            ne = 1;
        }
    }
    if (ne == 0) {
        tools_error(out, res, "edit: 'edits' must contain at least one replacement");
        return ASH_OK;
    }

    ash_buf file;
    int tf = 0;
    if (tools_read_file(out, path, &file, &tf) != ASH_OK) {
        ash_buf b;
        ash_buf_init(&b, out);
        ash_buf_append_cstr(&b, "Could not edit ");
        ash_buf_append_cstr(&b, path);
        ash_buf_append_cstr(&b, ": ");
        ash_buf_append_cstr(&b, ash_errbuf);
        tools_result(res, &b, 1);
        return ASH_OK;
    }

    const char *raw = (const char *)file.data;
    size_t rawlen = file.len;
    const char *bom = "";
    if (rawlen >= 3 && (unsigned char)raw[0] == 0xEF &&
        (unsigned char)raw[1] == 0xBB && (unsigned char)raw[2] == 0xBF) {
        bom = "\xEF\xBB\xBF";
        raw += 3;
        rawlen -= 3;
    }
    int crlf = 0;
    for (size_t i = 0; i + 1 < rawlen; i++) {
        if (raw[i] == '\r' && raw[i + 1] == '\n') {
            crlf = 1;
            break;
        }
        if (raw[i] == '\n')
            break;
    }
    size_t nlen = 0;
    const char *norm = normalize_lf(out, raw, rawlen, &nlen);

    size_t newlen = 0;
    const char *err = NULL;
    const char *newc = ash_edit_apply(out, norm, nlen, edits, ne, &newlen, &err);
    if (newc == NULL) {
        tools_error(out, res, err ? err : "edit: could not apply edits");
        return ASH_OK;
    }

    ash_buf final;
    ash_buf_init(&final, out);
    ash_buf_append_cstr(&final, bom);
    if (crlf) {
        for (size_t i = 0; i < newlen; i++) {
            if (newc[i] == '\n')
                ash_buf_append_byte(&final, '\r');
            ash_buf_append_byte(&final, (unsigned char)newc[i]);
        }
    } else {
        ash_buf_append(&final, newc, newlen);
    }

    ash_status st = tools_write_file(path, final.data, final.len);
    if (st != ASH_OK) {
        ash_buf b;
        ash_buf_init(&b, out);
        ash_buf_append_cstr(&b, "Could not write ");
        ash_buf_append_cstr(&b, path);
        ash_buf_append_cstr(&b, ": ");
        ash_buf_append_cstr(&b, ash_errbuf);
        tools_result(res, &b, 1);
        return ASH_OK;
    }

    ash_buf b;
    ash_buf_init(&b, out);
    char m[64];
    int mn = snprintf(m, sizeof m, "Successfully replaced %d block(s) in ", ne);
    if (mn > 0)
        ash_buf_append(&b, m, (size_t)mn);
    ash_buf_append_cstr(&b, path);
    tools_result(res, &b, 0);
    return ASH_OK;
}
