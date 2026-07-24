#include <dirent.h>
#include <regex.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "internal.h"

enum { GREP_MAX_MATCHES = 2000 };

struct grep_ctx {
    regex_t   *re;
    ash_arena *tmp;
    ash_buf   *out;
    int        matches;
    int        capped;
    int        files;
};

static int is_binary(const char *data, size_t len)
{
    size_t n = len < 4096 ? len : 4096;
    for (size_t i = 0; i < n; i++)
        if (data[i] == 0)
            return 1;
    return 0;
}

static void grep_file(struct grep_ctx *g, const char *path)
{
    if (g->capped)
        return;
    ash_arena_reset(g->tmp);
    ash_buf file;
    int tf = 0;
    if (tools_read_file(g->tmp, path, &file, &tf) != ASH_OK)
        return;
    const char *c = (const char *)file.data;
    size_t len = file.len;
    if (is_binary(c, len))
        return;

    size_t ls = 0;
    int lineno = 0;
    char line[8192];
    while (ls <= len) {
        size_t le = ls;
        while (le < len && c[le] != '\n')
            le++;
        lineno++;
        size_t llen = le - ls;
        size_t copy = llen < sizeof line - 1 ? llen : sizeof line - 1;
        memcpy(line, c + ls, copy);
        line[copy] = 0;
        if (regexec(g->re, line, 0, NULL, 0) == 0) {
            if (g->matches >= GREP_MAX_MATCHES || g->out->len >= TOOLS_MAX_BYTES) {
                g->capped = 1;
                return;
            }
            g->matches++;
            ash_buf_append_cstr(g->out, path);
            ash_buf_append_byte(g->out, ':');
            char num[16];
            int nn = snprintf(num, sizeof num, "%d:", lineno);
            if (nn > 0)
                ash_buf_append(g->out, num, (size_t)nn);
            if (llen > TOOLS_GREP_LINE_MAX) {
                ash_buf_append(g->out, c + ls, TOOLS_GREP_LINE_MAX);
                ash_buf_append_cstr(g->out, "... [truncated]");
            } else {
                ash_buf_append(g->out, c + ls, llen);
            }
            ash_buf_append_byte(g->out, '\n');
        }
        if (le >= len)
            break;
        ls = le + 1;
    }
}

static void grep_walk(struct grep_ctx *g, const char *dir)
{
    if (g->capped || g->files >= TOOLS_WALK_MAX)
        return;
    DIR *d = opendir(dir);
    if (d == NULL)
        return;
    struct dirent *e;
    char path[4096];
    while ((e = readdir(d)) != NULL && !g->capped) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        if (strcmp(e->d_name, ".git") == 0)
            continue;
        int n = snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        if (n <= 0 || (size_t)n >= sizeof path)
            continue;
        struct stat st;
        if (lstat(path, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            grep_walk(g, path);
        } else if (S_ISREG(st.st_mode)) {
            g->files++;
            grep_file(g, path);
        }
    }
    closedir(d);
}

ash_status ash_tool_grep(ash_arena *out, const ash_json *args,
                         ash_tool_result *res)
{
    const char *pattern = NULL, *path = NULL;
    if (!tools_arg_str(args, "pattern", out, &pattern, NULL)) {
        tools_error(out, res, "grep: missing 'pattern'");
        return ASH_OK;
    }
    if (!tools_arg_str(args, "path", out, &path, NULL))
        path = ".";

    regex_t re;
    if (regcomp(&re, pattern, REG_EXTENDED | REG_NEWLINE) != 0) {
        tools_error(out, res, "grep: invalid regular expression");
        return ASH_OK;
    }

    ash_arena tmp;
    if (ash_arena_create(&tmp, "grep-file", 1u << 16) != ASH_OK) {
        regfree(&re);
        tools_error(out, res, "grep: out of memory");
        return ASH_OK;
    }

    ash_buf b;
    ash_buf_init(&b, out);
    struct grep_ctx g = { .re = &re, .tmp = &tmp, .out = &b };

    struct stat st;
    if (stat(path, &st) == 0 && S_ISREG(st.st_mode))
        grep_file(&g, path);
    else
        grep_walk(&g, path);

    regfree(&re);
    ash_arena_destroy(&tmp);

    if (g.matches == 0) {
        ash_buf_append_cstr(&b, "No matches found.");
    } else if (g.capped) {
        ash_buf_append_cstr(&b, "\n[Results truncated. Narrow the pattern or path.]");
    }
    tools_result(res, &b, 0);
    return ASH_OK;
}
