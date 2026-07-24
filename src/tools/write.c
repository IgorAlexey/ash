#include <stdio.h>
#include <string.h>

#include "internal.h"

ash_status ash_tool_write(ash_arena *out, const ash_json *args,
                          ash_tool_result *res)
{
    const char *path = NULL;
    const char *content = NULL;
    size_t clen = 0;
    if (!tools_arg_str(args, "path", out, &path, NULL)) {
        tools_error(out, res, "write: missing 'path'");
        return ASH_OK;
    }
    if (!tools_arg_str(args, "content", out, &content, &clen)) {
        tools_error(out, res, "write: missing 'content'");
        return ASH_OK;
    }

    ash_status st = tools_write_file(path, content, clen);
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
    char num[32];
    int n = snprintf(num, sizeof num, "Successfully wrote %zu bytes to ", clen);
    if (n > 0)
        ash_buf_append(&b, num, (size_t)n);
    ash_buf_append_cstr(&b, path);
    tools_result(res, &b, 0);
    return ASH_OK;
}
