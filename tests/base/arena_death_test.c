#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ash/base/arena.h"
#include "ash_test.h"

int main(void)
{
    int fds[2];
    ASH_CHECK(pipe(fds) == 0);

    pid_t pid = fork();
    if (pid == 0) {
        dup2(fds[1], 2);
        close(fds[0]);
        close(fds[1]);
        ash_arena a;
        if (ash_arena_create(&a, "death", 1024) != ASH_OK)
            _exit(2);
        void *p = ash_array(&a, char, SIZE_MAX);
        (void)p;
        _exit(0);
    }

    close(fds[1]);
    char buf[512];
    size_t total = 0;
    while (total < sizeof buf - 1) {
        ssize_t n = read(fds[0], buf + total, sizeof buf - 1 - total);
        if (n <= 0)
            break;
        total += (size_t)n;
    }
    buf[total] = '\0';
    close(fds[0]);

    int st = 0;
    ASH_CHECK(waitpid(pid, &st, 0) == pid);
    ASH_CHECK(WIFSIGNALED(st));
    ASH_CHECK(WTERMSIG(st) == SIGABRT);
    ASH_CHECK(strstr(buf, "overflows") != NULL);

    return ash_test_done();
}
