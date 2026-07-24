#include <errno.h>
#include <poll.h>
#include <string.h>
#include <unistd.h>

#include "ash/core/proc.h"
#include "ash_test.h"

static size_t drain(ash_proc *p, char *buf, size_t cap)
{
    size_t len = 0;
    while (len < cap) {
        ssize_t r = read(ash_proc_out_fd(p), buf + len, cap - len);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        if (r == 0)
            break;
        len += (size_t)r;
    }
    return len;
}

int main(void)
{
    char buf[512];

    {
        const char *argv[] = { "echo", "hello", NULL };
        ash_proc p;
        ASH_CHECK(ash_proc_spawn(&p, argv) == ASH_OK);
        size_t n = drain(&p, buf, sizeof buf);
        int code = -1;
        ASH_CHECK(ash_proc_wait(&p, &code) == ASH_OK && code == 0);
        ASH_CHECK(n == 6 && memcmp(buf, "hello\n", 6) == 0);
        ash_proc_close(&p);
    }

    {
        const char *argv[] = { "false", NULL };
        ash_proc p;
        ASH_CHECK(ash_proc_spawn(&p, argv) == ASH_OK);
        (void)drain(&p, buf, sizeof buf);
        int code = -1;
        ASH_CHECK(ash_proc_wait(&p, &code) == ASH_OK && code == 1);
        ash_proc_close(&p);
    }

    {
        const char *argv[] = { "/nonexistent/xyzzy", NULL };
        ash_proc p;
        ASH_CHECK(ash_proc_spawn(&p, argv) == ASH_OK);
        (void)drain(&p, buf, sizeof buf);
        int code = -1;
        ASH_CHECK(ash_proc_wait(&p, &code) == ASH_OK && code == 127);
        ash_proc_close(&p);
    }

    {
        const char *argv[] = { "sh", "-c", "echo out; echo err 1>&2", NULL };
        ash_proc p;
        ASH_CHECK(ash_proc_spawn(&p, argv) == ASH_OK);
        size_t n = drain(&p, buf, sizeof buf - 1);
        struct pollfd pf = { .fd = ash_proc_pidfd(&p), .events = POLLIN, .revents = 0 };
        (void)poll(&pf, 1, 2000);
        ASH_CHECK(pf.revents & POLLIN);
        int code = -1;
        ASH_CHECK(ash_proc_wait(&p, &code) == ASH_OK && code == 0);
        buf[n] = 0;
        ASH_CHECK(strstr(buf, "out") != NULL && strstr(buf, "err") != NULL);
        ash_proc_close(&p);
    }

    return ash_test_done();
}
