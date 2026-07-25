#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "ash/ai/http.h"
#include "ash/app/loop.h"
#include "ash/core/session.h"
#include "ash/edit/diffview.h"
#include "ash/term/screen.h"
#include "ash_test.h"

static const char HAPPY[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello, \"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"world\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char HAPPY2[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"SECONDTURN\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char ERRBODY[] =
    "event: error\n"
    "data: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\","
    "\"message\":\"Overloaded\"}}\n"
    "\n";

static const char TOOL_USE[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_x\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":"
    "\"{\\\"command\\\": \\\"echo TOOLTEST\\\"}\"}}\n"
    "\n"
    "event: content_block_stop\n"
    "data: {\"type\":\"content_block_stop\",\"index\":0}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char TOOL_USE_B[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_y\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":"
    "\"{\\\"command\\\": \\\"echo ROUND2\\\"}\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char TOOL_PARALLEL[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_p1\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_p2\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":"
    "\"{\\\"command\\\": \\\"echo PARA_AAA\\\"}\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":"
    "\"{\\\"command\\\": \\\"echo PARA_BBB\\\"}\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char TOOL_SURVIVOR[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_s\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":"
    "\"{\\\"command\\\": \\\"sleep 1 & echo SURVIVOR\\\"}\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char TOOL_BIGOUT[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_big\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":"
    "\"{\\\"command\\\": \\\"yes ABCDEFGH | head -c 1100000\\\"}\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char TOOL_FINAL[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"Ran the tool.\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char TOOL_ENV[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_e\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":"
    "\"{\\\"command\\\": \\\"echo scrub=$ANTHROPIC_API_KEY-end\\\"}\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char HOLD_PREFIX[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"partial\"}}\n"
    "\n";

static void nap(long ms)
{
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    (void)nanosleep(&ts, NULL);
}

static int pty_is_raw(int mfd)
{
    struct termios t;
    return tcgetattr(mfd, &t) == 0 && (t.c_lflag & ECHO) == 0;
}

static void wait_until_raw(int mfd)
{
    for (int i = 0; i < 5000 && !pty_is_raw(mfd); i++)
        nap(1);
    ASH_CHECK(pty_is_raw(mfd));
}

static int64_t now_ms(void)
{
    struct timespec ts;
    (void)clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

enum { PTY_WAIT_MS = 20000, PTY_POLL_MS = 50 };

static int pty_read_until(int mfd, char *out, size_t cap, size_t *got,
                          const char *want)
{
    int64_t deadline = now_ms() + PTY_WAIT_MS;
    for (;;) {
        if (want != NULL && strstr(out, want) != NULL)
            return 1;
        int64_t left = deadline - now_ms();
        if (left <= 0)
            return 0;
        struct pollfd pfd = { .fd = mfd, .events = POLLIN, .revents = 0 };
        int r = poll(&pfd, 1, left < PTY_POLL_MS ? (int)left : PTY_POLL_MS);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return 0;
        }
        if (r == 0)
            continue;
        if (*got >= cap - 1)
            return 0;
        ssize_t n = read(mfd, out + *got, cap - 1 - *got);
        if (n <= 0)
            return want == NULL;
        *got += (size_t)n;
        out[*got] = 0;
    }
}

enum { SERVE_WRITE_FAILED = 21, SERVE_NO_SIGPIPE = 22 };

static void ignore_sigpipe(void)
{
    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGPIPE, &sa, NULL) != 0) {
        int err = errno;

        fprintf(stderr, "  sigaction SIGPIPE: %s\n", strerror(err));
        _exit(SERVE_NO_SIGPIPE);
    }
}

static pid_t fork_checked(void)
{
    pid_t pid = fork();
    int err = errno;

    if (pid < 0) {
        ash_test_check(0, "fork failed", __FILE__, __LINE__);
        fprintf(stderr, "  fork: %s\n", strerror(err));
        exit(ash_test_done());
    }
    return pid;
}

static pid_t fork_server(void)
{
    pid_t pid = fork_checked();

    if (pid == 0)
        ignore_sigpipe();
    return pid;
}

static int write_all(int c, const char *p, size_t n)
{
    size_t off = 0;

    while (off < n) {
        ssize_t w = write(c, p + off, n - off);
        int err = w < 0 ? errno : EIO;

        if (w > 0) {
            off += (size_t)w;
            continue;
        }
        if (err == EINTR)
            continue;
        fprintf(stderr, "  write to fd %d stopped at %zu of %zu: %s\n",
                c, off, n, strerror(err));
        return err;
    }
    return 0;
}

static void serve_write(int c, const char *p, size_t n)
{
    int err = write_all(c, p, n);

    if (err == 0)
        return;
    if (err == EPIPE || err == ECONNRESET)
        _exit(0);
    _exit(SERVE_WRITE_FAILED);
}

static void write_str_all(int c, const char *s)
{
    write_all(c, s, strlen(s));
}

static int listen_loopback(int *port)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0)
        return -1;
    int one = 1;
    (void)setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        close(lfd);
        return -1;
    }
    socklen_t al = sizeof addr;
    (void)getsockname(lfd, (struct sockaddr *)&addr, &al);
    *port = ntohs(addr.sin_port);
    if (listen(lfd, 1) != 0) {
        close(lfd);
        return -1;
    }
    return lfd;
}

static void read_request(int c)
{
    char head[4096];
    size_t got = 0;
    char *end = NULL;
    while (end == NULL) {
        if (got == sizeof head)
            return;
        ssize_t r = read(c, head + got, sizeof head - got);
        if (r <= 0)
            return;
        got += (size_t)r;
        end = memmem(head, got, "\r\n\r\n", 4);
    }
    const char *cl = memmem(head, (size_t)(end - head), "Content-Length: ", 16);
    if (cl == NULL)
        return;
    size_t need = strtoul(cl + 16, NULL, 10);
    size_t body = got - (size_t)(end + 4 - head);
    char skip[4096];
    while (body < need) {
        ssize_t r = read(c, skip, sizeof skip);
        if (r <= 0)
            return;
        body += (size_t)r;
    }
}

static void serve_body(int lfd, const char *body)
{
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        return;
    read_request(c);
    char hdr[256];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      strlen(body));
    if (hn > 0) {
        serve_write(c, hdr, (size_t)hn);
        serve_write(c, body, strlen(body));
    }
    close(c);
}

static void serve_body_delay(int lfd, const char *body, long ms)
{
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        return;
    read_request(c);
    nap(ms);
    char hdr[256];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      strlen(body));
    if (hn > 0) {
        serve_write(c, hdr, (size_t)hn);
        serve_write(c, body, strlen(body));
    }
    close(c);
}

static void serve_hold(int lfd)
{
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        return;
    char req[2048];
    (void)read(c, req, sizeof req);
    const char *hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Connection: close\r\n\r\n";
    serve_write(c, hdr, strlen(hdr));
    serve_write(c, HOLD_PREFIX, strlen(HOLD_PREFIX));
    char tmp[64];
    while (read(c, tmp, sizeof tmp) > 0)
        ;
    close(c);
}

static void serve_reset(int lfd)
{
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        return;
    read_request(c);
    const char *hdr =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/event-stream\r\n"
        "Content-Length: 65536\r\n"
        "Connection: close\r\n\r\n";
    serve_write(c, hdr, strlen(hdr));
    serve_write(c, HOLD_PREFIX, strlen(HOLD_PREFIX));
    struct linger lg = { 1, 0 };
    (void)setsockopt(c, SOL_SOCKET, SO_LINGER, &lg, sizeof lg);
    close(c);
}

static void run_loop(const char *url, int in_fd, char *out, size_t cap)
{
    int outfd = memfd_create("loop-out", 0);
    ASH_CHECK(outfd >= 0);
    ash_loop_cfg cfg = {
        .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
    };
    ASH_CHECK(ash_loop_run(&cfg, in_fd, outfd) == ASH_OK);
    ASH_CHECK(lseek(outfd, 0, SEEK_SET) == 0);
    ssize_t rn = read(outfd, out, cap - 1);
    if (rn < 0)
        rn = 0;
    out[rn] = 0;
    close(outfd);
}

static void test_happy(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, HAPPY);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "hello\r", 6);
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "hello") != NULL);
    ASH_CHECK(strstr(out, "Hello, world") != NULL);
    ASH_CHECK(strstr(out, "bye") != NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_error(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, ERRBODY);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "hello\r", 6);
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "error:") != NULL);
    ASH_CHECK(strstr(out, "Overloaded") != NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_cancel(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t spid = fork_server();
    if (spid == 0) {
        serve_hold(lfd);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    pid_t wpid = fork_checked();
    if (wpid == 0) {
        close(inp[0]);
        write_all(inp[1], "hello\r", 6);
        nap(250);
        write_all(inp[1], "\x03", 1);
        nap(150);
        close(inp[1]);
        _exit(0);
    }
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "[canceled]") != NULL);
    (void)waitpid(wpid, NULL, 0);
    (void)waitpid(spid, NULL, 0);
}

static void test_cancel_same_read(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t spid = fork_server();
    if (spid == 0) {
        serve_body_delay(lfd, HAPPY, 400);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_str_all(inp[1], "hello\r\x03");
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "[canceled]") != NULL);
    (void)waitpid(spid, NULL, 0);
}

static void paste_cancel_case(const char *seq, size_t n)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t spid = fork_server();
    if (spid == 0) {
        serve_body_delay(lfd, HAPPY, 400);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], seq, n);
    close(inp[1]);

    char url[64];
    static char out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "[canceled]") == NULL);
    ASH_CHECK(strstr(out, "Hello, world") != NULL);
    (void)waitpid(spid, NULL, 0);
}

static void test_paste_cancel_parsed(void)
{
    static const char SEQ[] = "hello\r\x1b[200~\x03\x1b[201~";
    paste_cancel_case(SEQ, sizeof SEQ - 1);
}

static void test_paste_cancel_unfed(void)
{
    enum { READ_SPLIT = 4096 };
    static const char TAIL[] = "\x03\x1b[201~";
    static char seq[READ_SPLIT + sizeof TAIL];

    size_t n = 0;
    memcpy(seq + n, "hello\r\x1b[200~", 12);
    n += 12;
    memset(seq + n, 'a', (size_t)READ_SPLIT - n);
    n = READ_SPLIT;
    memcpy(seq + n, TAIL, sizeof TAIL - 1);
    n += sizeof TAIL - 1;

    paste_cancel_case(seq, n);
}

static void headless_typeahead_case(const char *seq, size_t n)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t spid = fork_server();
    if (spid == 0) {
        serve_body_delay(lfd, HAPPY, 400);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], seq, n);
    close(inp[1]);

    char url[64];
    static char out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "Hello, world") != NULL);
    ASH_CHECK(strstr(out, "LATER") == NULL);
    ASH_CHECK(strstr(out, "bye") != NULL);
    (void)waitpid(spid, NULL, 0);
}

static void test_headless_typeahead_discarded(void)
{
    static const char SEQ[] = "hello\r!echo LAT''ER\r";
    headless_typeahead_case(SEQ, sizeof SEQ - 1);
}

static void test_headless_midturn_discarded(void)
{
    enum { READ_SPLIT = 4096 };
    static const char TAIL[] = "!echo LAT''ER\r";
    static char seq[READ_SPLIT + sizeof TAIL];

    size_t n = 0;
    memcpy(seq + n, "hello\r", 6);
    n += 6;
    memset(seq + n, 'a', (size_t)READ_SPLIT - n);
    n = READ_SPLIT;
    memcpy(seq + n, TAIL, sizeof TAIL - 1);
    n += sizeof TAIL - 1;

    headless_typeahead_case(seq, n);
}

static void test_queue_while_busy(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t spid = fork_server();
    if (spid == 0) {
        serve_body_delay(lfd, HAPPY, 400);
        serve_body(lfd, HAPPY2);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    ASH_CHECK(mfd >= 0);
    ASH_CHECK(grantpt(mfd) == 0);
    ASH_CHECK(unlockpt(mfd) == 0);
    const char *sname = ptsname(mfd);
    ASH_CHECK(sname != NULL);
    int sfd = open(sname, O_RDWR | O_NOCTTY);
    ASH_CHECK(sfd >= 0);

    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);

    pid_t lpid = fork_checked();
    if (lpid == 0) {
        close(mfd);
        ash_loop_cfg cfg = {
            .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
        };
        if (ash_screen_init(sfd) == ASH_OK) {
            ash_status st = ash_loop_run(&cfg, sfd, sfd);
            (void)st;
            ash_screen_shutdown();
        }
        close(sfd);
        _exit(0);
    }
    close(sfd);
    wait_until_raw(mfd);

    pid_t wpid = fork_checked();
    if (wpid == 0) {
        write_all(mfd, "first\r", 6);
        nap(150);
        write_all(mfd, "second\r", 7);
        nap(900);
        write_all(mfd, "/quit\r", 6);
        nap(150);
        _exit(0);
    }

    static char out[1 << 18];
    size_t got = 0;
    for (;;) {
        ssize_t r = read(mfd, out + got, sizeof out - 1 - got);
        if (r <= 0)
            break;
        got += (size_t)r;
        if (got >= sizeof out - 1)
            break;
    }
    out[got] = 0;
    close(mfd);

    const char *first = strstr(out, "first");
    const char *second = strstr(out, "second");
    ASH_CHECK(first != NULL);
    ASH_CHECK(second != NULL);
    ASH_CHECK(first < second);
    ASH_CHECK(strstr(out, "SECONDTURN") != NULL);

    (void)waitpid(wpid, NULL, 0);
    (void)waitpid(lpid, NULL, 0);
    (void)waitpid(spid, NULL, 0);
}

static pid_t spawn_loop_pty(const char *url, int *master)
{
    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    ASH_CHECK(mfd >= 0);
    ASH_CHECK(grantpt(mfd) == 0);
    ASH_CHECK(unlockpt(mfd) == 0);
    const char *sname = ptsname(mfd);
    ASH_CHECK(sname != NULL);
    int sfd = open(sname, O_RDWR | O_NOCTTY);
    ASH_CHECK(sfd >= 0);

    pid_t pid = fork_checked();
    if (pid == 0) {
        close(mfd);
        ash_loop_cfg cfg = {
            .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
        };
        if (ash_screen_init(sfd) == ASH_OK) {
            ash_status st = ash_loop_run(&cfg, sfd, sfd);
            (void)st;
            ash_screen_shutdown();
        }
        close(sfd);
        _exit(0);
    }
    close(sfd);
    wait_until_raw(mfd);
    *master = mfd;
    return pid;
}

static void test_two_lines_one_write(void)
{
    int mfd = -1;
    pid_t lpid = spawn_loop_pty("http://127.0.0.1:1/", &mfd);

    write_str_all(mfd, "!echo AA''A\r!echo BB''B\r");

    static char out[1 << 18];
    size_t got = 0;
    out[0] = 0;
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, "AAA"));
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, "BBB"));

    const char *first = strstr(out, "AAA");
    const char *second = strstr(out, "BBB");
    ASH_CHECK(first != NULL);
    ASH_CHECK(second != NULL);
    ASH_CHECK(first < second);

    write_str_all(mfd, "/quit\r");
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, NULL));
    close(mfd);

    (void)waitpid(lpid, NULL, 0);
}

static void test_submit_then_eof_one_write(void)
{
    static const char SEQ[] = "!echo ON''EX\r\x04";

    int mfd = -1;
    pid_t lpid = spawn_loop_pty("http://127.0.0.1:1/", &mfd);

    write_str_all(mfd, SEQ);

    static char out[1 << 18];
    size_t got = 0;
    out[0] = 0;
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, "ONEX"));

    write_str_all(mfd, "/quit\r");
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, NULL));
    close(mfd);

    (void)waitpid(lpid, NULL, 0);
}

static void test_two_submits_one_write(void)
{
    static const char SEQ[] = "!echo ON''EX\r\r!echo TW''OX\r";

    int mfd = -1;
    pid_t lpid = spawn_loop_pty("http://127.0.0.1:1/", &mfd);

    write_str_all(mfd, SEQ);

    static char out[1 << 18];
    size_t got = 0;
    out[0] = 0;
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, "ONEX"));
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, "TWOX"));

    const char *first = strstr(out, "ONEX");
    const char *second = strstr(out, "TWOX");
    ASH_CHECK(first != NULL);
    ASH_CHECK(second != NULL);
    ASH_CHECK(first < second);

    write_str_all(mfd, "/quit\r");
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, NULL));
    close(mfd);

    (void)waitpid(lpid, NULL, 0);
}

static void test_paste_mark_split(void)
{
    enum { READ_SPLIT = 4096 };
    static const char TAIL[] = "X\x1b[201~\x03!echo PA''STE\r";
    static char seq[READ_SPLIT + sizeof TAIL];

    size_t n = 0;
    memcpy(seq + n, "\x1b[200~", 6);
    n += 6;
    memset(seq + n, 'a', (size_t)READ_SPLIT - 3 - n);
    n = READ_SPLIT - 3;
    memcpy(seq + n, "\x1b[2", 3);
    n += 3;
    memcpy(seq + n, TAIL, sizeof TAIL - 1);
    n += sizeof TAIL - 1;

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], seq, n);
    close(inp[1]);

    char out[8192];
    run_loop("http://127.0.0.1:1/", inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "PASTE") != NULL);
    ASH_CHECK(strstr(out, "bye") != NULL);
}

static void test_transport_error(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_reset(lfd);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "hello\r", 6);
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "error:") != NULL);
    ASH_CHECK(strstr(out, "transfer failed") != NULL);
    ASH_CHECK(strstr(out, "http status 0") == NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_split_escape(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t spid = fork_server();
    if (spid == 0) {
        serve_body(lfd, HAPPY);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    pid_t wpid = fork_checked();
    if (wpid == 0) {
        close(inp[0]);
        write_all(inp[1], "hi\x1b", 3);
        nap(200);
        write_all(inp[1], "[Dworld\r", 8);
        nap(150);
        close(inp[1]);
        _exit(0);
    }
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "hiworld") != NULL);
    ASH_CHECK(strstr(out, "\x1b[D") == NULL);
    ASH_CHECK(strstr(out, "Hello, world") != NULL);
    (void)waitpid(wpid, NULL, 0);
    (void)waitpid(spid, NULL, 0);
}

static void test_tool(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, TOOL_USE);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "run it\r", 7);
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "echo TOOLTEST") != NULL);
    ASH_CHECK(strstr(out, "TOOLTEST\n") != NULL || strstr(out, "TOOLTEST\r") != NULL);
    ASH_CHECK(strstr(out, "Ran the tool.") != NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_tool_multi(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, TOOL_USE);
        serve_body(lfd, TOOL_USE_B);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "go\r", 3);
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "TOOLTEST") != NULL);
    ASH_CHECK(strstr(out, "ROUND2") != NULL);
    ASH_CHECK(strstr(out, "Ran the tool.") != NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_tool_parallel(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, TOOL_PARALLEL);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "go\r", 3);
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "PARA_AAA") != NULL);
    ASH_CHECK(strstr(out, "PARA_BBB") != NULL);
    ASH_CHECK(strstr(out, "Ran the tool.") != NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_env_scrub(void)
{
    setenv("ANTHROPIC_API_KEY", "SECRETVALUE", 1);

    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, TOOL_ENV);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "go\r", 3);
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "scrub=-end") != NULL);
    ASH_CHECK(strstr(out, "SECRETVALUE") == NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_tool_survivor(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, TOOL_SURVIVOR);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "go\r", 3);
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_loop(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "SURVIVOR") != NULL);
    ASH_CHECK(strstr(out, "Ran the tool.") != NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_tool_bigout(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, TOOL_BIGOUT);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "go\r", 3);
    close(inp[1]);

    int outfd = memfd_create("bigout", 0);
    ASH_CHECK(outfd >= 0);
    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_loop_cfg cfg = {
        .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
    };
    ASH_CHECK(ash_loop_run(&cfg, inp[0], outfd) == ASH_OK);
    close(inp[0]);

    off_t sz = lseek(outfd, 0, SEEK_END);
    ASH_CHECK(sz > 1000000 && sz < 1200000);
    char *buf = malloc((size_t)sz + 1);
    ASH_CHECK(buf != NULL);
    ASH_CHECK(lseek(outfd, 0, SEEK_SET) == 0);
    size_t got = 0;
    while (got < (size_t)sz) {
        ssize_t r = read(outfd, buf + got, (size_t)sz - got);
        if (r <= 0)
            break;
        got += (size_t)r;
    }
    buf[got] = 0;
    ASH_CHECK(strstr(buf, "[output truncated]") != NULL);
    ASH_CHECK(strstr(buf, "Ran the tool.") != NULL);
    free(buf);
    close(outfd);
    (void)waitpid(pid, NULL, 0);
}

static void test_slash(void)
{
    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    pid_t wpid = fork_checked();
    if (wpid == 0) {
        close(inp[0]);
        write_all(inp[1], "/help\r", 6);
        nap(120);
        write_all(inp[1], "/clear\r", 7);
        nap(120);
        write_all(inp[1], "/quit\r", 6);
        nap(80);
        close(inp[1]);
        _exit(0);
    }
    close(inp[1]);

    char out[8192];
    run_loop("http://127.0.0.1:1/", inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "commands:") != NULL);
    ASH_CHECK(strstr(out, "conversation cleared") != NULL);
    ASH_CHECK(strstr(out, "bye") != NULL);
    (void)waitpid(wpid, NULL, 0);
}

static void test_one_screen(void)
{
    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    close(inp[1]);

    char out[8192];
    run_loop("http://127.0.0.1:1/", inp[0], out, sizeof out);
    close(inp[0]);

    enum { INTRO_MAX_LINES = 20, INTRO_MAX_COLS = 80 };

    ASH_CHECK(strstr(out, "ash -") != NULL);
    const char *first_prompt = strchr(out, '>');
    size_t intro_len = first_prompt ? (size_t)(first_prompt - out) : strlen(out);
    int lines = 0, col = 0, maxcol = 0;
    for (size_t i = 0; i < intro_len; i++) {
        if (out[i] == '\n') {
            lines++;
            col = 0;
        } else if ((out[i] & 0xC0) != 0x80) {
            col++;
            if (col > maxcol)
                maxcol = col;
        }
    }
    ASH_CHECK(lines <= INTRO_MAX_LINES);
    ASH_CHECK(maxcol <= INTRO_MAX_COLS);
}

static void test_session(void)
{
    char spath[] = "/tmp/ash-session-XXXXXX";
    int sfd = mkstemp(spath);
    ASH_CHECK(sfd >= 0);
    close(sfd);

    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, TOOL_USE);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "go\r", 3);
    close(inp[1]);

    int outfd = memfd_create("out", 0);
    ASH_CHECK(outfd >= 0);
    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_loop_cfg cfg = {
        .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
        .session_path = spath,
    };
    ASH_CHECK(ash_loop_run(&cfg, inp[0], outfd) == ASH_OK);
    close(inp[0]);
    close(outfd);

    int rfd = open(spath, O_RDONLY);
    ASH_CHECK(rfd >= 0);
    char logbuf[8192];
    ssize_t ln = read(rfd, logbuf, sizeof logbuf);
    if (ln < 0)
        ln = 0;
    close(rfd);
    ASH_CHECK(memmem(logbuf, (size_t)ln, "user\tgo", 7) != NULL);
    ASH_CHECK(memmem(logbuf, (size_t)ln, "tool_use\tbash", 13) != NULL);
    ASH_CHECK(memmem(logbuf, (size_t)ln, "tool_result\tTOOLTEST", 20) != NULL);
    ASH_CHECK(memmem(logbuf, (size_t)ln, "assistant\tRan the tool.", 23) != NULL);
    unlink(spath);
    (void)waitpid(pid, NULL, 0);
}

static void test_clear_marker(void)
{
    char spath[] = "/tmp/ash-clearmark-XXXXXX";
    int sfd = mkstemp(spath);
    ASH_CHECK(sfd >= 0);
    close(sfd);

    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork_server();
    if (pid == 0) {
        serve_body(lfd, HAPPY);
        serve_body(lfd, HAPPY);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    pid_t wpid = fork_checked();
    if (wpid == 0) {
        close(inp[0]);
        write_all(inp[1], "one\r", 4);
        nap(200);
        write_all(inp[1], "/clear\r", 7);
        nap(200);
        write_all(inp[1], "two\r", 4);
        nap(150);
        close(inp[1]);
        _exit(0);
    }
    close(inp[1]);

    int outfd = memfd_create("out", 0);
    ASH_CHECK(outfd >= 0);
    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_loop_cfg cfg = {
        .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
        .session_path = spath,
    };
    ASH_CHECK(ash_loop_run(&cfg, inp[0], outfd) == ASH_OK);
    close(inp[0]);
    close(outfd);

    int rfd = open(spath, O_RDONLY);
    ASH_CHECK(rfd >= 0);
    uint8_t logbuf[8192];
    ssize_t ln = read(rfd, logbuf, sizeof logbuf);
    if (ln < 0)
        ln = 0;
    close(rfd);

    ASH_CHECK(memmem(logbuf, (size_t)ln, "user\tone", 8) != NULL);
    ASH_CHECK(memmem(logbuf, (size_t)ln, "user\ttwo", 8) != NULL);

    ash_span spans[64];
    size_t nsp = 0, trunc = 0;
    ASH_CHECK(ash_log_scan(logbuf, (size_t)ln, spans, 64, &nsp, &trunc) == ASH_OK);
    int marks = 0;
    for (size_t i = 0; i < nsp; i++)
        if (spans[i].type == ASH_REC_MARK)
            marks++;
    ASH_CHECK(marks == 1);

    unlink(spath);
    (void)waitpid(wpid, NULL, 0);
    (void)waitpid(pid, NULL, 0);
}

static void test_bang(void)
{
    char spath[] = "/tmp/ash_bang_XXXXXX";
    int sfd = mkstemp(spath);
    ASH_CHECK(sfd >= 0);
    close(sfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    write_all(inp[1], "!echo BANGTEST\r", 15);
    close(inp[1]);

    int outfd = memfd_create("bang-out", 0);
    ASH_CHECK(outfd >= 0);
    ash_loop_cfg cfg = {
        .url = "http://127.0.0.1:1/", .api_key = "k", .model = "claude-x",
        .max_tokens = 64, .session_path = spath,
    };
    ASH_CHECK(ash_loop_run(&cfg, inp[0], outfd) == ASH_OK);
    close(inp[0]);

    ASH_CHECK(lseek(outfd, 0, SEEK_SET) == 0);
    char out[8192];
    ssize_t rn = read(outfd, out, sizeof out - 1);
    if (rn < 0)
        rn = 0;
    out[rn] = 0;
    close(outfd);
    ASH_CHECK(strstr(out, "BANGTEST") != NULL);

    int rfd = open(spath, O_RDONLY);
    ASH_CHECK(rfd >= 0);
    uint8_t logbuf[8192];
    ssize_t ln = read(rfd, logbuf, sizeof logbuf);
    if (ln < 0)
        ln = 0;
    close(rfd);
    ASH_CHECK(memmem(logbuf, (size_t)ln, "Ran `echo BANGTEST`", 19) != NULL);
    unlink(spath);
}

static const char CONFIRM_ACCEPT_ENV[] = "ASH_TEST_CONFIRM_ACCEPT";
static const char CONFIRM_MARK_ENV[] = "ASH_TEST_CONFIRM_MARK";

void ash_diffview_init(ash_diffview *dv, ash_arena *arena, const char *path)
{
    memset(dv, 0, sizeof *dv);
    dv->arena = arena;
    dv->path = path;
}

void ash_diffview_set(ash_diffview *dv, const char *old, size_t oldlen,
                      const char *neu, size_t newlen)
{
    (void)dv;
    (void)old;
    (void)oldlen;
    (void)neu;
    (void)newlen;
}

static const char MODAL_MARK[] = "MODALUP";

void ash_diffview_render(ash_diffview *dv, ash_fb *fb, ash_rect rect,
                         const ash_diffview_theme *theme)
{
    (void)dv;
    (void)rect;
    (void)theme;
    ash_style st = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, ASH_ATTR_NONE };
    ash_fb_put_text(fb, 0, 0, st, MODAL_MARK, sizeof MODAL_MARK - 1);
}

ash_diffview_action ash_diffview_key(ash_diffview *dv, ash_key k, int view_h)
{
    (void)dv;
    (void)k;
    (void)view_h;
    const char *mark = getenv(CONFIRM_MARK_ENV);
    if (mark != NULL) {
        int fd = open(mark, O_WRONLY | O_APPEND);
        if (fd >= 0) {
            write_all(fd, "k", 1);
            close(fd);
        }
    }
    const char *accept = getenv(CONFIRM_ACCEPT_ENV);
    return accept != NULL && accept[0] == '1' ? ASH_DIFFVIEW_ACCEPT
                                              : ASH_DIFFVIEW_REJECT;
}

static size_t slurp(const char *path, char *out, size_t cap)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
        return 0;
    ssize_t n = read(fd, out, cap - 1);
    close(fd);
    if (n < 0)
        n = 0;
    out[n] = 0;
    return (size_t)n;
}

static void edit_body(char *out, size_t cap, const char *path)
{
    (void)snprintf(out, cap,
        "event: message_start\n"
        "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
        "\n"
        "event: content_block_start\n"
        "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
        "{\"type\":\"tool_use\",\"id\":\"toolu_ed\",\"name\":\"edit\",\"input\":{}}}\n"
        "\n"
        "event: content_block_delta\n"
        "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
        "{\"type\":\"input_json_delta\",\"partial_json\":"
        "\"{\\\"path\\\": \\\"%s\\\", \\\"oldText\\\": \\\"alpha\\\", "
        "\\\"newText\\\": \\\"gamma\\\"}\"}}\n"
        "\n"
        "event: message_delta\n"
        "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
        "\n"
        "event: message_stop\n"
        "data: {\"type\":\"message_stop\"}\n"
        "\n", path);
}

static void confirm_case(int accept, const char *want)
{
    char path[] = "/tmp/ash-confirm-XXXXXX";
    int tfd = mkstemp(path);
    ASH_CHECK(tfd >= 0);
    if (tfd < 0)
        return;
    write_all(tfd, "alpha\n", 6);
    close(tfd);

    char mark[] = "/tmp/ash-confirm-key-XXXXXX";
    int kfd = mkstemp(mark);
    ASH_CHECK(kfd >= 0);
    if (kfd < 0) {
        unlink(path);
        return;
    }
    close(kfd);

    setenv(CONFIRM_ACCEPT_ENV, accept ? "1" : "0", 1);
    setenv(CONFIRM_MARK_ENV, mark, 1);

    static char body[4096];
    edit_body(body, sizeof body, path);

    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t spid = fork_server();
    if (spid == 0) {
        serve_body(lfd, body);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int mfd = posix_openpt(O_RDWR | O_NOCTTY);
    ASH_CHECK(mfd >= 0);
    ASH_CHECK(grantpt(mfd) == 0);
    ASH_CHECK(unlockpt(mfd) == 0);
    const char *sname = ptsname(mfd);
    ASH_CHECK(sname != NULL);
    int sfd = open(sname, O_RDWR | O_NOCTTY);
    ASH_CHECK(sfd >= 0);

    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);

    pid_t lpid = fork_checked();
    if (lpid == 0) {
        close(mfd);
        ash_loop_cfg cfg = {
            .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
        };
        if (ash_screen_init(sfd) == ASH_OK) {
            ash_status st = ash_loop_run(&cfg, sfd, sfd);
            (void)st;
            ash_screen_shutdown();
        }
        close(sfd);
        _exit(0);
    }
    close(sfd);
    wait_until_raw(mfd);

    pid_t wpid = fork_checked();
    if (wpid == 0) {
        write_all(mfd, "go\r", 3);
        nap(700);
        write_all(mfd, "y", 1);
        nap(700);
        write_all(mfd, "\x03", 1);
        nap(200);
        write_all(mfd, "\x03", 1);
        nap(200);
        _exit(0);
    }

    static char out[1 << 18];
    size_t got = 0;
    for (;;) {
        ssize_t r = read(mfd, out + got, sizeof out - 1 - got);
        if (r <= 0)
            break;
        got += (size_t)r;
        if (got >= sizeof out - 1)
            break;
    }
    close(mfd);

    (void)waitpid(wpid, NULL, 0);
    (void)waitpid(lpid, NULL, 0);
    (void)waitpid(spid, NULL, 0);

    char keys[64];
    ASH_CHECK(slurp(mark, keys, sizeof keys) > 0);
    char content[256];
    (void)slurp(path, content, sizeof content);
    ASH_CHECK_STREQ(content, want);

    unlink(mark);
    unlink(path);
    unsetenv(CONFIRM_ACCEPT_ENV);
    unsetenv(CONFIRM_MARK_ENV);
}

static void test_modal_leftover(void)
{
    char path[] = "/tmp/ash-modal-XXXXXX";
    int tfd = mkstemp(path);
    ASH_CHECK(tfd >= 0);
    write_all(tfd, "alpha\n", 6);
    close(tfd);

    char mark[] = "/tmp/ash-modal-key-XXXXXX";
    int kfd = mkstemp(mark);
    ASH_CHECK(kfd >= 0);
    close(kfd);

    char cnt[] = "/tmp/ash-modal-cnt-XXXXXX";
    int cfd = mkstemp(cnt);
    ASH_CHECK(cfd >= 0);
    close(cfd);

    setenv(CONFIRM_ACCEPT_ENV, "0", 1);
    setenv(CONFIRM_MARK_ENV, mark, 1);

    static char body[4096];
    edit_body(body, sizeof body, path);

    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t spid = fork_server();
    if (spid == 0) {
        serve_body(lfd, body);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    int mfd = -1;
    pid_t lpid = spawn_loop_pty(url, &mfd);

    write_str_all(mfd, "go\r");

    static char out[1 << 18];
    size_t got = 0;
    out[0] = 0;
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, MODAL_MARK));

    char seq[256];
    int sn = snprintf(seq, sizeof seq, "\x03!printf x >>%s; echo RA''N\r", cnt);
    ASH_CHECK(sn > 0 && (size_t)sn < sizeof seq);
    write_str_all(mfd, seq);

    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, "RAN"));

    write_str_all(mfd, "/quit\r");
    ASH_CHECK(pty_read_until(mfd, out, sizeof out, &got, NULL));
    close(mfd);

    (void)waitpid(lpid, NULL, 0);
    (void)waitpid(spid, NULL, 0);

    char ran[64];
    size_t rn = slurp(cnt, ran, sizeof ran);
    ASH_CHECK(rn == 1);
    ASH_CHECK_STREQ(ran, "x");

    unlink(cnt);
    unlink(mark);
    unlink(path);
    unsetenv(CONFIRM_ACCEPT_ENV);
    unsetenv(CONFIRM_MARK_ENV);
}

static void test_confirm_reject(void)
{
    confirm_case(0, "alpha\n");
}

static void test_confirm_accept(void)
{
    confirm_case(1, "gamma\n");
}

int main(void)
{
    ASH_CHECK(ash_http_global_init() == ASH_OK);
    test_happy();
    test_bang();
    test_error();
    test_cancel();
    test_cancel_same_read();
    test_paste_cancel_parsed();
    test_paste_cancel_unfed();
    test_headless_typeahead_discarded();
    test_headless_midturn_discarded();
    test_queue_while_busy();
    test_two_lines_one_write();
    test_submit_then_eof_one_write();
    test_two_submits_one_write();
    test_paste_mark_split();
    test_transport_error();
    test_split_escape();
    test_tool();
    test_tool_multi();
    test_tool_parallel();
    test_env_scrub();
    test_tool_survivor();
    test_tool_bigout();
    test_slash();
    test_one_screen();
    test_session();
    test_clear_marker();
    test_confirm_reject();
    test_confirm_accept();
    test_modal_leftover();
    ash_http_global_cleanup();
    return ash_test_done();
}
