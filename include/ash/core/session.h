#ifndef ASH_CORE_SESSION_H
#define ASH_CORE_SESSION_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/slice.h"
#include "ash/base/status.h"

enum ash_rec_type {
    ASH_REC_HEADER = 0,
    ASH_REC_TURN   = 1,
    ASH_REC_MARK   = 2,
    ASH_REC_HEAD   = 3
};

enum ash_head_kind {
    ASH_HEAD_UNDO = 0,
    ASH_HEAD_REDO = 1
};

#define ASH_REC_MAGIC    0x52554154u
#define ASH_HDR_SIZE     32u
#define ASH_MAX_PAYLOAD  (64u * 1024u * 1024u)
#define ASH_SEQ_NONE     UINT64_MAX

typedef struct ash_span {
    size_t   off;
    size_t   payload_off;
    uint32_t payload_len;
    uint16_t type;
    uint16_t flags;
    uint64_t seq;
    uint64_t prev;
} ash_span;

ASH_API uint32_t ash_crc32c_pair(const void *a, size_t na, const void *b, size_t nb);

ASH_API ASH_WUR ash_status ash_log_scan(const uint8_t *buf, size_t len,
                                        ash_span *out, size_t cap,
                                        size_t *n_out, size_t *trunc_out);

typedef struct ash_log {
    ash_arena *arena;
    ash_buf    image;
    ash_span  *rec;
    size_t     n;
    size_t     cap;
    uint64_t   tip;
    uint64_t  *redo;
    size_t     redo_n;
    size_t     redo_cap;
    uint64_t   next_seq;
    int        fd;
} ash_log;

ASH_API ASH_WUR ash_status ash_log_open(ash_log *g, ash_arena *arena, const char *path);
ASH_API ASH_WUR ash_status ash_log_append_turn(ash_log *g, const void *payload, uint32_t len);
ASH_API ASH_WUR ash_status ash_log_clear(ash_log *g);
ASH_API ASH_WUR ash_status ash_log_compact(ash_log *g, uint64_t from, uint64_t to,
                                           const char *summary, uint32_t slen);
ASH_API ASH_WUR ash_status ash_log_undo(ash_log *g);
ASH_API ASH_WUR ash_status ash_log_redo(ash_log *g);
ASH_API uint64_t ash_log_tip_seq(const ash_log *g);
ASH_API void ash_log_close(ash_log *g);

#endif
