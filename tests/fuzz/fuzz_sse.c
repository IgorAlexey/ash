#include <stddef.h>
#include <stdint.h>

#include "ash/ai/sse.h"
#include "ash/base/arena.h"

struct hasher {
    uint64_t h;
};

static void hash_emit(void *ud, const ash_sse_event *ev)
{
    struct hasher *hs = ud;
    uint64_t h = hs->h;
    for (size_t i = 0; i < ev->type.len; i++) {
        h ^= (unsigned char)ev->type.p[i];
        h *= 1099511628211ULL;
    }
    h ^= 0x01;
    h *= 1099511628211ULL;
    for (size_t i = 0; i < ev->data.len; i++) {
        h ^= (unsigned char)ev->data.p[i];
        h *= 1099511628211ULL;
    }
    h ^= 0x02;
    h *= 1099511628211ULL;
    hs->h = h;
}

static ash_status feed_chunked(const uint8_t *d, size_t n, size_t chunk,
                               uint64_t *out)
{
    ash_arena a;
    if (ash_arena_create(&a, "fuzz", 4096) != ASH_OK) {
        *out = 0;
        return ASH_OK;
    }
    ash_sse_parser p;
    ash_sse_init(&p, &a);
    struct hasher hs = { 1469598103934665603ULL };
    ash_status st = ASH_OK;
    for (size_t off = 0; off < n && st == ASH_OK; off += chunk) {
        size_t m = chunk < n - off ? chunk : n - off;
        st = ash_sse_feed(&p, d + off, m, hash_emit, &hs);
    }
    *out = hs.h;
    ash_arena_destroy(&a);
    return st;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    uint64_t whole = 0;
    uint64_t byte = 0;
    ash_status sw = feed_chunked(data, size, size ? size : 1, &whole);
    ash_status sb = feed_chunked(data, size, 1, &byte);
    if (sw != sb)
        __builtin_trap();
    if (whole != byte)
        __builtin_trap();
    return 0;
}
