#include <string.h>

#include "ash/base/status.h"
#include "ash/ext/ext.h"
#include "ash_test.h"

static ash_status eval(ash_ext *e, const char *src)
{
    return ash_ext_eval(e, src, strlen(src), "=test");
}

static void test_dangerous_globals_absent(void)
{
    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, NULL) == ASH_OK);

    const char *src =
        "assert(io == nil, 'io leaked')\n"
        "assert(require == nil, 'require leaked')\n"
        "assert(package == nil, 'package leaked')\n"
        "assert(debug == nil, 'debug leaked')\n"
        "assert(load == nil, 'load leaked')\n"
        "assert(dofile == nil, 'dofile leaked')\n"
        "assert(loadfile == nil, 'loadfile leaked')\n"
        "assert(os.execute == nil, 'os.execute leaked')\n"
        "assert(os.remove == nil, 'os.remove leaked')\n"
        "assert(os.rename == nil, 'os.rename leaked')\n"
        "assert(os.exit == nil, 'os.exit leaked')\n"
        "assert(os.tmpname == nil, 'os.tmpname leaked')\n"
        "assert(os.setlocale == nil, 'os.setlocale leaked')\n";
    ASH_CHECK(eval(e, src) == ASH_OK);

    ash_ext_destroy(e);
}

static void test_using_io_errors(void)
{
    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, NULL) == ASH_OK);

    ASH_CHECK(eval(e, "io.write('x')") == ASH_ERR_STATE);
    ASH_CHECK(eval(e, "os.execute('true')") == ASH_ERR_STATE);
    ASH_CHECK(eval(e, "require('os')") == ASH_ERR_STATE);

    ash_ext_destroy(e);
}

static void test_getenv_allowlist(void)
{
    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, NULL) == ASH_OK);

    ASH_CHECK(eval(e, "assert(os.getenv('PATH') == nil)") == ASH_OK);
    ASH_CHECK(eval(e, "assert(os.getenv('LD_PRELOAD') == nil)") == ASH_OK);
    ASH_CHECK(eval(e, "os.getenv('TERM')") == ASH_OK);
    ASH_CHECK(eval(e, "assert(type(os.time()) == 'number')") == ASH_OK);

    ash_ext_destroy(e);
}

static void test_bytecode_rejected(void)
{
    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, NULL, NULL) == ASH_OK);

    const char bc[] = { 0x1b, 'L', 'u', 'a', 0 };
    ASH_CHECK(ash_ext_eval(e, bc, 4, "=bc") == ASH_ERR_PARSE);

    ash_ext_destroy(e);
}

static void test_instruction_budget(void)
{
    ash_ext_limits lim = ash_ext_limits_default();
    lim.instr_budget = 200000;
    lim.hook_count = 200;

    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, &lim, NULL) == ASH_OK);

    ASH_CHECK(eval(e, "while true do end") == ASH_ERR_STATE);
    ASH_CHECK(strstr(ash_errbuf, "instruction budget") != NULL);

    ASH_CHECK(eval(e, "local s = 0 for i = 1, 10 do s = s + i end") == ASH_OK);

    ash_ext_destroy(e);
}

static void test_alloc_budget(void)
{
    ash_ext_limits lim = ash_ext_limits_default();
    lim.mem_budget = 1u << 20;

    ash_ext *e = NULL;
    ASH_CHECK(ash_ext_create(&e, &lim, NULL) == ASH_OK);

    ASH_CHECK(eval(e, "local s = string.rep('x', 8 * 1024 * 1024)")
              == ASH_ERR_NOMEM);
    ASH_CHECK(eval(e, "local t = 'small'") == ASH_OK);

    ash_ext_destroy(e);
}

int main(void)
{
    test_dangerous_globals_absent();
    test_using_io_errors();
    test_getenv_allowlist();
    test_bytecode_rejected();
    test_instruction_budget();
    test_alloc_budget();
    return ash_test_done();
}
