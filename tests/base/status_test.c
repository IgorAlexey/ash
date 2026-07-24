#include "ash/base/status.h"
#include "ash_test.h"

int main(void)
{
    ASH_CHECK_STREQ(ash_status_str(ASH_OK), "ok");
    ASH_CHECK_STREQ(ash_status_str(ASH_ERR_NOMEM), "out of memory");
    ASH_CHECK(ash_status_str((ash_status)9999)[0] != '\0');

    for (int i = 0; i < ASH_STATUS_COUNT; i++)
        ASH_CHECK(ash_status_str((ash_status)i)[0] != '\0');

    ash_status st = ash_fail(ASH_ERR_PARSE, "bad token at %d", 42);
    ASH_CHECK(st == ASH_ERR_PARSE);
    ASH_CHECK(strstr(ash_errbuf, "42") != NULL);

    return ash_test_done();
}
