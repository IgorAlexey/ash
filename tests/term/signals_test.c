#include <fcntl.h>
#include <pty.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include "ash/term/screen.h"
#include "ash/term/signals.h"
#include "ash_test.h"

static void child_run(int fd)
{
    if (ash_screen_init(fd) != ASH_OK)
        _exit(2);
    if (ash_signals_init() != ASH_OK)
        _exit(3);

    sigset_t block, prev;
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTERM);
    sigaddset(&block, SIGHUP);
    (void)sigprocmask(SIG_BLOCK, &block, &prev);

    (void)write(fd, "READY", 5);
    while (!ash_signal_pending())
        (void)sigsuspend(&prev);

    (void)sigprocmask(SIG_SETMASK, &prev, NULL);
    ash_screen_shutdown();
    _exit(0);
}

static int wait_ready(int master)
{
    char buf[512];
    size_t total = 0;
    while (total < sizeof buf - 1) {
        ssize_t r = read(master, buf + total, sizeof buf - 1 - total);
        if (r <= 0)
            return 0;
        total += (size_t)r;
        if (memmem(buf, total, "READY", 5))
            return 1;
    }
    return 0;
}

static int drain_nb(int fd, char *buf, size_t cap)
{
    (void)fcntl(fd, F_SETFL, O_NONBLOCK);
    size_t total = 0;
    while (total < cap - 1) {
        ssize_t r = read(fd, buf + total, cap - 1 - total);
        if (r <= 0)
            break;
        total += (size_t)r;
    }
    buf[total] = '\0';
    return (int)total;
}

static int termios_eq(const struct termios *a, const struct termios *b)
{
    if (a->c_iflag != b->c_iflag || a->c_oflag != b->c_oflag ||
        a->c_cflag != b->c_cflag || a->c_lflag != b->c_lflag)
        return 0;
    for (size_t i = 0; i < NCCS; i++)
        if (a->c_cc[i] != b->c_cc[i])
            return 0;
    return 1;
}

static int one_signal(int sig)
{
    int master, slave;
    if (openpty(&master, &slave, NULL, NULL, NULL) != 0)
        return 0;

    struct termios pre;
    if (tcgetattr(slave, &pre) != 0)
        return 0;

    pid_t pid = fork();
    if (pid == 0) {
        close(master);
        child_run(slave);
        _exit(9);
    }

    if (!wait_ready(master)) {
        kill(pid, SIGKILL);
        (void)waitpid(pid, NULL, 0);
        return 0;
    }

    struct termios raw;
    (void)tcgetattr(slave, &raw);
    int went_raw = !termios_eq(&pre, &raw);

    (void)kill(pid, sig);
    int st = 0;
    (void)waitpid(pid, &st, 0);

    struct termios post;
    (void)tcgetattr(slave, &post);
    int restored = termios_eq(&pre, &post);

    char out[4096];
    int n = drain_nb(master, out, sizeof out);
    int blob = memmem(out, (size_t)n, "\033[?2004l", 8) != NULL;

    close(master);
    close(slave);
    return went_raw && restored && blob;
}

int main(void)
{
    int sigs[] = { SIGINT, SIGTERM, SIGHUP, SIGSEGV, SIGABRT };
    for (size_t i = 0; i < sizeof sigs / sizeof sigs[0]; i++)
        ASH_CHECK(one_signal(sigs[i]));
    return ash_test_done();
}
