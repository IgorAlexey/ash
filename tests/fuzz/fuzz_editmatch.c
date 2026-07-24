#include <stddef.h>
#include <stdint.h>

#include "ash/base/arena.h"
#include "ash/tools/tools.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ash_arena a;
    if (ash_arena_create(&a, "fuzz-edit", 1u << 16) != ASH_OK)
        return 0;

    size_t nlen = size > 0 ? data[0] : 0;
    size_t body = size > 0 ? size - 1 : 0;
    if (nlen > body)
        nlen = body;
    const char *old = (const char *)data + 1;
    const char *content = old + nlen;
    size_t clen = body - nlen;

    size_t on = 0, cn = 0;
    (void)ash_edit_normalize(&a, old, nlen, &on);
    (void)ash_edit_normalize(&a, content, clen, &cn);

    ash_edit_match m = ash_edit_find(&a, content, clen, old, nlen);
    if (m.found && m.haystack != NULL)
        (void)(m.index + m.len <= m.haystack_len);
    (void)ash_edit_count(&a, content, clen, old, nlen);

    ash_arena_destroy(&a);
    return 0;
}
