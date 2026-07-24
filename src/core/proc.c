#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ash/core/proc.h"
#include "ash/base/poison.h"

#if !defined(SYS_pidfd_open) || !defined(SYS_pidfd_send_signal)
#error "pidfd syscalls are undefined; ash needs Linux 5.3+ (glibc) headers"
#endif

static int open_pidfd(pid_t pid)
{
    return (int)syscall(SYS_pidfd_open, pid, 0u);
}

ash_status ash_proc_spawn(ash_proc *p, const char *const argv[])
{
    if (p == NULL || argv == NULL || argv[0] == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_proc_spawn: bad arguments");

    p->pid = -1;
    p->pidfd = -1;
    p->out = -1;

    int fds[2];
    if (pipe2(fds, O_CLOEXEC) != 0)
        return ash_fail(ASH_ERR_OS, "pipe2: %s", strerror(errno));

    pid_t pid = fork();
    if (pid < 0) {
        int e = errno;
        close(fds[0]);
        close(fds[1]);
        return ash_fail(ASH_ERR_OS, "fork: %s", strerror(e));
    }

    if (pid == 0) {
        int nul = open("/dev/null", O_RDONLY);
        if (nul < 0)
            _exit(127);
        if (dup2(nul, STDIN_FILENO) < 0)
            _exit(127);
        if (nul > STDIN_FILENO)
            close(nul);
        if (dup2(fds[1], STDOUT_FILENO) < 0 || dup2(fds[1], STDERR_FILENO) < 0)
            _exit(127);
        (void)fcntl(STDOUT_FILENO, F_SETFD, 0);
        (void)fcntl(STDERR_FILENO, F_SETFD, 0);
        execvp(argv[0], (char *const *)argv);
        _exit(127);
    }

    close(fds[1]);
    int pfd = open_pidfd(pid);
    if (pfd < 0) {
        int e = errno;
        close(fds[0]);
        (void)waitpid(pid, NULL, 0);
        return ash_fail(ASH_ERR_OS, "pidfd_open: %s", strerror(e));
    }

    p->pid = pid;
    p->pidfd = pfd;
    p->out = fds[0];
    return ASH_OK;
}

int ash_proc_out_fd(const ash_proc *p)
{
    return p->out;
}

int ash_proc_pidfd(const ash_proc *p)
{
    return p->pidfd;
}

ash_status ash_proc_wait(ash_proc *p, int *exit_code)
{
    if (p == NULL || p->pidfd < 0 || p->pid < 0)
        return ash_fail(ASH_ERR_STATE, "ash_proc_wait: no live process");

    siginfo_t si;
    memset(&si, 0, sizeof si);
    int r;
    do {
        r = waitid(P_PIDFD, (id_t)p->pidfd, &si, WEXITED);
    } while (r != 0 && errno == EINTR);
    if (r != 0)
        return ash_fail(ASH_ERR_OS, "waitid: %s", strerror(errno));

    if (exit_code != NULL)
        *exit_code = si.si_code == CLD_EXITED ? si.si_status : 128 + si.si_status;
    p->pid = -1;
    return ASH_OK;
}

void ash_proc_close(ash_proc *p)
{
    if (p->out >= 0) {
        close(p->out);
        p->out = -1;
    }
    if (p->pidfd >= 0) {
        if (p->pid > 0) {
            (void)syscall(SYS_pidfd_send_signal, p->pidfd, SIGKILL, NULL, 0u);
            siginfo_t si;
            (void)waitid(P_PIDFD, (id_t)p->pidfd, &si, WEXITED);
        }
        close(p->pidfd);
        p->pidfd = -1;
    }
    p->pid = -1;
}
