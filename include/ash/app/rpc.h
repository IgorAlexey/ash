#ifndef ASH_APP_RPC_H
#define ASH_APP_RPC_H

#include <stddef.h>

#include "ash/app/loop.h"
#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/slice.h"
#include "ash/base/status.h"

typedef enum ash_rpc_kind {
    ASH_RPC_UNKNOWN = 0,
    ASH_RPC_PROMPT,
    ASH_RPC_ABORT,
    ASH_RPC_GET_STATE,
    ASH_RPC_GET_MESSAGES,
    ASH_RPC_NEW_SESSION
} ash_rpc_kind;

typedef struct ash_rpc_cmd {
    ash_rpc_kind kind;
    ash_slice    id;
    ash_slice    type;
    ash_slice    message;
} ash_rpc_cmd;

ASH_API ASH_WUR ash_status ash_rpc_parse(ash_arena *a, const char *line,
                                         size_t len, ash_rpc_cmd *out);

ASH_API ASH_WUR ash_status ash_rpc_run(const ash_loop_cfg *cfg,
                                       int in_fd, int out_fd);

#endif
