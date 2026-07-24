#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ash/ai/http.h"
#include "ash/base/arena.h"
#include "ash_test.h"

struct collector {
    char   buf[4096];
    size_t len;
};

static void collect(void *ud, const char *data, size_t n)
{
    struct collector *c = ud;
    if (c->len + n <= sizeof c->buf) {
        memcpy(c->buf + c->len, data, n);
        c->len += n;
    }
}

static int read_request(int c, char *buf, size_t cap, char **body, size_t *blen)
{
    size_t got = 0;
    char *end = NULL;
    long clen = -1;
    while (got < cap - 1) {
        ssize_t r = read(c, buf + got, cap - 1 - got);
        if (r <= 0)
            break;
        got += (size_t)r;
        buf[got] = 0;
        if (end == NULL) {
            end = strstr(buf, "\r\n\r\n");
            char *cl = strstr(buf, "Content-Length:");
            if (cl != NULL)
                clen = strtol(cl + 15, NULL, 10);
        }
        if (end != NULL) {
            size_t off = (size_t)(end + 4 - buf);
            size_t have = got - off;
            if (clen < 0 || have >= (size_t)clen) {
                *body = buf + off;
                *blen = clen < 0 ? have : (size_t)clen;
                return 0;
            }
        }
    }
    return -1;
}

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

static void serve_once(int lfd)
{
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        return;
    char req[2048];
    char *body = NULL;
    size_t blen = 0;
    if (read_request(c, req, sizeof req, &body, &blen) == 0) {
        char hdr[256];
        int hn = snprintf(hdr, sizeof hdr,
                          "HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/event-stream\r\n"
                          "Content-Length: %zu\r\n"
                          "Connection: close\r\n\r\n",
                          blen);
        if (hn > 0) {
            write_all(c, hdr, (size_t)hn);
            write_all(c, body, blen);
        }
    }
    close(c);
}

int main(void)
{
    ASH_CHECK(ash_http_global_init() == ASH_OK);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    ASH_CHECK(lfd >= 0);
    int one = 1;
    (void)setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASH_CHECK(bind(lfd, (struct sockaddr *)&addr, sizeof addr) == 0);
    socklen_t al = sizeof addr;
    ASH_CHECK(getsockname(lfd, (struct sockaddr *)&addr, &al) == 0);
    int port = ntohs(addr.sin_port);
    ASH_CHECK(listen(lfd, 1) == 0);

    pid_t pid = fork();
    ASH_CHECK(pid >= 0);
    if (pid == 0) {
        serve_once(lfd);
        serve_once(lfd);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);

    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "http", 1u << 16) == ASH_OK);
    struct collector col = { .len = 0 };

    static const char expect[] = { 'A', 'B', 0, 'C', 'D' };
    char body[sizeof expect];
    memcpy(body, expect, sizeof expect);
    size_t blen = sizeof expect;
    ash_http *h = NULL;
    ASH_CHECK(ash_http_start(&h, &a, url, body, blen, NULL, collect, &col) == ASH_OK);
    memset(body, 'X', blen);

    long status = 0;
    ASH_CHECK(ash_http_run(h, &status) == ASH_OK);
    ASH_CHECK(status == 200);
    ASH_CHECK(col.len == blen);
    ASH_CHECK(memcmp(col.buf, expect, blen) == 0);
    ash_http_close(h);

    struct collector empty = { .len = 0 };
    ash_http *h2 = NULL;
    ASH_CHECK(ash_http_start(&h2, &a, url, NULL, 0, NULL, collect, &empty) == ASH_OK);
    long status2 = 0;
    ASH_CHECK(ash_http_run(h2, &status2) == ASH_OK);
    ASH_CHECK(status2 == 200);
    ASH_CHECK(empty.len == 0);
    ash_http_close(h2);

    ash_arena_destroy(&a);
    ash_http_global_cleanup();
    (void)waitpid(pid, NULL, 0);
    return ash_test_done();
}
