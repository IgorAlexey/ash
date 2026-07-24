#include <errno.h>
#include <stdio.h>
#include <termios.h>
#include <unistd.h>

#include "ash/term/screen.h"
#include "ash/base/poison.h"

static const char ENABLE[]  = "\033[?2004h\033[?1002h\033[?1006h\033[>1u";
static const char DISABLE[] = "\033[?2026l\033[<u\033[?1006l\033[?1002l\033[?2004l";
static const char OSC52_PRE[] = "\033]52;c;";
static const char OSC52_END[] = "\a";
static const char SYNC_ON[]  = "\033[?2026h";
static const char SYNC_OFF[] = "\033[?2026l";

static int            g_fd = -1;
static struct termios g_saved;
static int            g_have_saved;

static void emit(const char *s, size_t n)
{
    if (g_fd < 0)
        return;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(g_fd, s + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        off += (size_t)w;
    }
}

ash_status ash_screen_init(int fd)
{
    if (tcgetattr(fd, &g_saved) != 0)
        return ash_fail(ASH_ERR_OS, "tcgetattr failed");
    g_have_saved = 1;
    g_fd = fd;

    struct termios raw = g_saved;
    raw.c_iflag &= (tcflag_t)~(IXON | ICRNL | INLCR | IGNCR | BRKINT | INPCK | ISTRIP);
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO | ISIG | IEXTEN);
    raw.c_cflag = (raw.c_cflag & (tcflag_t)~CSIZE) | (tcflag_t)CS8;
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(fd, TCSAFLUSH, &raw) != 0)
        return ash_fail(ASH_ERR_OS, "tcsetattr failed");

    emit(ENABLE, sizeof ENABLE - 1);
    return ASH_OK;
}

void ash_screen_shutdown(void)
{
    if (g_fd < 0)
        return;
    emit(DISABLE, sizeof DISABLE - 1);
    if (g_have_saved)
        (void)tcsetattr(g_fd, TCSAFLUSH, &g_saved);
    g_fd = -1;
}

void ash_screen_emergency_restore(void)
{
    if (g_fd < 0)
        return;
    int saved_errno = errno;
    if (g_have_saved)
        (void)tcsetattr(g_fd, TCSANOW, &g_saved);
    size_t off = 0;
    size_t n = sizeof DISABLE - 1;
    while (off < n) {
        ssize_t w = write(g_fd, DISABLE + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            break;
        }
        off += (size_t)w;
    }
    errno = saved_errno;
}

int ash_screen_fd(void)
{
    return g_fd;
}

void ash_screen_frame_begin(void)
{
    emit(SYNC_ON, sizeof SYNC_ON - 1);
}

void ash_screen_frame_end(void)
{
    emit(SYNC_OFF, sizeof SYNC_OFF - 1);
}

void ash_screen_write(const void *p, size_t n)
{
    emit((const char *)p, n);
}

void ash_screen_clipboard(const void *b64, size_t n)
{
    emit(OSC52_PRE, sizeof OSC52_PRE - 1);
    emit((const char *)b64, n);
    emit(OSC52_END, sizeof OSC52_END - 1);
}

void ash_screen_finish(int row)
{
    if (g_fd < 0)
        return;
    if (row < 1)
        row = 1;
    char seq[40];
    int n = snprintf(seq, sizeof seq, "\033[m\033[%d;1H\033[J\033[0 q\033[?25h",
                     row);
    if (n > 0 && (size_t)n < sizeof seq)
        emit(seq, (size_t)n);
}
