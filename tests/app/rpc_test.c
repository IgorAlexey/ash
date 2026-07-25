#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ash/ai/http.h"
#include "ash/app/rpc.h"
#include "ash/base/arena.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
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

static const char TOOL_USE_BINARY[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_b\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":"
    "\"{\\\"command\\\": \\\"printf '\\\\\\\\377\\\\\\\\376'\\\"}\"}}\n"
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

static void report_errno(const char *call, int err)
{
    if (err == 0) {
        fprintf(stderr, "  %s failed, errno not set\n", call);
        return;
    }
    fprintf(stderr, "  %s failed: %s\n", call, strerror(err));
}

static void report_status(const char *call, ash_status st)
{
    fprintf(stderr, "  %s failed: %s (%d)\n", call, ash_status_str(st), (int)st);
}

static ash_status write_all(int c, const char *p, size_t n, int *err)
{
    size_t off = 0;
    *err = 0;
    while (off < n) {
        ssize_t w = write(c, p + off, n - off);
        if (w < 0 && errno == EINTR)
            continue;
        if (w < 0) {
            *err = errno;
            return ASH_ERR_IO;
        }
        if (w == 0)
            return ASH_ERR_IO;
        off += (size_t)w;
    }
    return ASH_OK;
}

static int listen_loopback(int *port, const char **call, int *err)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    if (lfd < 0) {
        *call = "socket";
        *err = errno;
        return -1;
    }
    int one = 1;
    (void)setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(lfd, (struct sockaddr *)&addr, sizeof addr) != 0) {
        *call = "bind";
        *err = errno;
        close(lfd);
        return -1;
    }
    socklen_t al = sizeof addr;
    (void)getsockname(lfd, (struct sockaddr *)&addr, &al);
    *port = ntohs(addr.sin_port);
    if (listen(lfd, 1) != 0) {
        *call = "listen";
        *err = errno;
        close(lfd);
        return -1;
    }
    return lfd;
}

static void serve_body(int lfd, const char *body)
{
    int c = accept(lfd, NULL, NULL);
    int err = errno;
    ASH_CHECK(c >= 0);
    if (c < 0) {
        report_errno("accept", err);
        return;
    }
    char req[2048];
    (void)read(c, req, sizeof req);
    char hdr[256];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      strlen(body));
    ASH_CHECK(hn > 0 && (size_t)hn < sizeof hdr);
    if (hn <= 0 || (size_t)hn >= sizeof hdr) {
        close(c);
        return;
    }
    int werr = 0;
    ash_status wst = write_all(c, hdr, (size_t)hn, &werr);
    ASH_CHECK(wst == ASH_OK);
    if (wst != ASH_OK) {
        report_errno("write", werr);
        close(c);
        return;
    }
    wst = write_all(c, body, strlen(body), &werr);
    ASH_CHECK(wst == ASH_OK);
    if (wst != ASH_OK)
        report_errno("write", werr);
    close(c);
}

static ash_status run_rpc(const char *url, int in_fd, char *out, size_t cap)
{
    ASH_CHECK(cap != 0);
    if (cap == 0)
        return ASH_ERR_RANGE;
    out[0] = 0;
    int outfd = memfd_create("rpc-out", 0);
    int err = errno;
    ASH_CHECK(outfd >= 0);
    if (outfd < 0) {
        report_errno("memfd_create", err);
        return ASH_ERR_OS;
    }
    ash_loop_cfg cfg = {
        .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
    };
    ash_status st = ash_rpc_run(&cfg, in_fd, outfd);
    ASH_CHECK(st == ASH_OK);
    if (st != ASH_OK) {
        report_status("ash_rpc_run", st);
        close(outfd);
        return st;
    }
    off_t pos = lseek(outfd, 0, SEEK_SET);
    err = errno;
    ASH_CHECK(pos == 0);
    if (pos != 0) {
        report_errno("lseek", err);
        close(outfd);
        return ASH_ERR_OS;
    }
    ssize_t rn = read(outfd, out, cap - 1);
    err = errno;
    ASH_CHECK(rn >= 0);
    if (rn < 0) {
        report_errno("read", err);
        close(outfd);
        return ASH_ERR_OS;
    }
    out[rn] = 0;
    close(outfd);
    return ASH_OK;
}

static void report_wstatus(const char *what, int wstatus)
{
    if (WIFSIGNALED(wstatus)) {
        fprintf(stderr, "  %s killed by signal %d\n", what, WTERMSIG(wstatus));
        return;
    }
    if (WIFEXITED(wstatus)) {
        fprintf(stderr, "  %s exited with status %d\n", what,
                WEXITSTATUS(wstatus));
        return;
    }
    fprintf(stderr, "  %s ended with wait status 0x%x\n", what,
            (unsigned)wstatus);
}

static void reap(pid_t pid, int *wstatus)
{
    ASH_CHECK(pid > 0);
    if (pid <= 0)
        return;
    int st = 0;
    while (waitpid(pid, &st, 0) < 0 && errno == EINTR)
        ;
    if (wstatus != NULL)
        *wstatus = st;
}

static void kill_child(pid_t pid)
{
    ASH_CHECK(pid > 0);
    if (pid <= 0)
        return;
    (void)kill(pid, SIGKILL);
    reap(pid, NULL);
}

#define REAP_OK(pid)                                            \
    do {                                                        \
        int wstatus = 0;                                        \
        reap(pid, &wstatus);                                    \
        int child_ok = WIFEXITED(wstatus) &&                    \
                       WEXITSTATUS(wstatus) == 0;               \
        ASH_CHECK(child_ok);                                    \
        if (!child_ok)                                          \
            report_wstatus("server child", wstatus);            \
    } while (0)

#define FORK_OR_BAIL(pid, cleanup)              \
    do {                                        \
        (pid) = fork();                         \
        int fork_err = errno;                   \
        ASH_CHECK((pid) >= 0);                  \
        if ((pid) < 0) {                        \
            report_errno("fork", fork_err);     \
            cleanup;                            \
            return;                             \
        }                                       \
    } while (0)

#define PIPE_OR_BAIL(fds, cleanup)              \
    do {                                        \
        int pipe_rc = pipe(fds);                \
        int pipe_err = errno;                   \
        ASH_CHECK(pipe_rc == 0);                \
        if (pipe_rc != 0) {                     \
            report_errno("pipe", pipe_err);     \
            cleanup;                            \
            return;                             \
        }                                       \
    } while (0)

static void test_parse(void)
{
    ash_arena a;
    ash_status st = ash_arena_create(&a, "parse", 1u << 16);
    ASH_CHECK(st == ASH_OK);
    if (st != ASH_OK) {
        report_status("ash_arena_create", st);
        return;
    }
    ash_rpc_cmd c;

    const char *p1 = "{\"id\":\"7\",\"type\":\"prompt\",\"message\":\"hi\"}";
    ASH_CHECK(ash_rpc_parse(&a, p1, strlen(p1), &c) == ASH_OK);
    ASH_CHECK(c.kind == ASH_RPC_PROMPT);
    ASH_CHECK(c.id.len == 1 && c.id.p[0] == '7');
    ASH_CHECK(c.message.len == 2 && memcmp(c.message.p, "hi", 2) == 0);

    const char *p2 = "{\"type\":\"get_state\"}";
    ASH_CHECK(ash_rpc_parse(&a, p2, strlen(p2), &c) == ASH_OK);
    ASH_CHECK(c.kind == ASH_RPC_GET_STATE && c.id.len == 0);

    const char *p3 = "{\"type\":\"frobnicate\"}";
    ASH_CHECK(ash_rpc_parse(&a, p3, strlen(p3), &c) == ASH_OK);
    ASH_CHECK(c.kind == ASH_RPC_UNKNOWN);

    const char *p4 = "not json";
    ASH_CHECK(ash_rpc_parse(&a, p4, strlen(p4), &c) != ASH_OK);

    const char *p5 = "[1,2,3]";
    ASH_CHECK(ash_rpc_parse(&a, p5, strlen(p5), &c) != ASH_OK);

    const char *p6 = "{\"message\":\"no type\"}";
    ASH_CHECK(ash_rpc_parse(&a, p6, strlen(p6), &c) != ASH_OK);

    ash_arena_destroy(&a);
}

static void test_sync(void)
{
    int inp[2] = { -1, -1 };
    PIPE_OR_BAIL(inp, (void)0);
    const char *cmds =
        "{\"id\":\"1\",\"type\":\"get_state\"}\n"
        "{\"id\":\"2\",\"type\":\"get_messages\"}\n"
        "{\"id\":\"3\",\"type\":\"new_session\"}\n"
        "{\"id\":\"4\",\"type\":\"bogus\"}\n"
        "garbage line\n"
        "\n";
    int werr = 0;
    ash_status wst = write_all(inp[1], cmds, strlen(cmds), &werr);
    close(inp[1]);
    ASH_CHECK(wst == ASH_OK);
    if (wst != ASH_OK) {
        report_errno("write", werr);
        close(inp[0]);
        return;
    }

    char out[8192];
    ash_status st = run_rpc("http://127.0.0.1:1/", inp[0], out, sizeof out);
    close(inp[0]);
    ASH_CHECK(st == ASH_OK);
    if (st != ASH_OK) {
        report_status("run_rpc", st);
        return;
    }

    ASH_CHECK(strstr(out, "\"command\":\"get_state\",\"id\":\"1\",\"success\":true") != NULL);
    ASH_CHECK(strstr(out, "\"message_count\":0") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"get_messages\",\"id\":\"2\"") != NULL);
    ASH_CHECK(strstr(out, "\"messages\":[]") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"new_session\",\"id\":\"3\",\"success\":true") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"bogus\",\"id\":\"4\",\"success\":false") != NULL);
    ASH_CHECK(strstr(out, "unknown command type") != NULL);
    ASH_CHECK(strstr(out, "\"success\":false,\"error\":\"json") != NULL);
}

static void test_prompt(void)
{
    int port = -1;
    const char *call = "listen_loopback";
    int err = 0;
    int lfd = listen_loopback(&port, &call, &err);
    ASH_CHECK(lfd >= 0);
    if (lfd < 0) {
        report_errno(call, err);
        return;
    }
    pid_t pid = -1;
    FORK_OR_BAIL(pid, close(lfd));
    if (pid == 0) {
        serve_body(lfd, HAPPY);
        close(lfd);
        _exit(ash_test_fails == 0 ? 0 : 1);
    }
    close(lfd);

    int inp[2] = { -1, -1 };
    PIPE_OR_BAIL(inp, kill_child(pid));
    const char *cmd = "{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hi\"}\n";
    int werr = 0;
    ash_status wst = write_all(inp[1], cmd, strlen(cmd), &werr);
    close(inp[1]);
    ASH_CHECK(wst == ASH_OK);
    if (wst != ASH_OK) {
        report_errno("write", werr);
        close(inp[0]);
        kill_child(pid);
        return;
    }

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_status st = run_rpc(url, inp[0], out, sizeof out);
    close(inp[0]);
    ASH_CHECK(st == ASH_OK);
    if (st != ASH_OK) {
        report_status("run_rpc", st);
        kill_child(pid);
        return;
    }

    ASH_CHECK(strstr(out, "\"event\":\"turn_start\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"message_start\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"message_update\",\"text\":\"Hello, \"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"message_update\",\"text\":\"world\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"message_end\",\"stop_reason\":\"end_turn\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"turn_end\"") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"prompt\",\"id\":\"p1\",\"success\":true") != NULL);
    REAP_OK(pid);
}

static void test_prompt_tool(void)
{
    int port = -1;
    const char *call = "listen_loopback";
    int err = 0;
    int lfd = listen_loopback(&port, &call, &err);
    ASH_CHECK(lfd >= 0);
    if (lfd < 0) {
        report_errno(call, err);
        return;
    }
    pid_t pid = -1;
    FORK_OR_BAIL(pid, close(lfd));
    if (pid == 0) {
        serve_body(lfd, TOOL_USE);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(ash_test_fails == 0 ? 0 : 1);
    }
    close(lfd);

    int inp[2] = { -1, -1 };
    PIPE_OR_BAIL(inp, kill_child(pid));
    const char *cmd = "{\"type\":\"prompt\",\"message\":\"run it\"}\n";
    int werr = 0;
    ash_status wst = write_all(inp[1], cmd, strlen(cmd), &werr);
    close(inp[1]);
    ASH_CHECK(wst == ASH_OK);
    if (wst != ASH_OK) {
        report_errno("write", werr);
        close(inp[0]);
        kill_child(pid);
        return;
    }

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_status st = run_rpc(url, inp[0], out, sizeof out);
    close(inp[0]);
    ASH_CHECK(st == ASH_OK);
    if (st != ASH_OK) {
        report_status("run_rpc", st);
        kill_child(pid);
        return;
    }

    ASH_CHECK(strstr(out, "\"event\":\"tool_execution_start\"") != NULL);
    ASH_CHECK(strstr(out, "\"name\":\"bash\"") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"echo TOOLTEST\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"tool_execution_end\"") != NULL);
    ASH_CHECK(strstr(out, "TOOLTEST") != NULL);
    ASH_CHECK(strstr(out, "\"is_error\":false") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"prompt\",\"success\":true") != NULL);
    REAP_OK(pid);
}

static void check_tool_end_line(ash_arena *a, const char *line, size_t n,
                                int *seen)
{
    ash_json v;
    ash_status st = ash_json_parse(a, line, n, &v);
    ASH_CHECK(st == ASH_OK);
    if (st != ASH_OK)
        return;
    ASH_CHECK(v.type == ASH_JSON_OBJECT);

    const ash_json *ev = ash_json_get(&v, "event");
    ash_slice es;
    if (ev == NULL || ash_json_str(ev, &es) != ASH_OK)
        return;
    if (es.len != sizeof "tool_execution_end" - 1 ||
        memcmp(es.p, "tool_execution_end", es.len) != 0)
        return;

    (*seen)++;
    const ash_json *res = ash_json_get(&v, "result");
    ASH_CHECK(res != NULL);
    if (res == NULL)
        return;
    ash_slice rs;
    ash_status rst = ash_json_str(res, &rs);
    ASH_CHECK(rst == ASH_OK);
    if (rst != ASH_OK)
        return;
    static const unsigned char want[] = { 0xef, 0xbf, 0xbd, 0xef, 0xbf, 0xbd };
    ASH_CHECK(rs.len == sizeof want && memcmp(rs.p, want, sizeof want) == 0);
}

static void test_prompt_tool_binary(void)
{
    int port = -1;
    const char *call = "listen_loopback";
    int err = 0;
    int lfd = listen_loopback(&port, &call, &err);
    ASH_CHECK(lfd >= 0);
    if (lfd < 0) {
        report_errno(call, err);
        return;
    }
    pid_t pid = -1;
    FORK_OR_BAIL(pid, close(lfd));
    if (pid == 0) {
        serve_body(lfd, TOOL_USE_BINARY);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(ash_test_fails == 0 ? 0 : 1);
    }
    close(lfd);

    int inp[2] = { -1, -1 };
    PIPE_OR_BAIL(inp, kill_child(pid));
    const char *cmd = "{\"type\":\"prompt\",\"message\":\"dump bytes\"}\n";
    int werr = 0;
    ash_status wst = write_all(inp[1], cmd, strlen(cmd), &werr);
    close(inp[1]);
    ASH_CHECK(wst == ASH_OK);
    if (wst != ASH_OK) {
        report_errno("write", werr);
        close(inp[0]);
        kill_child(pid);
        return;
    }

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_status st = run_rpc(url, inp[0], out, sizeof out);
    close(inp[0]);
    ASH_CHECK(st == ASH_OK);
    if (st != ASH_OK) {
        report_status("run_rpc", st);
        kill_child(pid);
        return;
    }
    REAP_OK(pid);

    ash_arena a;
    st = ash_arena_create(&a, "toolbin", 1u << 18);
    ASH_CHECK(st == ASH_OK);
    if (st != ASH_OK) {
        report_status("ash_arena_create", st);
        return;
    }

    int seen = 0;
    const char *p = out;
    while (*p != 0) {
        const char *nl = strchr(p, '\n');
        size_t n = nl != NULL ? (size_t)(nl - p) : strlen(p);
        if (n > 0)
            check_tool_end_line(&a, p, n, &seen);
        p = nl != NULL ? nl + 1 : p + n;
    }
    ASH_CHECK(seen == 1);
    ash_arena_destroy(&a);
}

int main(void)
{
    ASH_CHECK(ash_http_global_init() == ASH_OK);
    test_parse();
    test_sync();
    test_prompt();
    test_prompt_tool();
    test_prompt_tool_binary();
    ash_http_global_cleanup();
    return ash_test_done();
}
