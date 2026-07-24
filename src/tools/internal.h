#ifndef ASH_TOOLS_INTERNAL_H
#define ASH_TOOLS_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/status.h"
#include "ash/tools/tools.h"

enum { TOOLS_MAX_LINES = 2000 };
enum { TOOLS_MAX_BYTES = 50u * 1024u };
enum { TOOLS_FILE_CAP = 16u * 1024u * 1024u };
enum { TOOLS_GREP_LINE_MAX = 500 };
enum { TOOLS_WALK_MAX = 20000 };

ash_status ash_tool_read(ash_arena *out, const ash_json *args, ash_tool_result *res);
ash_status ash_tool_write(ash_arena *out, const ash_json *args, ash_tool_result *res);
ash_status ash_tool_edit(ash_arena *out, const ash_json *args, ash_tool_result *res);
ash_status ash_tool_grep(ash_arena *out, const ash_json *args, ash_tool_result *res);
ash_status ash_tool_ls(ash_arena *out, const ash_json *args, ash_tool_result *res);

int tools_arg_str(const ash_json *args, const char *key, ash_arena *a,
                  const char **out, size_t *len);
int tools_arg_int(const ash_json *args, const char *key, int64_t *out);

void tools_result(ash_tool_result *res, ash_buf *b, int is_error);
void tools_error(ash_arena *a, ash_tool_result *res, const char *msg);

ash_status tools_read_file(ash_arena *a, const char *path, ash_buf *out,
                           int *truncated);
ash_status tools_write_file(const char *path, const void *data, size_t len);

void tools_append_size(ash_buf *b, size_t bytes);

#endif
