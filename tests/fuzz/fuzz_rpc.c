#include <stddef.h>
#include <stdint.h>

#include "ash/app/rpc.h"
#include "ash/base/arena.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ash_arena a;
    if (ash_arena_create(&a, "fuzz-rpc", 1u << 16) != ASH_OK)
        return 0;
    ash_rpc_cmd c;
    if (ash_rpc_parse(&a, (const char *)data, size, &c) == ASH_OK) {
        volatile ash_rpc_kind k = c.kind;
        (void)k;
        (void)c.id.len;
        (void)c.type.len;
        (void)c.message.len;
    }
    ash_arena_destroy(&a);
    return 0;
}
