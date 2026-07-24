#include <string.h>

#include "ash/ai/sse.h"
#include "ash/base/arena.h"
#include "ash_test.h"

enum { MAXEV = 64 };

typedef struct collector {
    char type[MAXEV][64];
    char data[MAXEV][256];
    int  n;
} collector;

static void collect(void *ud, const ash_sse_event *ev)
{
    collector *c = ud;
    if (c->n >= MAXEV)
        return;
    size_t tl = ev->type.len < 63 ? ev->type.len : 63;
    size_t dl = ev->data.len < 255 ? ev->data.len : 255;
    if (tl)
        memcpy(c->type[c->n], ev->type.p, tl);
    c->type[c->n][tl] = '\0';
    if (dl)
        memcpy(c->data[c->n], ev->data.p, dl);
    c->data[c->n][dl] = '\0';
    c->n++;
}

static void ignore(void *ud, const ash_sse_event *ev)
{
    (void)ud;
    (void)ev;
}

static void feed_split(const char *stream, size_t chunk, collector *out)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "sse", 256) == ASH_OK);
    ash_sse_parser p;
    ash_sse_init(&p, &a);
    out->n = 0;
    size_t n = strlen(stream);
    for (size_t i = 0; i < n; i += chunk) {
        size_t m = i + chunk <= n ? chunk : n - i;
        ASH_CHECK(ash_sse_feed(&p, stream + i, m, collect, out) == ASH_OK);
    }
    ash_arena_destroy(&a);
}

static int eqcol(const collector *a, const collector *b)
{
    if (a->n != b->n)
        return 0;
    for (int i = 0; i < a->n; i++)
        if (strcmp(a->type[i], b->type[i]) != 0 ||
            strcmp(a->data[i], b->data[i]) != 0)
            return 0;
    return 1;
}

static void test_line_cap(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "cap", 4096) == ASH_OK);
    ash_sse_parser p;
    ash_sse_init(&p, &a);

    static char big[ASH_SSE_MAX_LINE + 16];
    memset(big, 'x', sizeof big);
    ash_status st = ash_sse_feed(&p, big, sizeof big, ignore, NULL);
    ASH_CHECK(st == ASH_ERR_PROTOCOL);
    ASH_CHECK(ash_sse_feed(&p, "y", 1, ignore, NULL) == ASH_ERR_PROTOCOL);

    ash_arena_destroy(&a);
}

int main(void)
{
    const char *streams[] = {
        "event: message\ndata: hello\n\n",
        "data: a\ndata: b\ndata: c\n\n",
        ": comment\nevent: ping\ndata: {}\n\nevent: done\ndata: bye\n\n",
        "data: crlf\r\n\r\n",
        "event:nospace\ndata:x\n\n",
    };
    for (size_t k = 0; k < sizeof streams / sizeof streams[0]; k++) {
        collector whole, one, three;
        feed_split(streams[k], strlen(streams[k]), &whole);
        feed_split(streams[k], 1, &one);
        feed_split(streams[k], 3, &three);
        ASH_CHECK(eqcol(&whole, &one));
        ASH_CHECK(eqcol(&whole, &three));
    }

    collector c;
    feed_split("event: message\ndata: hello\n\n", 1000, &c);
    ASH_CHECK(c.n == 1);
    ASH_CHECK_STREQ(c.type[0], "message");
    ASH_CHECK_STREQ(c.data[0], "hello");

    feed_split("data: a\ndata: b\ndata: c\n\n", 1000, &c);
    ASH_CHECK(c.n == 1);
    ASH_CHECK_STREQ(c.data[0], "a\nb\nc");
    ASH_CHECK_STREQ(c.type[0], "message");

    feed_split(": x\nevent: ping\ndata: {}\n\nevent: done\ndata: bye\n\n", 1000, &c);
    ASH_CHECK(c.n == 2);
    ASH_CHECK_STREQ(c.type[0], "ping");
    ASH_CHECK_STREQ(c.data[0], "{}");
    ASH_CHECK_STREQ(c.type[1], "done");
    ASH_CHECK_STREQ(c.data[1], "bye");

    feed_split("event:nospace\ndata:x\n\n", 1000, &c);
    ASH_CHECK(c.n == 1);
    ASH_CHECK_STREQ(c.type[0], "nospace");
    ASH_CHECK_STREQ(c.data[0], "x");

    test_line_cap();

    return ash_test_done();
}
