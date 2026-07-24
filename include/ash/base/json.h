#ifndef ASH_BASE_JSON_H
#define ASH_BASE_JSON_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/slice.h"
#include "ash/base/status.h"

#ifndef ASH_JSON_MAX_INPUT
#define ASH_JSON_MAX_INPUT (1u << 20)
#endif

typedef enum ash_json_type {
    ASH_JSON_NULL,
    ASH_JSON_BOOL,
    ASH_JSON_NUMBER,
    ASH_JSON_STRING,
    ASH_JSON_ARRAY,
    ASH_JSON_OBJECT
} ash_json_type;

typedef struct ash_json ash_json;
typedef struct ash_json_member ash_json_member;

struct ash_json {
    ash_json_type type;
    union {
        int boolean;
        struct { const char *p; size_t n; } num;
        struct { const char *p; size_t n; } str;
        struct { ash_json *v; size_t n; } arr;
        struct { ash_json_member *v; size_t n; } obj;
    } u;
};

struct ash_json_member {
    const char *key;
    size_t      klen;
    ash_json    val;
};

_Static_assert(sizeof(ash_json) <= 24,
               "ash_json exceeds 24 bytes: worst-case arena use is about 50x "
               "the input (doubling growth leaves up to 4N live 24-byte slots "
               "for N nodes at ~2 input bytes each, plus number-token copies), "
               "so at the 1 MiB input cap a parse peaks near 50 MiB; a fatter "
               "node moves that cliff and ASH_JSON_MAX_INPUT must be revisited");

ASH_API ASH_WUR ash_status ash_json_parse(ash_arena *a, const char *buf,
                                          size_t len, ash_json *out);

ASH_API const ash_json *ash_json_get(const ash_json *v, const char *key);
ASH_API ASH_WUR ash_status ash_json_str(const ash_json *v, ash_slice *out);
ASH_API ASH_WUR ash_status ash_json_int64(const ash_json *v, int64_t *out);

#endif
