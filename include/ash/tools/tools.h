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

typedef enum ash_tool_kind {
    ASH_TOOL_PURE = 0,
    ASH_TOOL_SHELL
} ash_tool_kind;

typedef struct ash_tool {
    const char   *name;
    const char   *schema;
    ash_tool_kind kind;
    ash_tool_fn   run;
} ash_tool;

_Static_assert(sizeof(ash_tool) <= 4 * sizeof(void *),
               "ash_tool must stay a struct of pointers: ash_tools_register "
               "copies the struct by value and never copies the bytes behind "
               "name or schema, so the registrant owns both strings and must "
               "keep them allocated and unmodified from a successful "
               "ash_tools_register until its ash_tools_unregister_owner "
               "returns; a const ash_tool * from ash_tool_find is valid over "
               "that same window and is invalidated only by the unregister of "
               "the owner that registered it, never by another owner's; grow "
               "this struct to carry storage of its own and every one of "
               "those rules changes");

enum { ASH_TOOLS_EXTRA_CAP = 16 };
enum { ASH_TOOL_SCHEMA_MAX = 8192 };
enum { ASH_TOOL_SCHEMA_ARENA = ASH_TOOL_SCHEMA_MAX * 512 };

ASH_API const char     *ash_tools_schema(void);
ASH_API const ash_tool *ash_tool_find(const char *name);

ASH_API ASH_WUR ash_status ash_tools_register(const ash_tool *borrowed,
                                              const void *owner);
ASH_API void ash_tools_unregister_owner(const void *owner);
ASH_API ASH_WUR ash_status ash_tools_schema_build(ash_arena *a,
                                                  const char **out);

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

typedef int (*ash_confirm_fn)(void *ud, const char *path,
                              const char *old, size_t olen,
                              const char *neu, size_t nlen,
                              const char **edited, size_t *edited_len);

ASH_API void ash_tools_set_confirm(ash_confirm_fn fn, void *ud);

#endif

