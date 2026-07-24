#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
#include "ash/core/config.h"
#include "ash/ext/ext.h"
#include "ash/tools/tools.h"
#include "ash_test.h"

static ash_status eval(ash_ext *e, const char *src)
{
    return ash_ext_eval(e, src, strlen(src), "=test");
}

static void test_config_read(void)
{
    ash_config cfg;
    memset(&cfg, 0, sizeof cfg);
    cfg.provider.value = "anthropic";
    cfg.model.value = "claude-opus";
    cfg.thinking = ASH_THINK_HIGH;

    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, &cfg) == ASH_OK);

    ASH_CHECK(eval(e, "assert(ash.get_config('provider') == 'anthropic')") == ASH_OK);
    ASH_CHECK(eval(e, "assert(ash.get_config('model') == 'claude-opus')") == ASH_OK);
    ASH_CHECK(eval(e, "assert(ash.get_config('theme') == nil)") == ASH_OK);
    ASH_CHECK(eval(e, "assert(ash.get_config('unknown') == nil)") == ASH_OK);
    ASH_CHECK(eval(e, "assert(type(ash.get_config('thinking')) == 'string')") == ASH_OK);

    ash_ext_destroy(e);
}

static void test_keybindings(void)
{
    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, NULL) == ASH_OK);

    ASH_CHECK(eval(e, "fired = false\n"
                      "ash.bind('save', function() fired = true end)\n"
                      "ash.bind('quit', function() end)") == ASH_OK);

    ASH_CHECK(ash_ext_keybinding_count(e) == 2);
    ASH_CHECK_STREQ(ash_ext_keybinding_name(e, 0), "save");
    ASH_CHECK_STREQ(ash_ext_keybinding_name(e, 1), "quit");

    ASH_CHECK(eval(e, "assert(fired == false)") == ASH_OK);
    ASH_CHECK(ash_ext_keybinding_invoke(e, "save") == ASH_OK);
    ASH_CHECK(eval(e, "assert(fired == true)") == ASH_OK);

    ASH_CHECK(ash_ext_keybinding_invoke(e, "missing") == ASH_ERR_NOTFOUND);

    ash_ext_destroy(e);
}

static void test_tool_roundtrip(ash_arena *a)
{
    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, NULL) == ASH_OK);

    const char *reg =
        "ash.register_tool{\n"
        "  name = 'greet',\n"
        "  description = 'greet someone by name',\n"
        "  schema = '{\"type\":\"object\",\"properties\":"
        "{\"who\":{\"type\":\"string\"}},\"required\":[\"who\"]}',\n"
        "  callback = function(args) return 'hello, ' .. args.who end\n"
        "}";
    ASH_CHECK(eval(e, reg) == ASH_OK);
    ASH_CHECK(ash_ext_tool_count(e) == 1);

    ash_tool *t = ash_ext_tool_at(e, 0);
    ASH_CHECK(t != NULL && strcmp(t->name, "greet") == 0);

    ash_json sv;
    ASH_CHECK(ash_json_parse(a, t->schema, strlen(t->schema), &sv) == ASH_OK);
    const ash_json *nm = ash_json_get(&sv, "name");
    ash_slice ns;
    ASH_CHECK(nm != NULL && ash_json_str(nm, &ns) == ASH_OK
              && ash_slice_eq_cstr(ns, "greet"));
    ASH_CHECK(ash_json_get(&sv, "input_schema") != NULL);

    const char *in = "{\"who\":\"world\"}";
    ash_tool_result res;
    ASH_CHECK(ash_tool_dispatch(t, a, in, strlen(in), &res) == ASH_OK);
    ASH_CHECK(res.is_error == 0);
    ASH_CHECK(res.content != NULL && strcmp(res.content, "hello, world") == 0);

    ash_ext_destroy(e);
}

static void test_tool_error(ash_arena *a)
{
    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, NULL) == ASH_OK);

    ASH_CHECK(eval(e, "ash.register_tool{ name = 'boom',"
                      " callback = function() error('kaboom') end }") == ASH_OK);
    ash_tool *t = ash_ext_tool_at(e, 0);
    ASH_CHECK(t != NULL);

    ash_tool_result res;
    ASH_CHECK(ash_tool_dispatch(t, a, "{}", 2, &res) == ASH_OK);
    ASH_CHECK(res.is_error == 1);
    ASH_CHECK(res.content != NULL && strstr(res.content, "kaboom") != NULL);

    ash_ext_destroy(e);
}

static void test_tool_returns_error_flag(ash_arena *a)
{
    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, NULL) == ASH_OK);

    ASH_CHECK(eval(e, "ash.register_tool{ name = 'flag',"
                      " callback = function() return 'nope', true end }") == ASH_OK);
    ash_tool *t = ash_ext_tool_at(e, 0);

    ash_tool_result res;
    ASH_CHECK(ash_tool_dispatch(t, a, "{}", 2, &res) == ASH_OK);
    ASH_CHECK(res.is_error == 1);
    ASH_CHECK(res.content != NULL && strcmp(res.content, "nope") == 0);

    ash_ext_destroy(e);
}

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ext-test", 1u << 16) == ASH_OK);
    test_config_read();
    test_keybindings();
    test_tool_roundtrip(&a);
    test_tool_error(&a);
    test_tool_returns_error_flag(&a);
    ash_arena_destroy(&a);
    return ash_test_done();
}
