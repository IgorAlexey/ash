#include <stdio.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
#include "ash/tools/tools.h"
#include "ash_test.h"

static int owner_a;
static int owner_b;

static size_t schema_array_len(ash_arena *a, const char *s)
{
    ash_json v;
    ASH_CHECK(ash_json_parse(a, s, strlen(s), &v) == ASH_OK);
    ASH_CHECK(v.type == ASH_JSON_ARRAY);
    return v.type == ASH_JSON_ARRAY ? v.u.arr.n : 0;
}

static void test_schema(ash_arena *a)
{
    const char *schema = ash_tools_schema();
    ASH_CHECK(schema != NULL);
    ash_json v;
    ASH_CHECK(ash_json_parse(a, schema, strlen(schema), &v) == ASH_OK);
    ASH_CHECK(v.type == ASH_JSON_ARRAY && v.u.arr.n >= 1);

    int saw_bash = 0;
    for (size_t i = 0; i < v.u.arr.n; i++) {
        const ash_json *nm = ash_json_get(&v.u.arr.v[i], "name");
        ash_slice s;
        ASH_CHECK(nm != NULL && ash_json_str(nm, &s) == ASH_OK);
        ASH_CHECK(ash_json_get(&v.u.arr.v[i], "input_schema") != NULL);
        if (ash_slice_eq_cstr(s, "bash"))
            saw_bash = 1;
    }
    ASH_CHECK(saw_bash);
}

static void test_find(void)
{
    const ash_tool *bash = ash_tool_find("bash");
    ASH_CHECK(bash != NULL && strcmp(bash->name, "bash") == 0);
    ASH_CHECK(bash->kind == ASH_TOOL_SHELL);
    ASH_CHECK(bash->run == NULL);
    ASH_CHECK(ash_tool_find("does-not-exist") == NULL);
    ASH_CHECK(ash_tool_find(NULL) == NULL);
}

static void test_bash_command(ash_arena *a)
{
    const char *cmd = NULL;
    const char *in = "{\"command\":\"echo hi\"}";
    ASH_CHECK(ash_bash_command(a, in, strlen(in), &cmd) == ASH_OK);
    ASH_CHECK(cmd != NULL && strcmp(cmd, "echo hi") == 0);

    cmd = (const char *)1;
    ASH_CHECK(ash_bash_command(a, "{}", 2, &cmd) != ASH_OK && cmd == NULL);

    cmd = (const char *)1;
    ASH_CHECK(ash_bash_command(a, "not json", 8, &cmd) != ASH_OK);
    ASH_CHECK(cmd == NULL);

    cmd = (const char *)1;
    ASH_CHECK(ash_bash_command(a, NULL, 0, &cmd) != ASH_OK && cmd == NULL);
}

static ash_status extra_run(ash_arena *out, const ash_json *args,
                            ash_tool_result *res)
{
    const ash_json *who = ash_json_get(args, "who");
    ash_slice s;
    if (who == NULL || ash_json_str(who, &s) != ASH_OK)
        return ash_fail(ASH_ERR_RANGE, "extra: no 'who' string");

    ash_buf b;
    ash_buf_init(&b, out);
    ash_buf_append_cstr(&b, "hello ");
    ash_buf_append(&b, s.p, s.len);
    res->len = b.len;
    ash_buf_append_byte(&b, 0);
    res->content = (const char *)b.data;
    res->is_error = 0;
    return ASH_OK;
}

static const ash_tool EXTRA_TOOL = {
    "extra",
    "{\"name\":\"extra\",\"description\":\"an extension tool\","
    "\"input_schema\":{\"type\":\"object\"}}",
    ASH_TOOL_PURE,
    extra_run
};

static const ash_tool TOOL_A = {
    "owner-a-tool",
    "{\"name\":\"owner-a-tool\",\"input_schema\":{\"type\":\"object\"}}",
    ASH_TOOL_PURE,
    extra_run
};

static const ash_tool TOOL_B = {
    "owner-b-tool",
    "{\"name\":\"owner-b-tool\",\"input_schema\":{\"type\":\"object\"}}",
    ASH_TOOL_PURE,
    extra_run
};

static void test_register(ash_arena *a)
{
    ASH_CHECK(ash_tool_find("extra") == NULL);
    ASH_CHECK(ash_tools_register(&EXTRA_TOOL, &owner_a) == ASH_OK);

    const ash_tool *t = ash_tool_find("extra");
    ASH_CHECK(t != NULL && strcmp(t->name, "extra") == 0);
    ASH_CHECK(t->kind == ASH_TOOL_PURE && t->run == extra_run);

    ASH_CHECK(ash_tools_register(&EXTRA_TOOL, &owner_a) == ASH_ERR_STATE);
    ASH_CHECK(ash_tools_register(&EXTRA_TOOL, &owner_b) == ASH_ERR_STATE);
    ASH_CHECK(ash_tool_find("bash") != NULL);
    ASH_CHECK(ash_tool_find("ls") != NULL);
    ASH_CHECK(ash_tool_find("does-not-exist") == NULL);

    const char *schema = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &schema) == ASH_OK);
    ASH_CHECK(schema != NULL);
    ASH_CHECK(strstr(schema, "\"bash\"") != NULL);
    ASH_CHECK(strstr(schema, "\"extra\"") != NULL);

    size_t with_extra = schema_array_len(a, schema);

    ash_tools_unregister_owner(&owner_a);
    ASH_CHECK(ash_tool_find("extra") == NULL);
    ASH_CHECK(ash_tool_find("bash") != NULL);

    schema = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &schema) == ASH_OK);
    ASH_CHECK(strstr(schema, "\"extra\"") == NULL);
    ASH_CHECK(schema_array_len(a, schema) + 1 == with_extra);
}

static void test_dispatch(ash_arena *a)
{
    ASH_CHECK(ash_tools_register(&EXTRA_TOOL, &owner_a) == ASH_OK);
    const ash_tool *t = ash_tool_find("extra");
    ASH_CHECK(t != NULL);

    ash_tool_result res;
    const char *in = "{\"who\":\"world\"}";
    ASH_CHECK(ash_tool_dispatch(t, a, in, strlen(in), &res) == ASH_OK);
    ASH_CHECK(res.is_error == 0);
    ASH_CHECK(res.content != NULL);
    ASH_CHECK_STREQ(res.content, "hello world");
    ASH_CHECK(res.len == strlen("hello world"));

    ASH_CHECK(ash_tool_dispatch(t, a, "{}", 2, &res) == ASH_ERR_RANGE);

    ASH_CHECK(ash_tool_dispatch(t, a, "not json", 8, &res) == ASH_OK);
    ASH_CHECK(res.is_error == 1 && res.content != NULL);

    ASH_CHECK(ash_tool_dispatch(t, a, NULL, 0, &res) == ASH_OK);
    ASH_CHECK(res.is_error == 1);

    ASH_CHECK(ash_tool_dispatch(ash_tool_find("bash"), a, "{}", 2, &res)
              == ASH_ERR_STATE);
    char host_msg[ASH_ERRBUF_CAP];
    memcpy(host_msg, ash_errbuf, sizeof host_msg);
    ASH_CHECK(strstr(host_msg, "bash") != NULL);

    ASH_CHECK(ash_tool_dispatch(NULL, a, "{}", 2, &res) == ASH_ERR_STATE);
    ASH_CHECK(strcmp(ash_errbuf, host_msg) != 0);
    ASH_CHECK(strstr(ash_errbuf, "bash") == NULL);

    ash_tools_unregister_owner(&owner_a);
}

static void test_register_rejects(void)
{
    ash_tool bad = EXTRA_TOOL;
    bad.name = NULL;
    ASH_CHECK(ash_tools_register(&bad, &owner_a) == ASH_ERR_RANGE);

    bad = EXTRA_TOOL;
    bad.schema = NULL;
    ASH_CHECK(ash_tools_register(&bad, &owner_a) == ASH_ERR_RANGE);

    ASH_CHECK(ash_tools_register(NULL, &owner_a) == ASH_ERR_RANGE);
    ASH_CHECK(ash_tools_register(&EXTRA_TOOL, NULL) == ASH_ERR_RANGE);
    ASH_CHECK(ash_tool_find("extra") == NULL);
}

static void test_register_rejects_shell(ash_arena *a)
{
    const char *base = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &base) == ASH_OK);

    ash_tool shell = EXTRA_TOOL;
    shell.kind = ASH_TOOL_SHELL;
    ASH_CHECK(ash_tools_register(&shell, &owner_a) == ASH_ERR_RANGE);
    ASH_CHECK(strstr(ash_errbuf, "extra") != NULL);
    ASH_CHECK(ash_tool_find("extra") == NULL);

    shell.run = NULL;
    ASH_CHECK(ash_tools_register(&shell, &owner_a) == ASH_ERR_RANGE);
    ASH_CHECK(ash_tool_find("extra") == NULL);

    const char *after = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &after) == ASH_OK);
    ASH_CHECK(strstr(after, "\"extra\"") == NULL);
    ASH_CHECK(schema_array_len(a, after) == schema_array_len(a, base));

    ash_tool pure = EXTRA_TOOL;
    ASH_CHECK(ash_tools_register(&pure, &owner_a) == ASH_OK);
    ASH_CHECK(ash_tool_find("extra") != NULL);
    ash_tools_unregister_owner(&owner_a);
}

static void test_duplicate_keys(ash_arena *a)
{
    static const char doc[] = "{\"name\":\"first\",\"name\":\"last\"}";
    ash_json v;
    ASH_CHECK(ash_json_parse(a, doc, strlen(doc), &v) == ASH_OK);
    ASH_CHECK(v.type == ASH_JSON_OBJECT && v.u.obj.n == 2);
    ash_slice s;
    ASH_CHECK(ash_json_str(ash_json_get(&v, "name"), &s) == ASH_OK);
    ASH_CHECK(ash_slice_eq_cstr(s, "first"));
    ASH_CHECK(ash_json_str(&v.u.obj.v[1].val, &s) == ASH_OK);
    ASH_CHECK(ash_slice_eq_cstr(s, "last"));

    static const char *const dup_schemas[] = {
        "{\"name\":\"dup\",\"name\":\"other\"}",
        "{\"name\":\"other\",\"name\":\"dup\"}",
        "{\"name\":\"dup\",\"input_schema\":{\"type\":\"object\"},"
        "\"name\":\"other\"}",
        "{\"name\":\"dup\",\"input_schema\":{\"type\":\"object\","
        "\"properties\":{\"a\":{\"type\":\"string\"},"
        "\"a\":{\"type\":\"integer\"}}}}",
        "{\"name\":\"dup\",\"input_schema\":{\"type\":\"object\","
        "\"type\":\"array\"}}",
        "{\"name\":\"dup\",\"input_schema\":{\"items\":["
        "{\"type\":\"object\"},{\"x\":1,\"x\":2}]}}",
        "{\"name\":\"dup\",\"a\":1,\"b\":2,\"a\":3}",
    };

    ash_tool t = EXTRA_TOOL;
    t.name = "dup";
    for (size_t i = 0; i < sizeof dup_schemas / sizeof dup_schemas[0]; i++) {
        t.schema = dup_schemas[i];
        ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_ERR_PARSE);
        ASH_CHECK(strstr(ash_errbuf, "dup") != NULL);
        ASH_CHECK(ash_tool_find("dup") == NULL);
    }

    t.schema = "{\"name\":\"dup\",\"input_schema\":{\"type\":\"object\","
               "\"properties\":{\"a\":{\"type\":\"string\"},"
               "\"b\":{\"type\":\"string\"}}}}";
    ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_OK);
    ASH_CHECK(ash_tool_find("dup") != NULL);
    ash_tools_unregister_owner(&owner_a);
    ASH_CHECK(ash_tool_find("dup") == NULL);
}

static void test_schema_rejects(ash_arena *a)
{
    static const char *const bad_schemas[] = {
        "not json",
        "{\"name\":\"evil\"",
        "{\"name\":\"evil\"}]",
        "{\"name\":\"evil\"} trailing",
        "[{\"name\":\"evil\"}]",
        "\"evil\"",
        "42",
        "null",
        "{}",
        "{\"name\":\"other\"}",
        "{\"name\":42}",
    };

    const char *base = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &base) == ASH_OK);

    ash_tool t = EXTRA_TOOL;
    t.name = "evil";
    for (size_t i = 0; i < sizeof bad_schemas / sizeof bad_schemas[0]; i++) {
        t.schema = bad_schemas[i];
        ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_ERR_PARSE);
        ASH_CHECK(strstr(ash_errbuf, "evil") != NULL);
        ASH_CHECK(ash_tool_find("evil") == NULL);
    }

    const char *schema = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &schema) == ASH_OK);
    ASH_CHECK(strstr(schema, "evil") == NULL);
    ASH_CHECK(schema_array_len(a, schema) == schema_array_len(a, base));

    t.schema = "{\"name\":\"evil\",\"input_schema\":{\"type\":\"object\"}}";
    ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_OK);
    ASH_CHECK(ash_tool_find("evil") != NULL);
    ash_tools_unregister_owner(&owner_a);
    ASH_CHECK(ash_tool_find("evil") == NULL);
}

static char g_big[ASH_TOOL_SCHEMA_MAX + 64];

static const char *pad_schema_to(size_t total)
{
    static const char head[] = "{\"name\":\"big\",\"pad\":\"";
    static const char tail[] = "\",\"input_schema\":{\"type\":\"object\"}}";
    size_t fixed = sizeof head - 1 + sizeof tail - 1;

    ASH_CHECK(total >= fixed && total <= sizeof g_big - 1);
    memcpy(g_big, head, sizeof head - 1);
    memset(g_big + sizeof head - 1, 'x', total - fixed);
    memcpy(g_big + total - (sizeof tail - 1), tail, sizeof tail - 1);
    g_big[total] = 0;
    return g_big;
}

static void test_schema_length_cap(ash_arena *a)
{
    ash_tool t = EXTRA_TOOL;
    t.name = "big";

    t.schema = pad_schema_to(ASH_TOOL_SCHEMA_MAX);
    ASH_CHECK(strlen(t.schema) == ASH_TOOL_SCHEMA_MAX);
    ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_OK);
    ASH_CHECK(ash_tool_find("big") != NULL);
    ash_tools_unregister_owner(&owner_a);

    t.schema = pad_schema_to(ASH_TOOL_SCHEMA_MAX + 1);
    ASH_CHECK(strlen(t.schema) == ASH_TOOL_SCHEMA_MAX + 1u);
    ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_ERR_RANGE);
    ASH_CHECK(strstr(ash_errbuf, "big") != NULL);
    ASH_CHECK(strstr(ash_errbuf, "8192") != NULL);
    ASH_CHECK(ash_tool_find("big") == NULL);

    const char *schema = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &schema) == ASH_OK);
    ASH_CHECK(strstr(schema, "\"big\"") == NULL);
}

enum { WORST_CHAIN_MAX = 127 };

static size_t build_worst_case(void)
{
    size_t n = 0;
    g_big[n++] = '[';
    for (;;) {
        size_t room = ASH_TOOL_SCHEMA_MAX - n - 1;
        if (n > 1) {
            if (room == 0)
                break;
            room--;
        }
        if (room < 3)
            break;
        size_t depth = (room - 1) / 2;
        if (depth > WORST_CHAIN_MAX)
            depth = WORST_CHAIN_MAX;
        if (n > 1)
            g_big[n++] = ',';
        for (size_t i = 0; i < depth; i++)
            g_big[n++] = '[';
        g_big[n++] = '0';
        for (size_t i = 0; i < depth; i++)
            g_big[n++] = ']';
    }
    g_big[n++] = ']';
    g_big[n] = 0;
    return n;
}

static void test_worst_case_amplification(ash_arena *a)
{
    size_t n = build_worst_case();
    ASH_CHECK(n <= ASH_TOOL_SCHEMA_MAX);
    ASH_CHECK(n + 4 > ASH_TOOL_SCHEMA_MAX);
    ASH_CHECK(strlen(g_big) == n);

    ash_tool t = EXTRA_TOOL;
    t.name = "big";
    t.schema = g_big;
    ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_ERR_PARSE);
    ASH_CHECK(ash_tool_find("big") == NULL);

    const char *schema = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &schema) == ASH_OK);
    ASH_CHECK(strstr(schema, "\"big\"") == NULL);

    ash_arena m;
    ASH_CHECK(ash_arena_create(&m, "amp", (size_t)ASH_TOOL_SCHEMA_ARENA)
              == ASH_OK);
    ash_json v;
    ASH_CHECK(ash_json_parse(&m, g_big, n, &v) == ASH_OK);
    ASH_CHECK(v.type == ASH_JSON_ARRAY && v.u.arr.n > 0);

    size_t used = ash_arena_mark_get(&m).used;
    ASH_CHECK(m.high_water == m.chunk_size);
    ASH_CHECK(used < ASH_TOOL_SCHEMA_ARENA);
    ASH_CHECK(used < n * 256);
    ASH_CHECK(used > n * 32);
    printf("worst case: %zu byte schema, %zu bytes of arena, %.1fx\n",
           n, used, (double)used / (double)n);
    ash_arena_destroy(&m);
}

static void test_duplicate_names(void)
{
    static const char *const builtins[] = {
        "bash", "read", "write", "edit", "grep", "ls"
    };
    static char schemas[6][64];

    for (size_t i = 0; i < sizeof builtins / sizeof builtins[0]; i++) {
        const ash_tool *before = ash_tool_find(builtins[i]);
        ASH_CHECK(before != NULL);

        snprintf(schemas[i], sizeof schemas[i],
                 "{\"name\":\"%s\",\"input_schema\":{\"type\":\"object\"}}",
                 builtins[i]);
        ash_tool t = EXTRA_TOOL;
        t.name = builtins[i];
        t.schema = schemas[i];

        ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_ERR_STATE);
        ASH_CHECK(strstr(ash_errbuf, builtins[i]) != NULL);
        ASH_CHECK(ash_tool_find(builtins[i]) == before);
        ASH_CHECK(before->run != extra_run);
    }

    ASH_CHECK(ash_tools_register(&TOOL_A, &owner_a) == ASH_OK);
    ASH_CHECK(ash_tools_register(&TOOL_A, &owner_a) == ASH_ERR_STATE);
    ASH_CHECK(ash_tools_register(&TOOL_A, &owner_b) == ASH_ERR_STATE);

    ash_tool alias = TOOL_B;
    alias.name = TOOL_A.name;
    alias.schema = TOOL_A.schema;
    ASH_CHECK(ash_tools_register(&alias, &owner_b) == ASH_ERR_STATE);

    ash_tools_unregister_owner(&owner_b);
    ASH_CHECK(ash_tool_find(TOOL_A.name) != NULL);
    ash_tools_unregister_owner(&owner_a);
    ASH_CHECK(ash_tool_find(TOOL_A.name) == NULL);
}

static void test_pointer_stability(ash_arena *a)
{
    ASH_CHECK(ash_tools_register(&TOOL_B, &owner_b) == ASH_OK);
    ASH_CHECK(ash_tools_register(&TOOL_A, &owner_a) == ASH_OK);

    const ash_tool *pa = ash_tool_find(TOOL_A.name);
    const ash_tool *pb = ash_tool_find(TOOL_B.name);
    ASH_CHECK(pa != NULL && pb != NULL && pa != pb);

    ash_tools_unregister_owner(&owner_b);

    ASH_CHECK(ash_tool_find(TOOL_A.name) == pa);
    ASH_CHECK_STREQ(pa->name, TOOL_A.name);
    ASH_CHECK(pa->run == extra_run);

    ash_tool_result res;
    const char *in = "{\"who\":\"stable\"}";
    ASH_CHECK(ash_tool_dispatch(pa, a, in, strlen(in), &res) == ASH_OK);
    ASH_CHECK_STREQ(res.content, "hello stable");

    ASH_CHECK(ash_tools_register(&TOOL_B, &owner_b) == ASH_OK);
    ASH_CHECK(ash_tool_find(TOOL_A.name) == pa);
    ASH_CHECK(ash_tool_find(TOOL_B.name) == pb);

    ash_tools_unregister_owner(&owner_a);
    ash_tools_unregister_owner(&owner_b);
}

static void test_dead_slots_are_reused(void)
{
    static char names[ASH_TOOLS_EXTRA_CAP][16];
    static char schemas[ASH_TOOLS_EXTRA_CAP][64];

    ash_tool t = EXTRA_TOOL;
    for (int i = 0; i < ASH_TOOLS_EXTRA_CAP; i++) {
        snprintf(names[i], sizeof names[i], "slot%d", i);
        snprintf(schemas[i], sizeof schemas[i],
                 "{\"name\":\"slot%d\",\"input_schema\":{\"type\":\"object\"}}",
                 i);
        t.name = names[i];
        t.schema = schemas[i];
        ASH_CHECK(ash_tools_register(&t, i % 2 ? &owner_b : &owner_a)
                  == ASH_OK);
    }

    t.name = TOOL_A.name;
    t.schema = TOOL_A.schema;
    ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_ERR_NOSPACE);

    ash_tools_unregister_owner(&owner_b);

    for (int i = 1; i < ASH_TOOLS_EXTRA_CAP; i += 2) {
        ASH_CHECK(ash_tool_find(names[i]) == NULL);
        ASH_CHECK(ash_tool_find(names[i - 1]) != NULL);
    }

    for (int i = 1; i < ASH_TOOLS_EXTRA_CAP; i += 2) {
        t.name = names[i];
        t.schema = schemas[i];
        ASH_CHECK(ash_tools_register(&t, &owner_b) == ASH_OK);
    }

    t.name = TOOL_A.name;
    t.schema = TOOL_A.schema;
    ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_ERR_NOSPACE);

    ash_tools_unregister_owner(&owner_a);
    ash_tools_unregister_owner(&owner_b);
    for (int i = 0; i < ASH_TOOLS_EXTRA_CAP; i++)
        ASH_CHECK(ash_tool_find(names[i]) == NULL);

    for (int i = 0; i < ASH_TOOLS_EXTRA_CAP; i++) {
        t.name = names[i];
        t.schema = schemas[i];
        ASH_CHECK(ash_tools_register(&t, &owner_a) == ASH_OK);
    }
    ash_tools_unregister_owner(&owner_a);
}

static void test_schema_build_rejects(ash_arena *a)
{
    const char *schema = NULL;
    ASH_CHECK(ash_tools_schema_build(NULL, &schema) == ASH_ERR_RANGE);
    ASH_CHECK(ash_tools_schema_build(a, NULL) == ASH_ERR_RANGE);
    ASH_CHECK(ash_tools_schema_build(NULL, NULL) == ASH_ERR_RANGE);
}

static void test_capacity(ash_arena *a)
{
    static char names[ASH_TOOLS_EXTRA_CAP + 1][16];
    static char schemas[ASH_TOOLS_EXTRA_CAP + 1][64];

    const char *base = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &base) == ASH_OK);
    size_t builtins = schema_array_len(a, base);

    ash_tool t = EXTRA_TOOL;
    for (int i = 0; i <= ASH_TOOLS_EXTRA_CAP; i++) {
        snprintf(names[i], sizeof names[i], "cap%d", i);
        snprintf(schemas[i], sizeof schemas[i],
                 "{\"name\":\"cap%d\",\"input_schema\":{\"type\":\"object\"}}",
                 i);
        t.name = names[i];
        t.schema = schemas[i];
        ash_status st = ash_tools_register(&t, &owner_a);
        if (i < ASH_TOOLS_EXTRA_CAP)
            ASH_CHECK(st == ASH_OK);
        else
            ASH_CHECK(st == ASH_ERR_NOSPACE);
    }

    ASH_CHECK(ash_tool_find(names[0]) != NULL);
    ASH_CHECK(ash_tool_find(names[ASH_TOOLS_EXTRA_CAP - 1]) != NULL);
    ASH_CHECK(ash_tool_find(names[ASH_TOOLS_EXTRA_CAP]) == NULL);

    const char *full = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &full) == ASH_OK);
    ASH_CHECK(schema_array_len(a, full) == builtins + ASH_TOOLS_EXTRA_CAP);

    ash_tools_unregister_owner(&owner_a);
    ASH_CHECK(ash_tool_find(names[0]) == NULL);

    const char *back = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &back) == ASH_OK);
    ASH_CHECK(schema_array_len(a, back) == builtins);
}

static void test_owner_scoped_unregister(ash_arena *a)
{
    ASH_CHECK(ash_tools_register(&TOOL_A, &owner_a) == ASH_OK);
    ASH_CHECK(ash_tools_register(&TOOL_B, &owner_b) == ASH_OK);

    const char *both = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &both) == ASH_OK);
    ASH_CHECK(strstr(both, "owner-a-tool") != NULL);
    ASH_CHECK(strstr(both, "owner-b-tool") != NULL);

    ash_tools_unregister_owner(&owner_a);
    ASH_CHECK(ash_tool_find("owner-a-tool") == NULL);
    ASH_CHECK(ash_tool_find("owner-b-tool") != NULL);

    const char *only_b = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &only_b) == ASH_OK);
    ASH_CHECK(strstr(only_b, "owner-a-tool") == NULL);
    ASH_CHECK(strstr(only_b, "owner-b-tool") != NULL);
    ASH_CHECK(schema_array_len(a, only_b) + 1 == schema_array_len(a, both));

    ash_tool_result res;
    const char *in = "{\"who\":\"b\"}";
    const ash_tool *tb = ash_tool_find("owner-b-tool");
    ASH_CHECK(ash_tool_dispatch(tb, a, in, strlen(in), &res) == ASH_OK);
    ASH_CHECK_STREQ(res.content, "hello b");

    ash_tools_unregister_owner(&owner_b);
    ASH_CHECK(ash_tool_find("owner-b-tool") == NULL);

    const char *none = NULL;
    ASH_CHECK(ash_tools_schema_build(a, &none) == ASH_OK);
    ASH_CHECK(strstr(none, "owner-b-tool") == NULL);
    ASH_CHECK(schema_array_len(a, none) + 2 == schema_array_len(a, both));

    ash_tools_unregister_owner(&owner_a);
    ash_tools_unregister_owner(NULL);
    ASH_CHECK(ash_tool_find("bash") != NULL);
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "reg", 1u << 16) == ASH_OK);
    test_schema(&a);
    test_find();
    test_bash_command(&a);
    test_register(&a);
    test_dispatch(&a);
    test_register_rejects();
    test_register_rejects_shell(&a);
    test_duplicate_keys(&a);
    test_schema_rejects(&a);
    test_schema_length_cap(&a);
    test_worst_case_amplification(&a);
    test_duplicate_names();
    test_pointer_stability(&a);
    test_dead_slots_are_reused();
    test_schema_build_rejects(&a);
    test_capacity(&a);
    test_owner_scoped_unregister(&a);
    ash_arena_destroy(&a);
    return ash_test_done();
}
