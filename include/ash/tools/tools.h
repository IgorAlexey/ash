#ifndef ASH_TOOLS_TOOLS_H
#define ASH_TOOLS_TOOLS_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/json.h"
#include "ash/base/status.h"

typedef struct ash_tool_result {
    const char *content;
    size_t      len;
    int         is_error;
} ash_tool_result;

typedef ash_status (*ash_tool_fn)(ash_arena *out, const ash_json *args,
                                  ash_tool_result *res);

typedef struct ash_tool {
    const char *name;
    const char *schema;
    ash_tool_fn run;
} ash_tool;

ASH_API const char     *ash_tools_schema(void);
ASH_API const ash_tool *ash_tool_find(const char *name);

ASH_API ASH_WUR ash_status ash_tool_dispatch(const ash_tool *t, ash_arena *out,
                                             const char *input, size_t len,
                                             ash_tool_result *res);

ASH_API ASH_WUR ash_status ash_bash_command(ash_arena *a, const char *input,
                                            size_t len, const char **cmd);

typedef struct ash_edit_match {
    int         found;
    int         fuzzy;
    size_t      index;
    size_t      len;
    const char *haystack;
    size_t      haystack_len;
} ash_edit_match;

ASH_API const char *ash_edit_normalize(ash_arena *a, const char *s, size_t len,
                                       size_t *out_len);

ASH_API ash_edit_match ash_edit_find(ash_arena *a, const char *content,
                                     size_t clen, const char *old, size_t olen);

ASH_API size_t ash_edit_count(ash_arena *a, const char *content, size_t clen,
                              const char *old, size_t olen);

typedef struct ash_edit_spec {
    const char *old;
    size_t      olen;
    const char *neu;
    size_t      nlen;
} ash_edit_spec;

ASH_API const char *ash_edit_apply(ash_arena *a, const char *content,
                                   size_t clen, const ash_edit_spec *edits,
                                   int ne, size_t *out_len, const char **err);

#endif

