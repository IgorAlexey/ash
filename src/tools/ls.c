#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "internal.h"

enum { LS_MAX = 4096 };

struct entry {
    const char *name;
    int         is_dir;
};

static int cmp_entry(const void *x, const void *y)
{
    const struct entry *a = x, *b = y;
    return strcmp(a->name, b->name);
}

ash_status ash_tool_ls(ash_arena *out, const ash_json *args,
                       ash_tool_result *res)
{
    const char *path = NULL;
    if (!tools_arg_str(args, "path", out, &path, NULL))
        path = ".";

    struct stat st;
    if (stat(path, &st) != 0) {
        ash_buf b;
        ash_buf_init(&b, out);
        ash_buf_append_cstr(&b, "Could not list ");
        ash_buf_append_cstr(&b, path);
        tools_result(res, &b, 1);
        return ASH_OK;
    }
    if (!S_ISDIR(st.st_mode)) {
        ash_buf b;
        ash_buf_init(&b, out);
        ash_buf_append_cstr(&b, path);
        ash_buf_append_byte(&b, '\n');
        tools_result(res, &b, 0);
        return ASH_OK;
    }

    ash_arena tmp;
    if (ash_arena_create(&tmp, "ls", 1u << 16) != ASH_OK) {
        tools_error(out, res, "ls: out of memory");
        return ASH_OK;
    }

    DIR *d = opendir(path);
    if (d == NULL) {
        ash_arena_destroy(&tmp);
        ash_buf b;
        ash_buf_init(&b, out);
        ash_buf_append_cstr(&b, "Could not open ");
        ash_buf_append_cstr(&b, path);
        tools_result(res, &b, 1);
        return ASH_OK;
    }

    struct entry *ents = ash_array(&tmp, struct entry, LS_MAX);
    int ne = 0;
    struct dirent *e;
    char full[4096];
    while ((e = readdir(d)) != NULL && ne < LS_MAX) {
        if (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0)
            continue;
        size_t nlen = strlen(e->d_name);
        char *nm = ash_array(&tmp, char, nlen + 1);
        memcpy(nm, e->d_name, nlen + 1);
        int is_dir = 0;
        int n = snprintf(full, sizeof full, "%s/%s", path, e->d_name);
        struct stat es;
        if (n > 0 && (size_t)n < sizeof full && lstat(full, &es) == 0)
            is_dir = S_ISDIR(es.st_mode);
        ents[ne].name = nm;
        ents[ne].is_dir = is_dir;
        ne++;
    }
    closedir(d);
    qsort(ents, (size_t)ne, sizeof ents[0], cmp_entry);

    ash_buf b;
    ash_buf_init(&b, out);
    for (int i = 0; i < ne; i++) {
        ash_buf_append_cstr(&b, ents[i].name);
        if (ents[i].is_dir)
            ash_buf_append_byte(&b, '/');
        ash_buf_append_byte(&b, '\n');
    }
    if (ne == 0)
        ash_buf_append_cstr(&b, "(empty directory)\n");
    ash_arena_destroy(&tmp);
    tools_result(res, &b, 0);
    return ASH_OK;
}
