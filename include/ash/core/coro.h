#ifndef ASH_CORE_CORO_H
#define ASH_CORE_CORO_H

#include <stddef.h>
#include <ucontext.h>

#include "ash/base/api.h"
#include "ash/base/status.h"

typedef enum ash_wait {
    ASH_WAIT_NONE = 0,
    ASH_WAIT_INPUT,
    ASH_WAIT_SSE,
    ASH_WAIT_TOOL,
    ASH_WAIT_SAVE
} ash_wait;

typedef enum ash_co_state {
    ASH_CO_SUSPENDED = 0,
    ASH_CO_READY,
    ASH_CO_DEAD
} ash_co_state;

typedef struct ash_co ash_co;
typedef ash_status (*ash_co_fn)(ash_co *co, void *ud);

struct ash_co {
    ucontext_t   uctx;
    ucontext_t   ret;
    void        *stack;
    size_t       stack_size;
    ash_co_fn    fn;
    void        *ud;
    void        *sched_fake;
    void        *co_fake;
    const void  *from_bottom;
    size_t       from_size;
    ash_status   status;
    ash_co_state state;
    ash_wait     wait;
    _Bool        canceled;
};

ASH_API ASH_WUR ash_status ash_co_create(ash_co *co, void *stack,
                                         size_t stack_size,
                                         ash_co_fn fn, void *ud);
ASH_API ash_wait ash_co_resume(ash_co *co);
ASH_API void     ash_co_yield(ash_co *co, ash_wait reason);
ASH_API void     ash_co_cancel(ash_co *co);

#endif
