#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/edit/gapbuf.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ash_arena a;
    if (ash_arena_create(&a, "fuzz-gap", 1u << 16) != ASH_OK)
        return 0;

    ash_gapbuf b;
    ash_gapbuf_init(&b, &a);

    size_t i = 0;
    while (i + 3 <= size) {
        uint8_t op = data[i];
        size_t pos = data[i + 1];
        size_t n = data[i + 2];
        i += 3;
        size_t len = ash_gapbuf_len(&b);
        if (len)
            pos %= len + 1;
        else
            pos = 0;
        size_t avail = size - i;
        if (n > avail)
            n = avail;

        switch (op & 3u) {
        case 0:
            ash_gapbuf_insert(&b, pos, data + i, n);
            i += n;
            break;
        case 1: {
            size_t end = pos + n;
            if (end > len)
                end = len;
            ash_gapbuf_erase(&b, pos, end);
            break;
        }
        case 2:
            ash_gapbuf_replace(&b, pos, pos + (n / 2), data + i, n - n / 2);
            i += n - n / 2;
            break;
        default: {
            uint8_t tmp[64];
            (void)ash_gapbuf_copy(&b, pos, tmp, sizeof tmp < n ? sizeof tmp : n);
            (void)ash_gapbuf_at(&b, pos);
            break;
        }
        }
    }

    ash_buf out;
    ash_buf_init(&out, &a);
    ash_gapbuf_extract(&b, &out);
    if (out.len != ash_gapbuf_len(&b))
        __builtin_trap();

    ash_arena_destroy(&a);
    return 0;
}
