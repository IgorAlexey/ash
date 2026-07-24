#include <string.h>

#include "ash/app/bang.h"
#include "ash_test.h"

static int split(const char *line, const char **cmd, size_t *len)
{
    return ash_bang_split(line, strlen(line), cmd, len);
}

int main(void)
{
    const char *cmd;
    size_t len;

    ASH_CHECK(split("!ls -la", &cmd, &len) == 1);
    ASH_CHECK(len == 6);
    ASH_CHECK(memcmp(cmd, "ls -la", 6) == 0);

    ASH_CHECK(split("!  ls -la  ", &cmd, &len) == 1);
    ASH_CHECK(len == 6);
    ASH_CHECK(memcmp(cmd, "ls -la", 6) == 0);

    ASH_CHECK(split("   \t!echo hi", &cmd, &len) == 1);
    ASH_CHECK(len == 7);
    ASH_CHECK(memcmp(cmd, "echo hi", 7) == 0);

    ASH_CHECK(split("!", &cmd, &len) == 1);
    ASH_CHECK(len == 0);

    ASH_CHECK(split("!   ", &cmd, &len) == 1);
    ASH_CHECK(len == 0);

    ASH_CHECK(split("!echo hi\r\n", &cmd, &len) == 1);
    ASH_CHECK(len == 7);
    ASH_CHECK(memcmp(cmd, "echo hi", 7) == 0);

    ASH_CHECK(split("ls -la", &cmd, &len) == 0);
    ASH_CHECK(split("", &cmd, &len) == 0);
    ASH_CHECK(split("   ", &cmd, &len) == 0);
    ASH_CHECK(split("/help", &cmd, &len) == 0);
    ASH_CHECK(split("hello ! world", &cmd, &len) == 0);

    return ash_test_done();
}
