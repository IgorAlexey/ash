#include <stdio.h>
#include <string.h>

#include "internal.h"

static size_t line_count(const char *s, size_t len)
{
    if (len == 0)
        return 0;
    size_t nl = 0;
    for (size_t i = 0; i < len; i++)
        if (s[i] == '\n')
            nl++;
    return s[len - 1] == '\n' ? nl : nl + 1;
}

static size_t line_start(const char *s, size_t len, size_t line)
{
    size_t idx = 0, seen = 0;
    while (seen < line && idx < len) {
        if (s[idx] == '\n')
            seen++;
        idx++;
    }
    return idx;
}

struct trunc {
    size_t out_len;
    size_t out_lines;
    size_t total_lines;
    int    truncated;
    int    by_bytes;
    int    first_exceeds;
    size_t first_line_bytes;
};

static struct trunc truncate_head(const char *s, size_t len)
{
    struct trunc t = { 0 };
    t.total_lines = line_count(s, len);
    if (t.total_lines <= TOOLS_MAX_LINES && len <= TOOLS_MAX_BYTES) {
        t.out_len = len;
        t.out_lines = t.total_lines;
        return t;
    }

    size_t first_len = 0;
    while (first_len < len && s[first_len] != '\n')
        first_len++;
    if (first_len > TOOLS_MAX_BYTES) {
        t.truncated = 1;
        t.by_bytes = 1;
        t.first_exceeds = 1;
        t.first_line_bytes = first_len;
        return t;
    }

    size_t out_bytes = 0, count = 0, prefix_end = 0, i = 0;
    while (i < len && count < TOOLS_MAX_LINES) {
        size_t line_len = 0;
        while (i + line_len < len && s[i + line_len] != '\n')
            line_len++;
        size_t cost = line_len + (count > 0 ? 1 : 0);
        if (out_bytes + cost > TOOLS_MAX_BYTES) {
            t.by_bytes = 1;
            break;
        }
        out_bytes += cost;
        prefix_end = i + line_len;
        count++;
        i += line_len + 1;
    }
    if (count >= TOOLS_MAX_LINES && out_bytes <= TOOLS_MAX_BYTES)
        t.by_bytes = 0;
    t.truncated = 1;
    t.out_len = prefix_end;
    t.out_lines = count;
    return t;
}

ash_status ash_tool_read(ash_arena *out, const ash_json *args,
                         ash_tool_result *res)
{
    const char *path = NULL;
    if (!tools_arg_str(args, "path", out, &path, NULL)) {
        tools_error(out, res, "read: missing 'path'");
        return ASH_OK;
    }
    int64_t offset = 0, limit = 0;
    int have_offset = tools_arg_int(args, "offset", &offset);
    int have_limit = tools_arg_int(args, "limit", &limit);

    ash_buf file;
    int trunc_file = 0;
    if (tools_read_file(out, path, &file, &trunc_file) != ASH_OK) {
        ash_buf b;
        ash_buf_init(&b, out);
        ash_buf_append_cstr(&b, "Could not read ");
        ash_buf_append_cstr(&b, path);
        ash_buf_append_cstr(&b, ": ");
        ash_buf_append_cstr(&b, ash_errbuf);
        tools_result(res, &b, 1);
        return ASH_OK;
    }

    const char *c = (const char *)file.data;
    size_t L = file.len;
    size_t nf = 1;
    for (size_t i = 0; i < L; i++)
        if (c[i] == '\n')
            nf++;

    size_t start = 0;
    if (have_offset && offset > 1)
        start = (size_t)offset - 1;
    if (start >= nf) {
        ash_buf b;
        ash_buf_init(&b, out);
        char m[128];
        int n = snprintf(m, sizeof m,
                         "Offset %lld is beyond end of file (%zu lines total)",
                         (long long)offset, nf);
        if (n > 0)
            ash_buf_append(&b, m, (size_t)n);
        tools_result(res, &b, 1);
        return ASH_OK;
    }

    size_t sel_start = line_start(c, L, start);
    size_t sel_end = L;
    size_t user_lines = 0;
    int user_limited = 0;
    if (have_limit && limit >= 0) {
        size_t e = start + (size_t)limit;
        if (e > nf)
            e = nf;
        user_lines = e - start;
        user_limited = 1;
        if (e < nf) {
            size_t es = line_start(c, L, e);
            sel_end = es > 0 ? es - 1 : 0;
        }
    }
    const char *sel = c + sel_start;
    size_t sel_len = sel_end > sel_start ? sel_end - sel_start : 0;

    struct trunc t = truncate_head(sel, sel_len);
    size_t start_disp = start + 1;

    ash_buf b;
    ash_buf_init(&b, out);
    if (t.first_exceeds) {
        char m[256];
        char szline[32], szmax[32];
        ash_buf tb1;
        ash_buf_init(&tb1, out);
        tools_append_size(&tb1, t.first_line_bytes);
        ash_buf_append_byte(&tb1, 0);
        snprintf(szline, sizeof szline, "%s", (const char *)tb1.data);
        ash_buf tb2;
        ash_buf_init(&tb2, out);
        tools_append_size(&tb2, TOOLS_MAX_BYTES);
        ash_buf_append_byte(&tb2, 0);
        snprintf(szmax, sizeof szmax, "%s", (const char *)tb2.data);
        int n = snprintf(m, sizeof m,
                         "[Line %zu is %s, exceeds %s limit. Use bash: sed -n "
                         "'%zup' %s | head -c %u]",
                         start_disp, szline, szmax, start_disp, path,
                         (unsigned)TOOLS_MAX_BYTES);
        if (n > 0)
            ash_buf_append(&b, m, (size_t)n);
        tools_result(res, &b, 0);
        return ASH_OK;
    }

    ash_buf_append(&b, sel, t.out_len);
    if (t.truncated) {
        size_t end_disp = start_disp + t.out_lines - 1;
        size_t next = end_disp + 1;
        char m[160];
        int n;
        if (t.by_bytes) {
            char szmax[32];
            ash_buf tb;
            ash_buf_init(&tb, out);
            tools_append_size(&tb, TOOLS_MAX_BYTES);
            ash_buf_append_byte(&tb, 0);
            snprintf(szmax, sizeof szmax, "%s", (const char *)tb.data);
            n = snprintf(m, sizeof m,
                         "\n\n[Showing lines %zu-%zu of %zu (%s limit). Use "
                         "offset=%zu to continue.]",
                         start_disp, end_disp, nf, szmax, next);
        } else {
            n = snprintf(m, sizeof m,
                         "\n\n[Showing lines %zu-%zu of %zu. Use offset=%zu to "
                         "continue.]",
                         start_disp, end_disp, nf, next);
        }
        if (n > 0)
            ash_buf_append(&b, m, (size_t)n);
    } else if (user_limited && start + user_lines < nf) {
        size_t remaining = nf - (start + user_lines);
        size_t next = start + user_lines + 1;
        char m[128];
        int n = snprintf(m, sizeof m,
                         "\n\n[%zu more lines in file. Use offset=%zu to "
                         "continue.]",
                         remaining, next);
        if (n > 0)
            ash_buf_append(&b, m, (size_t)n);
    }
    tools_result(res, &b, 0);
    return ASH_OK;
}
