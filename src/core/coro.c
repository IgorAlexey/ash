#include <ucontext.h>

#include "ash/core/coro.h"
#include "ash/base/poison.h"

#if defined(__SANITIZE_ADDRESS__)
#  define ASH_ASAN 1
#elif defined(__has_feature)
#  if __has_feature(address_sanitizer)
#    define ASH_ASAN 1
#  endif
#endif

#ifdef ASH_ASAN
#  include <sanitizer/common_interface_defs.h>
#  define FIBER_START(save, bottom, size) \
       __sanitizer_start_switch_fiber((save), (bottom), (size))
#  define FIBER_FINISH(save, pbottom, psize) \
       __sanitizer_finish_switch_fiber((save), (pbottom), (psize))
#else
#  define FIBER_START(save, bottom, size) \
       ((void)(save), (void)(bottom), (void)(size))
#  define FIBER_FINISH(save, pbottom, psize) \
       ((void)(save), (void)(pbottom), (void)(psize))
#endif

static _Thread_local ash_co *g_co_starting;

static void ash_co_trampoline(void)
{
    ash_co *co = g_co_starting;
    FIBER_FINISH(co->co_fake, &co->from_bottom, &co->from_size);
    co->status = co->fn(co, co->ud);
    co->state = ASH_CO_DEAD;
    co->wait = ASH_WAIT_NONE;
    FIBER_START(NULL, co->from_bottom, co->from_size);
    swapcontext(&co->uctx, &co->ret);
}

ash_status ash_co_create(ash_co *co, void *stack, size_t stack_size,
                         ash_co_fn fn, void *ud)
{
    if (getcontext(&co->uctx) != 0)
        return ash_fail(ASH_ERR_OS, "getcontext failed");
    co->uctx.uc_stack.ss_sp = stack;
    co->uctx.uc_stack.ss_size = stack_size;
    co->uctx.uc_link = NULL;
    co->stack = stack;
    co->stack_size = stack_size;
    co->fn = fn;
    co->ud = ud;
    co->sched_fake = NULL;
    co->co_fake = NULL;
    co->from_bottom = NULL;
    co->from_size = 0;
    co->status = ASH_OK;
    co->state = ASH_CO_SUSPENDED;
    co->wait = ASH_WAIT_INPUT;
    co->canceled = 0;
    makecontext(&co->uctx, ash_co_trampoline, 0);
    return ASH_OK;
}

ash_wait ash_co_resume(ash_co *co)
{
    if (co->state == ASH_CO_DEAD)
        return ASH_WAIT_NONE;
    g_co_starting = co;
    FIBER_START(&co->sched_fake, co->stack, co->stack_size);
    swapcontext(&co->ret, &co->uctx);
    FIBER_FINISH(co->sched_fake, NULL, NULL);
    return co->wait;
}

void ash_co_yield(ash_co *co, ash_wait reason)
{
    co->wait = reason;
    co->state = ASH_CO_SUSPENDED;
    FIBER_START(&co->co_fake, co->from_bottom, co->from_size);
    swapcontext(&co->uctx, &co->ret);
    FIBER_FINISH(co->co_fake, &co->from_bottom, &co->from_size);
    co->state = ASH_CO_READY;
}

void ash_co_cancel(ash_co *co)
{
    co->canceled = 1;
}
