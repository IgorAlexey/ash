#include <string.h>

#include "ash/base/json.h"
#include "ash/tools/tools.h"
#include "internal.h"

#define ASH_BASH_SCHEMA \
    "{\"name\":\"bash\"," \
    "\"description\":\"Run a shell command and return its combined output.\"," \
    "\"input_schema\":{\"type\":\"object\"," \
    "\"properties\":{\"command\":{\"type\":\"string\"}}," \
    "\"required\":[\"command\"]}}"

#define ASH_READ_SCHEMA \
    "{\"name\":\"read\"," \
    "\"description\":\"Read a text file. Output is truncated to 2000 lines or " \
    "50KB, whichever is hit first. Use offset (1-indexed line) and limit to " \
    "page through large files.\"," \
    "\"input_schema\":{\"type\":\"object\"," \
    "\"properties\":{\"path\":{\"type\":\"string\"}," \
    "\"offset\":{\"type\":\"integer\"},\"limit\":{\"type\":\"integer\"}}," \
    "\"required\":[\"path\"]}}"

#define ASH_WRITE_SCHEMA \
    "{\"name\":\"write\"," \
    "\"description\":\"Write content to a file, creating parent directories. " \
    "Overwrites if it exists.\"," \
    "\"input_schema\":{\"type\":\"object\"," \
    "\"properties\":{\"path\":{\"type\":\"string\"}," \
    "\"content\":{\"type\":\"string\"}}," \
    "\"required\":[\"path\",\"content\"]}}"

#define ASH_EDIT_SCHEMA \
    "{\"name\":\"edit\"," \
    "\"description\":\"Edit a file by exact text replacement. Each oldText must " \
    "match a unique, non-overlapping region of the file.\"," \
    "\"input_schema\":{\"type\":\"object\"," \
    "\"properties\":{\"path\":{\"type\":\"string\"}," \
    "\"edits\":{\"type\":\"array\",\"items\":{\"type\":\"object\"," \
    "\"properties\":{\"oldText\":{\"type\":\"string\"}," \
    "\"newText\":{\"type\":\"string\"}}," \
    "\"required\":[\"oldText\",\"newText\"]}}}," \
    "\"required\":[\"path\",\"edits\"]}}"

#define ASH_GREP_SCHEMA \
    "{\"name\":\"grep\"," \
    "\"description\":\"Search files for a POSIX extended regular expression. " \
    "Recurses from path (default '.'), skips .git.\"," \
    "\"input_schema\":{\"type\":\"object\"," \
    "\"properties\":{\"pattern\":{\"type\":\"string\"}," \
    "\"path\":{\"type\":\"string\"}}," \
    "\"required\":[\"pattern\"]}}"

#define ASH_LS_SCHEMA \
    "{\"name\":\"ls\"," \
    "\"description\":\"List the entries of a directory (default '.'). " \
    "Directories are marked with a trailing slash.\"," \
    "\"input_schema\":{\"type\":\"object\"," \
    "\"properties\":{\"path\":{\"type\":\"string\"}}}}"

static const char TOOLS_SCHEMA[] =
    "[" ASH_BASH_SCHEMA "," ASH_READ_SCHEMA "," ASH_WRITE_SCHEMA ","
    ASH_EDIT_SCHEMA "," ASH_GREP_SCHEMA "," ASH_LS_SCHEMA "]";

static const ash_tool TOOLS[] = {
    { "bash",  ASH_BASH_SCHEMA,  NULL },
    { "read",  ASH_READ_SCHEMA,  ash_tool_read },
    { "write", ASH_WRITE_SCHEMA, ash_tool_write },
    { "edit",  ASH_EDIT_SCHEMA,  ash_tool_edit },
    { "grep",  ASH_GREP_SCHEMA,  ash_tool_grep },
    { "ls",    ASH_LS_SCHEMA,    ash_tool_ls },
};

enum { TOOLS_COUNT = (int)(sizeof TOOLS / sizeof TOOLS[0]) };

const char *ash_tools_schema(void)
{
    return TOOLS_SCHEMA;
}

const ash_tool *ash_tool_find(const char *name)
{
    if (name == NULL)
        return NULL;
    for (int i = 0; i < TOOLS_COUNT; i++)
        if (strcmp(TOOLS[i].name, name) == 0)
            return &TOOLS[i];
    return NULL;
}

ash_status ash_tool_dispatch(const ash_tool *t, ash_arena *out,
                             const char *input, size_t len,
                             ash_tool_result *res)
{
    res->content = NULL;
    res->len = 0;
    res->is_error = 0;
    if (t == NULL || t->run == NULL)
        return ash_fail(ASH_ERR_STATE, "tool_dispatch: tool is host-executed");
    ash_json v;
    if (input == NULL || ash_json_parse(out, input, len, &v) != ASH_OK) {
        res->content = "tool error: input is not valid JSON";
        res->len = strlen(res->content);
        res->is_error = 1;
        return ASH_OK;
    }
    return t->run(out, &v, res);
}
