#include <stddef.h>
#include <stdint.h>

#include "ash/base/arena.h"
#include "ash/base/json.h"

static void walk(const ash_json *v)
{
    switch (v->type) {
    case ASH_JSON_NULL:
    case ASH_JSON_BOOL:
        return;
    case ASH_JSON_NUMBER: {
        int64_t n;
        (void)ash_json_int64(v, &n);
        return;
    }
    case ASH_JSON_STRING: {
        ash_slice sl;
        (void)ash_json_str(v, &sl);
        return;
    }
    case ASH_JSON_ARRAY:
        for (size_t i = 0; i < v->u.arr.n; i++)
            walk(&v->u.arr.v[i]);
        return;
    case ASH_JSON_OBJECT:
        for (size_t i = 0; i < v->u.obj.n; i++)
            walk(&v->u.obj.v[i].val);
        return;
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    ash_arena a;
    if (ash_arena_create(&a, "fuzz", 1u << 16) != ASH_OK)
        return 0;
    ash_json v;
    if (ash_json_parse(&a, (const char *)data, size, &v) == ASH_OK)
        walk(&v);
    ash_arena_destroy(&a);
    return 0;
}
