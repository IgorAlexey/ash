#include "ash/ui/fuzzy.h"

enum { FUZZY_MAX_TARGET = 4096, FUZZY_MAX_QUERY = ASH_FUZZY_MAX_POS };

static uint32_t fold_case(uint32_t cp)
{
    return cp >= 'A' && cp <= 'Z' ? cp + 32u : cp;
}

static size_t utf8_next(const char *s, size_t len, uint32_t *cp)
{
    uint8_t b0 = (uint8_t)s[0];
    if (b0 < 0x80) {
        *cp = b0;
        return 1;
    }
    int extra;
    uint32_t v;
    if ((b0 & 0xe0) == 0xc0) {
        extra = 1;
        v = b0 & 0x1fu;
    } else if ((b0 & 0xf0) == 0xe0) {
        extra = 2;
        v = b0 & 0x0fu;
    } else if ((b0 & 0xf8) == 0xf0) {
        extra = 3;
        v = b0 & 0x07u;
    } else {
        *cp = b0;
        return 1;
    }
    if (len < (size_t)(extra + 1)) {
        *cp = b0;
        return 1;
    }
    for (int i = 1; i <= extra; i++) {
        uint8_t b = (uint8_t)s[i];
        if ((b & 0xc0) != 0x80) {
            *cp = b0;
            return 1;
        }
        v = (v << 6) | (b & 0x3fu);
    }
    *cp = v;
    return (size_t)(extra + 1);
}

static int decode_cps(ash_arena *a, const char *s, size_t len,
                      uint32_t **out_cp, uint32_t **out_lo, int cap)
{
    uint32_t *cp = ash_array(a, uint32_t, (size_t)cap);
    uint32_t *lo = ash_array(a, uint32_t, (size_t)cap);
    int n = 0;
    size_t i = 0;
    while (i < len) {
        if (n >= cap)
            return -1;
        uint32_t c;
        size_t adv = utf8_next(s + i, len - i, &c);
        i += adv;
        cp[n] = c;
        lo[n] = fold_case(c);
        n++;
    }
    *out_cp = cp;
    *out_lo = lo;
    return n;
}

static int consider_as_equal(uint32_t a, uint32_t b)
{
    return a == b || (a == '/' && b == '\\') || (a == '\\' && b == '/');
}

static int32_t score_separator(uint32_t ch)
{
    switch (ch) {
    case '/':
    case '\\':
        return 5;
    case '_':
    case '-':
    case '.':
    case ' ':
    case '\'':
    case '"':
    case ':':
        return 4;
    default:
        return 0;
    }
}

static int32_t char_score(uint32_t query, uint32_t query_lower,
                          int have_prev, uint32_t target_prev,
                          uint32_t target_curr, uint32_t target_curr_lower,
                          int32_t seq_len)
{
    if (!consider_as_equal(query_lower, target_curr_lower))
        return 0;

    int32_t score = 1;
    if (seq_len > 0)
        score += seq_len * 5;
    if (query == target_curr)
        score += 1;

    if (have_prev) {
        int32_t sep = score_separator(target_prev);
        if (sep > 0)
            score += sep;
        else if (target_curr != target_curr_lower && seq_len == 0)
            score += 2;
    } else {
        score += 8;
    }
    return score;
}

static int starts_with(const uint32_t *lo, int tn, int at, const uint32_t *q, int qn)
{
    if (tn - at < qn)
        return 0;
    for (int i = 0; i < qn; i++)
        if (lo[at + i] != q[i])
            return 0;
    return 1;
}

int32_t ash_fuzzy_score(ash_arena *scratch, const char *haystack, size_t hlen,
                        const char *needle, size_t nlen,
                        int allow_non_contiguous, ash_fuzzy_match *out)
{
    out->score = 0;
    out->npos = 0;
    if (hlen == 0 || nlen == 0)
        return 0;

    uint32_t *target, *target_lower, *query, *query_lower;
    int tn = decode_cps(scratch, haystack, hlen, &target, &target_lower,
                        FUZZY_MAX_TARGET);
    int qn = decode_cps(scratch, needle, nlen, &query, &query_lower,
                        FUZZY_MAX_QUERY);
    if (tn <= 0 || qn <= 0 || tn < qn)
        return 0;

    size_t area = (size_t)qn * (size_t)tn;
    int32_t *scores = ash_array(scratch, int32_t, area);
    int32_t *matches = ash_array(scratch, int32_t, area);
    for (size_t i = 0; i < area; i++) {
        scores[i] = 0;
        matches[i] = 0;
    }

    for (int qi = 0; qi < qn; qi++) {
        int qoff = qi * tn;
        int qprev = qi > 0 ? (qi - 1) * tn : 0;
        for (int ti = 0; ti < tn; ti++) {
            int cur = qoff + ti;
            int diag = (qi > 0 && ti > 0) ? qprev + ti - 1 : 0;
            int32_t left = ti > 0 ? scores[cur - 1] : 0;
            int32_t diag_score = (qi > 0 && ti > 0) ? scores[diag] : 0;
            int32_t seq = (qi > 0 && ti > 0) ? matches[diag] : 0;

            int32_t sc;
            if (diag_score == 0 && qi != 0) {
                sc = 0;
            } else {
                sc = char_score(query[qi], query_lower[qi], ti != 0,
                                ti != 0 ? target[ti - 1] : 0, target[ti],
                                target_lower[ti], seq);
            }

            int valid = sc != 0 && diag_score + sc >= left &&
                        (allow_non_contiguous || qi > 0 ||
                         starts_with(target_lower, tn, ti, query_lower, qn));
            if (valid) {
                matches[cur] = seq + 1;
                scores[cur] = diag_score + sc;
            } else {
                matches[cur] = 0;
                scores[cur] = left;
            }
        }
    }

    int32_t pos[ASH_FUZZY_MAX_POS];
    int np = 0;
    int qi = qn - 1;
    int ti = tn - 1;
    for (;;) {
        int cur = qi * tn + ti;
        if (matches[cur] == 0) {
            if (ti == 0)
                break;
            ti--;
        } else {
            if (np < ASH_FUZZY_MAX_POS)
                pos[np++] = ti;
            if (qi == 0 || ti == 0)
                break;
            qi--;
            ti--;
        }
    }
    for (int i = 0; i < np; i++)
        out->positions[i] = pos[np - 1 - i];
    out->npos = np;
    out->score = scores[area - 1];
    return out->score;
}
