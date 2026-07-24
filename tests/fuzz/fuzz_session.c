#include <stddef.h>
#include <stdint.h>

#include "ash/core/session.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    static ash_span spans[8192];
    size_t n = 0;
    size_t trunc = 0;
    ash_status st = ash_log_scan(data, size, spans, 8192, &n, &trunc);

    if (st == ASH_ERR_NOSPACE)
        return 0;
    if (st != ASH_OK)
        __builtin_trap();

    if (trunc > size)
        __builtin_trap();

    size_t off = 0;
    for (size_t i = 0; i < n; i++) {
        if (spans[i].off != off)
            __builtin_trap();
        if (spans[i].seq != i)
            __builtin_trap();
        if (spans[i].payload_off != off + 32)
            __builtin_trap();
        if (spans[i].payload_off + spans[i].payload_len > size)
            __builtin_trap();
        off += 32 + spans[i].payload_len;
    }
    if (off != trunc)
        __builtin_trap();
    return 0;
}
