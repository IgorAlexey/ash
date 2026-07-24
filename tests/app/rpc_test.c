#include <arpa/inet.h>
#include <netinet/in.h>
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

static void write_all(int c, const char *p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(c, p + off, n - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
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

static void serve_body(int lfd, const char *body)
{
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        return;
    char req[2048];
    (void)read(c, req, sizeof req);
    char hdr[256];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      strlen(body));
    if (hn > 0) {
        write_all(c, hdr, (size_t)hn);
        write_all(c, body, strlen(body));
    }
    close(c);
}

static void run_rpc(const char *url, int in_fd, char *out, size_t cap)
{
    int outfd = memfd_create("rpc-out", 0);
    ASH_CHECK(outfd >= 0);
    ash_loop_cfg cfg = {
        .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
    };
    ASH_CHECK(ash_rpc_run(&cfg, in_fd, outfd) == ASH_OK);
    ASH_CHECK(lseek(outfd, 0, SEEK_SET) == 0);
    ssize_t rn = read(outfd, out, cap - 1);
    if (rn < 0)
        rn = 0;
    out[rn] = 0;
    close(outfd);
}

static void test_parse(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "parse", 1u << 16) == ASH_OK);
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
    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    const char *cmds =
        "{\"id\":\"1\",\"type\":\"get_state\"}\n"
        "{\"id\":\"2\",\"type\":\"get_messages\"}\n"
        "{\"id\":\"3\",\"type\":\"new_session\"}\n"
        "{\"id\":\"4\",\"type\":\"bogus\"}\n"
        "garbage line\n"
        "\n";
    write_all(inp[1], cmds, strlen(cmds));
    close(inp[1]);

    char out[8192];
    run_rpc("http://127.0.0.1:1/", inp[0], out, sizeof out);
    close(inp[0]);

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
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork();
    ASH_CHECK(pid >= 0);
    if (pid == 0) {
        serve_body(lfd, HAPPY);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    const char *cmd = "{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hi\"}\n";
    write_all(inp[1], cmd, strlen(cmd));
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_rpc(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "\"event\":\"turn_start\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"message_start\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"message_update\",\"text\":\"Hello, \"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"message_update\",\"text\":\"world\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"message_end\",\"stop_reason\":\"end_turn\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"turn_end\"") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"prompt\",\"id\":\"p1\",\"success\":true") != NULL);
    (void)waitpid(pid, NULL, 0);
}

static void test_prompt_tool(void)
{
    int port;
    int lfd = listen_loopback(&port);
    ASH_CHECK(lfd >= 0);
    pid_t pid = fork();
    ASH_CHECK(pid >= 0);
    if (pid == 0) {
        serve_body(lfd, TOOL_USE);
        serve_body(lfd, TOOL_FINAL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    int inp[2];
    ASH_CHECK(pipe(inp) == 0);
    const char *cmd = "{\"type\":\"prompt\",\"message\":\"run it\"}\n";
    write_all(inp[1], cmd, strlen(cmd));
    close(inp[1]);

    char url[64], out[8192];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    run_rpc(url, inp[0], out, sizeof out);
    close(inp[0]);

    ASH_CHECK(strstr(out, "\"event\":\"tool_execution_start\"") != NULL);
    ASH_CHECK(strstr(out, "\"name\":\"bash\"") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"echo TOOLTEST\"") != NULL);
    ASH_CHECK(strstr(out, "\"event\":\"tool_execution_end\"") != NULL);
    ASH_CHECK(strstr(out, "TOOLTEST") != NULL);
    ASH_CHECK(strstr(out, "\"is_error\":false") != NULL);
    ASH_CHECK(strstr(out, "\"command\":\"prompt\",\"success\":true") != NULL);
    (void)waitpid(pid, NULL, 0);
}

int main(void)
{
    ASH_CHECK(ash_http_global_init() == ASH_OK);
    test_parse();
    test_sync();
    test_prompt();
    test_prompt_tool();
    ash_http_global_cleanup();
    return ash_test_done();
}
