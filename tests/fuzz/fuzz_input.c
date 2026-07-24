#include <stddef.h>
#include <stdint.h>

#include "ash/term/input.h"

struct acc {
    uint64_t structural;
    uint64_t textbytes;
};

static void mix(uint64_t *h, uint64_t v)
{
    *h ^= v;
    *h *= 1099511628211ULL;
}

static void accumulate(struct acc *a, const ash_input_event *ev, uint32_t n)
{
    for (uint32_t i = 0; i < n; i++) {
        const ash_input_event *e = &ev[i];
        if (e->kind == ASH_EV_TEXT || e->kind == ASH_EV_PASTE_CHUNK) {
            for (uint32_t k = 0; k < e->len; k++)
                mix(&a->textbytes, (unsigned char)e->text[k]);
        } else {
            mix(&a->structural, (uint64_t)e->kind);
            mix(&a->structural, e->key);
            mix(&a->structural, e->mods);
        }
    }
}

static struct acc feed_chunked(const uint8_t *data, size_t size, size_t chunk)
{
    struct acc a = { 1469598103934665603ULL, 1469598103934665603ULL };
    ash_input in;
    ash_input_init(&in);
    ash_input_event out[256];
    size_t off = 0;
    while (off < size) {
        size_t m = chunk < size - off ? chunk : size - off;
        size_t fed = 0;
        while (fed < m) {
            uint32_t consumed = 0, produced = 0;
            ASH_IGNORE(ash_input_feed(&in, data + off + fed, (uint32_t)(m - fed),
                                      out, 256, &consumed, &produced));
            accumulate(&a, out, produced);
            if (consumed == 0 && produced == 0)
                break;
            fed += consumed;
        }
        off += m;
    }
    return a;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    struct acc whole = feed_chunked(data, size, size ? size : 1);
    struct acc byte = feed_chunked(data, size, 1);
    if (whole.structural != byte.structural)
        __builtin_trap();
    if (whole.textbytes != byte.textbytes)
        __builtin_trap();
    return 0;
}
