#include <arpa/inet.h>
#include <ctype.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ash/ai/http.h"
#include "ash/ai/provider.h"
#include "ash/base/arena.h"
#include "ash_test.h"

static const char RESP[] = "HTTP/1.1 200 OK\r\n"
                           "Content-Type: text/event-stream\r\n"
                           "Content-Length: 0\r\n"
                           "Connection: close\r\n\r\n";

static void on_text(void *ud, const char *t, size_t n)
{
    (void)ud;
    (void)t;
    (void)n;
}

static const char *tok_get(void *ctx)
{
    (void)ctx;
    return "tok-abc";
}

static void write_all(int fd, const char *p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
}

static int icontains(const char *hay, const char *needle)
{
    size_t nl = strlen(needle);
    for (const char *h = hay; *h; h++) {
        size_t i = 0;
        while (i < nl && h[i] &&
               tolower((unsigned char)h[i]) == tolower((unsigned char)needle[i]))
            i++;
        if (i == nl)
            return 1;
    }
    return 0;
}

static void run_case(const ash_provider_cfg *base, char *out, size_t cap)
{
    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    ASH_CHECK(lfd >= 0);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASH_CHECK(bind(lfd, (struct sockaddr *)&addr, sizeof addr) == 0);
    socklen_t al = sizeof addr;
    ASH_CHECK(getsockname(lfd, (struct sockaddr *)&addr, &al) == 0);
    int port = ntohs(addr.sin_port);
    ASH_CHECK(listen(lfd, 4) == 0);

    int pp[2];
    ASH_CHECK(pipe(pp) == 0);
    pid_t pid = fork();
    ASH_CHECK(pid >= 0);
    if (pid == 0) {
        close(pp[0]);
        int c = accept(lfd, NULL, NULL);
        if (c >= 0) {
            char req[4096];
            ssize_t rn = read(c, req, sizeof req);
            if (rn > 0)
                write_all(pp[1], req, (size_t)rn);
            write_all(c, RESP, strlen(RESP));
            close(c);
        }
        close(lfd);
        close(pp[1]);
        _exit(0);
    }
    close(pp[1]);

    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_provider_cfg cfg = *base;
    cfg.url = url;

    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "hdr", 1u << 16) == ASH_OK);
    ash_msg msgs[] = { { .role = "user", .content = "hi" } };
    char stop[32];
    ash_provider_stream *ps = NULL;
    if (ash_provider_start(&ps, &a, &cfg, msgs, 1, on_text, NULL, stop,
                           sizeof stop) == ASH_OK) {
        int running = 1;
        while (running) {
            if (ash_provider_wait(ps, -1, 1000, NULL) != ASH_OK)
                break;
            if (ash_provider_pump(ps, &running) != ASH_OK)
                break;
        }
        ash_provider_stream_close(ps);
    }

    size_t off = 0;
    ssize_t n;
    while (off < cap - 1 && (n = read(pp[0], out + off, cap - 1 - off)) > 0)
        off += (size_t)n;
    out[off] = '\0';
    close(pp[0]);
    close(lfd);
    (void)waitpid(pid, NULL, 0);
    ash_arena_destroy(&a);
}

int main(void)
{
    ASH_CHECK(ash_http_global_init() == ASH_OK);
    char req[8192];

    ash_provider_cfg oauth = {
        .provider = ash_provider_find("anthropic"),
        .model = "claude-x",
        .max_tokens = 32,
        .oauth_token = tok_get,
    };
    run_case(&oauth, req, sizeof req);
    ASH_CHECK(icontains(req, "Authorization: Bearer tok-abc"));
    ASH_CHECK(icontains(req, "anthropic-beta: oauth-2025-04-20"));
    ASH_CHECK(icontains(req, "anthropic-version: 2023-06-01"));
    ASH_CHECK(!icontains(req, "x-api-key"));

    ash_provider_cfg key = {
        .provider = ash_provider_find("anthropic"),
        .model = "claude-x",
        .max_tokens = 32,
        .api_key = "sk-secret",
    };
    run_case(&key, req, sizeof req);
    ASH_CHECK(icontains(req, "x-api-key: sk-secret"));
    ASH_CHECK(icontains(req, "anthropic-version: 2023-06-01"));
    ASH_CHECK(!icontains(req, "Authorization: Bearer"));
    ASH_CHECK(!icontains(req, "anthropic-beta"));

    ash_http_global_cleanup();
    return ash_test_done();
}
