#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
#include "ash/tools/tools.h"
#include "ash_test.h"

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
    ASH_CHECK(ash_bash_command(a, "not json", 8, &cmd) != ASH_OK && cmd == NULL);

    cmd = (const char *)1;
    ASH_CHECK(ash_bash_command(a, NULL, 0, &cmd) != ASH_OK && cmd == NULL);
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "reg", 1u << 16) == ASH_OK);
    test_schema(&a);
    test_find();
    test_bash_command(&a);
    ash_arena_destroy(&a);
    return ash_test_done();
}
