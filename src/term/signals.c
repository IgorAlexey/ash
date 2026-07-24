#include <signal.h>
#include <string.h>

#include "ash/term/signals.h"
#include "ash/term/screen.h"
#include "ash/base/poison.h"

static volatile sig_atomic_t g_pending;

static void graceful_handler(int sig)
{
    (void)sig;
    g_pending = 1;
}

static void fatal_handler(int sig)
{
    ash_screen_emergency_restore();
    raise(sig);
}

static ash_status install(int sig, void (*fn)(int), int flags)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = fn;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = flags;
    if (sigaction(sig, &sa, NULL) != 0)
        return ash_fail(ASH_ERR_OS, "sigaction %d failed", sig);
    return ASH_OK;
}

ash_status ash_signals_init(void)
{
    ASH_TRY(install(SIGINT, graceful_handler, 0));
    ASH_TRY(install(SIGTERM, graceful_handler, 0));
    ASH_TRY(install(SIGHUP, graceful_handler, 0));
    ASH_TRY(install(SIGSEGV, fatal_handler, (int)(SA_RESETHAND | SA_NODEFER)));
    ASH_TRY(install(SIGABRT, fatal_handler, (int)(SA_RESETHAND | SA_NODEFER)));
    return ASH_OK;
}

int ash_signal_pending(void)
{
    return g_pending;
}

void ash_signal_clear(void)
{
    g_pending = 0;
}
