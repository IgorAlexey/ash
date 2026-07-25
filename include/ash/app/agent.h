#ifndef ASH_APP_AGENT_H
#define ASH_APP_AGENT_H

#include <stddef.h>

#include "ash/ai/provider.h"
#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"

enum { ASH_AGENT_TOOL_OUT_CAP = 1 << 20 };

#define ASH_AGENT_TRUNC_MARK "\n[output truncated]"

typedef enum ash_agent_ev_kind {
    ASH_AGENT_TURN_START = 0,
    ASH_AGENT_MSG_START,
    ASH_AGENT_TEXT,
    ASH_AGENT_MSG_END,
    ASH_AGENT_USAGE,
    ASH_AGENT_MSG_APPEND,
    ASH_AGENT_TOOL_START,
    ASH_AGENT_TOOL_END,
    ASH_AGENT_ERROR,
    ASH_AGENT_TURN_END
} ash_agent_ev_kind;

typedef struct ash_agent_event {
    ash_agent_ev_kind   kind;
    const char         *id;
    const char         *name;
    const char         *text;
    size_t              len;
    int                 is_error;
    const ash_msg      *msg;
    const ash_ai_usage *usage;
} ash_agent_event;

typedef struct ash_agent_host {
    void *ud;
    void (*emit)(void *ud, const ash_agent_event *ev);
    int  (*pump)(void *ud, ash_provider_stream *s);
    int  (*shell)(void *ud, const char *cmd, ash_buf *out);
} ash_agent_host;

typedef enum ash_agent_outcome {
    ASH_AGENT_DONE = 0,
    ASH_AGENT_ABORTED,
    ASH_AGENT_FAILED
} ash_agent_outcome;

typedef struct ash_agent {
    ash_mem                *mem;
    const ash_provider_cfg *cfg;
    ash_agent_host          host;
    ash_msg                *msgs;
    size_t                  nmsgs;
    size_t                  msgcap;
    ash_buf                 resp;
    char                    stop[32];
    int                     canceled;
} ash_agent;

ASH_API void ash_agent_init(ash_agent *a, ash_mem *mem,
                            const ash_provider_cfg *cfg,
                            const ash_agent_host *host);
ASH_API void ash_agent_reset(ash_agent *a);
ASH_API void ash_agent_user(ash_agent *a, const char *text, size_t len);
ASH_API ASH_WUR ash_agent_outcome ash_agent_run(ash_agent *a);
ASH_API size_t ash_agent_msg_count(const ash_agent *a);
ASH_API const ash_msg *ash_agent_msg_at(const ash_agent *a, size_t i);

#endif
