#ifndef ASH_AI_SSE_H
#define ASH_AI_SSE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/slice.h"
#include "ash/base/status.h"

#ifndef ASH_SSE_MAX_LINE
#define ASH_SSE_MAX_LINE (1u << 20)
#endif
#ifndef ASH_SSE_MAX_DATA
#define ASH_SSE_MAX_DATA (16u << 20)
#endif

_Static_assert(ASH_SSE_MAX_LINE < ASH_SSE_MAX_DATA,
               "ASH_SSE_MAX_LINE must stay below ASH_SSE_MAX_DATA; the data "
               "cap check subtracts a line length from the data cap");

typedef struct ash_sse_event {
    ash_slice type;
    ash_slice data;
} ash_sse_event;

typedef void (*ash_sse_emit)(void *ud, const ash_sse_event *ev);

typedef struct ash_sse_parser {
    ash_buf  line;
    ash_buf  data;
    ash_buf  type;
    uint8_t  prefix[3];
    uint8_t  prefix_len;
    bool     bom_done;
    bool     saw_data;
    bool     cr;
    bool     dead;
} ash_sse_parser;

ASH_API void ash_sse_init(ash_sse_parser *p, ash_arena *arena);

ASH_API ASH_WUR ash_status ash_sse_feed(ash_sse_parser *p, const void *bytes,
                                        size_t n, ash_sse_emit emit, void *ud);

#endif
