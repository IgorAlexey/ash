#include <fcntl.h>
#include <pty.h>
#include <string.h>
#include <unistd.h>

#include "ash/term/screen.h"
#include "ash_test.h"

static size_t drain(int fd, char *buf, size_t cap)
{
    size_t total = 0;
    while (total < cap) {
        ssize_t r = read(fd, buf + total, cap - total);
        if (r <= 0)
            break;
        total += (size_t)r;
    }
    return total;
}

static int has(const char *hay, size_t n, const char *needle)
{
    return memmem(hay, n, needle, strlen(needle)) != NULL;
}

static void test_clipboard(void)
{
    int master, slave;
    ASH_CHECK(openpty(&master, &slave, NULL, NULL, NULL) == 0);
    ASH_CHECK(fcntl(master, F_SETFL, O_NONBLOCK) == 0);

    ASH_CHECK(ash_screen_init(slave) == ASH_OK);
    const char b64[] = "aGVsbG8gd29ybGQ=";
    ash_screen_clipboard(b64, sizeof b64 - 1);
    ash_screen_shutdown();

    char buf[8192];
    size_t n = drain(master, buf, sizeof buf);
    const char want[] = "\033]52;c;aGVsbG8gd29ybGQ=\007";
    ASH_CHECK(memmem(buf, n, want, sizeof want - 1) != NULL);

    close(master);
    close(slave);
}

int main(void)
{
    int master, slave;
    ASH_CHECK(openpty(&master, &slave, NULL, NULL, NULL) == 0);
    ASH_CHECK(fcntl(master, F_SETFL, O_NONBLOCK) == 0);

    ASH_CHECK(ash_screen_init(slave) == ASH_OK);
    ASH_CHECK(ash_screen_fd() == slave);
    ash_screen_frame_begin();
    ash_screen_write("hi", 2);
    ash_screen_frame_end();
    ash_screen_shutdown();

    char buf[8192];
    size_t n = drain(master, buf, sizeof buf);

    ASH_CHECK(!has(buf, n, "\033[?1049h"));
    ASH_CHECK(!has(buf, n, "\033[?1049l"));
    ASH_CHECK(!has(buf, n, "\033[?47h"));
    ASH_CHECK(!has(buf, n, "\033[?1047h"));
    ASH_CHECK(!has(buf, n, "\033[?1048h"));

    ASH_CHECK(has(buf, n, "\033[?2004h"));
    ASH_CHECK(has(buf, n, "\033[>1u"));

    const char *open = memmem(buf, n, "\033[?2026h", 8);
    const char *payload = open ? memmem(open + 8, (size_t)(buf + n - open - 8), "hi", 2)
                               : NULL;
    const char *disable = memmem(buf, n, "\033[<u", 4);

    const char *frame_close = NULL;
    if (payload && disable && disable - 8 > payload) {
        size_t span = (size_t)((disable - 8) - payload);
        frame_close = memmem(payload, span, "\033[?2026l", 8);
    }

    ASH_CHECK(open != NULL);
    ASH_CHECK(payload != NULL);
    ASH_CHECK(disable != NULL);
    ASH_CHECK(frame_close != NULL);
    ASH_CHECK(open < payload);
    ASH_CHECK(payload < frame_close);
    ASH_CHECK(frame_close + 8 <= disable - 8);

    close(master);
    close(slave);

    test_clipboard();
    return ash_test_done();
}
