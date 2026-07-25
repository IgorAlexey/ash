#include <string.h>

#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
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
    "\"description\":\"Read a text file. Output is truncated to 2000 lines " \
    "or 50KB, whichever is hit first. Use offset (1-indexed line) and " \
    "limit to page through large files.\"," \
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
    "\"description\":\"Edit a file by exact text replacement. Each oldText " \
    "must match a unique, non-overlapping region of the file.\"," \
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
    { "bash",  ASH_BASH_SCHEMA,  ASH_TOOL_SHELL, NULL },
    { "read",  ASH_READ_SCHEMA,  ASH_TOOL_PURE,  ash_tool_read },
    { "write", ASH_WRITE_SCHEMA, ASH_TOOL_PURE,  ash_tool_write },
    { "edit",  ASH_EDIT_SCHEMA,  ASH_TOOL_PURE,  ash_tool_edit },
    { "grep",  ASH_GREP_SCHEMA,  ASH_TOOL_PURE,  ash_tool_grep },
    { "ls",    ASH_LS_SCHEMA,    ASH_TOOL_PURE,  ash_tool_ls },
};

enum { TOOLS_COUNT = (int)(sizeof TOOLS / sizeof TOOLS[0]) };

_Static_assert(ASH_TOOL_SCHEMA_ARENA / ASH_TOOL_SCHEMA_MAX >= 256,
               "the schema-check arena must hold the whole parse of the "
               "largest schema ash_tools_register accepts in its first chunk, "
               "because the schema is script-controlled input and "
               "ash_arena_alloc calls ash_die rather than returning when it "
               "has to grow and the growth malloc fails: every nested array "
               "in json.c costs 8 node slots of 24 bytes the moment it takes "
               "one element, and nothing is freed until the parse ends, so "
               "two input bytes of '[' and ']' buy 192 arena bytes and the "
               "worst case is a run of maximally nested chains, measured at "
               "99x the input across every nesting depth from 2 to the "
               "ASH_JSON_MAX_DEPTH of 128, against the 50x the ash_json "
               "_Static_assert quotes for ordinary documents; 512x leaves 5x "
               "over that measured worst case, and 256x is the floor below "
               "which a single chunk stops being provably enough, so the only "
               "allocation that can still fail is the one malloc in "
               "ash_arena_create, which reports ASH_ERR_NOMEM instead of "
               "dying");

typedef struct extra_tool {
    ash_tool    tool;
    const void *owner;
} extra_tool;

static extra_tool g_extra_main_thread_only[ASH_TOOLS_EXTRA_CAP];

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
    for (int i = 0; i < ASH_TOOLS_EXTRA_CAP; i++) {
        const extra_tool *x = &g_extra_main_thread_only[i];
        if (x->owner != NULL && strcmp(x->tool.name, name) == 0)
            return &x->tool;
    }
    return NULL;
}

static const ash_json_member *repeated_key(const ash_json *v)
{
    if (v->type == ASH_JSON_ARRAY) {
        for (size_t i = 0; i < v->u.arr.n; i++) {
            const ash_json_member *m = repeated_key(&v->u.arr.v[i]);
            if (m != NULL)
                return m;
        }
        return NULL;
    }
    if (v->type != ASH_JSON_OBJECT)
        return NULL;
    for (size_t i = 0; i < v->u.obj.n; i++) {
        const ash_json_member *m = &v->u.obj.v[i];
        for (size_t j = 0; j < i; j++) {
            const ash_json_member *e = &v->u.obj.v[j];
            if (e->klen == m->klen &&
                (m->klen == 0 || memcmp(e->key, m->key, m->klen) == 0))
                return m;
        }
        const ash_json_member *d = repeated_key(&m->val);
        if (d != NULL)
            return d;
    }
    return NULL;
}

static ash_status schema_check(const ash_tool *borrowed)
{
    size_t n = strlen(borrowed->schema);
    if (n > ASH_TOOL_SCHEMA_MAX)
        return ash_fail(ASH_ERR_RANGE,
                        "tools_register: schema for '%s' is %zu bytes, over "
                        "the %d byte limit", borrowed->name, n,
                        ASH_TOOL_SCHEMA_MAX);

    ash_arena a;
    ASH_TRY(ash_arena_create(&a, "tools-schema-check",
                             (size_t)ASH_TOOL_SCHEMA_ARENA));

    ash_json v;
    ash_status st = ASH_OK;
    if (ash_json_parse(&a, borrowed->schema, n, &v) != ASH_OK) {
        st = ash_fail(ASH_ERR_PARSE,
                      "tools_register: schema for '%s' is not valid JSON",
                      borrowed->name);
    } else if (v.type != ASH_JSON_OBJECT) {
        st = ash_fail(ASH_ERR_PARSE,
                      "tools_register: schema for '%s' is not a JSON object",
                      borrowed->name);
    } else {
        const ash_json_member *dup = repeated_key(&v);
        const ash_json *nm = ash_json_get(&v, "name");
        ash_slice s;
        if (dup != NULL)
            st = ash_fail(ASH_ERR_PARSE,
                          "tools_register: schema for '%s' repeats the key "
                          "\"%.*s\"", borrowed->name, (int)dup->klen, dup->key);
        else if (nm == NULL || ash_json_str(nm, &s) != ASH_OK ||
                 !ash_slice_eq_cstr(s, borrowed->name))
            st = ash_fail(ASH_ERR_PARSE,
                          "tools_register: schema for '%s' has no matching "
                          "\"name\" member", borrowed->name);
    }

    ash_arena_destroy(&a);
    return st;
}

ash_status ash_tools_register(const ash_tool *borrowed, const void *owner)
{
    if (borrowed == NULL || borrowed->name == NULL || borrowed->schema == NULL)
        return ash_fail(ASH_ERR_RANGE,
                        "tools_register: tool needs a name and a schema");
    if (owner == NULL)
        return ash_fail(ASH_ERR_RANGE,
                        "tools_register: tool '%s' needs an owner",
                        borrowed->name);
    if (borrowed->kind != ASH_TOOL_PURE)
        return ash_fail(ASH_ERR_RANGE,
                        "tools_register: tool '%s' is host-executed; only "
                        "pure tools can be registered", borrowed->name);
    if (ash_tool_find(borrowed->name) != NULL)
        return ash_fail(ASH_ERR_STATE,
                        "tools_register: tool '%s' is already registered",
                        borrowed->name);

    int slot = -1;
    for (int i = 0; i < ASH_TOOLS_EXTRA_CAP; i++) {
        if (g_extra_main_thread_only[i].owner != NULL)
            continue;
        slot = i;
        break;
    }
    if (slot < 0)
        return ash_fail(ASH_ERR_NOSPACE,
                        "tools_register: registry is full (max %d extra tools)",
                        ASH_TOOLS_EXTRA_CAP);
    ASH_TRY(schema_check(borrowed));

    g_extra_main_thread_only[slot].tool = *borrowed;
    g_extra_main_thread_only[slot].owner = owner;
    return ASH_OK;
}

void ash_tools_unregister_owner(const void *owner)
{
    if (owner == NULL)
        return;
    for (int i = 0; i < ASH_TOOLS_EXTRA_CAP; i++) {
        if (g_extra_main_thread_only[i].owner != owner)
            continue;
        memset(&g_extra_main_thread_only[i], 0,
               sizeof g_extra_main_thread_only[i]);
    }
}

ash_status ash_tools_schema_build(ash_arena *a, const char **out)
{
    if (a == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "tools_schema_build: null argument");
    *out = NULL;

    ash_buf b;
    ash_buf_init(&b, a);
    ash_buf_append_byte(&b, '[');
    for (int i = 0; i < TOOLS_COUNT; i++) {
        if (i > 0)
            ash_buf_append_byte(&b, ',');
        ash_buf_append_cstr(&b, TOOLS[i].schema);
    }
    for (int i = 0; i < ASH_TOOLS_EXTRA_CAP; i++) {
        if (g_extra_main_thread_only[i].owner == NULL)
            continue;
        ash_buf_append_byte(&b, ',');
        ash_buf_append_cstr(&b, g_extra_main_thread_only[i].tool.schema);
    }
    ash_buf_append_byte(&b, ']');
    ash_buf_append_byte(&b, 0);

    *out = (const char *)b.data;
    return ASH_OK;
}

ash_status ash_tool_dispatch(const ash_tool *t, ash_arena *out,
                             const char *input, size_t len,
                             ash_tool_result *res)
{
    res->content = NULL;
    res->len = 0;
    res->is_error = 0;
    if (t == NULL)
        return ash_fail(ASH_ERR_STATE, "tool_dispatch: no such tool");
    if (t->kind != ASH_TOOL_PURE)
        return ash_fail(ASH_ERR_STATE,
                        "tool_dispatch: tool '%s' is host-executed", t->name);
    if (t->run == NULL)
        return ash_fail(ASH_ERR_STATE,
                        "tool_dispatch: tool '%s' has no run function",
                        t->name);
    ash_json v;
    if (input == NULL || ash_json_parse(out, input, len, &v) != ASH_OK) {
        res->content = "tool error: input is not valid JSON";
        res->len = strlen(res->content);
        res->is_error = 1;
        return ASH_OK;
    }
    return t->run(out, &v, res);
}
