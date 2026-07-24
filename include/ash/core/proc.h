#ifndef ASH_CORE_PROC_H
#define ASH_CORE_PROC_H

#include <sys/types.h>

#include "ash/base/api.h"
#include "ash/base/status.h"

typedef struct ash_proc {
    pid_t pid;
    int   pidfd;
    int   out;
} ash_proc;

ASH_API ASH_WUR ash_status ash_proc_spawn(ash_proc *p, const char *const argv[]);
ASH_API int  ash_proc_out_fd(const ash_proc *p);
ASH_API int  ash_proc_pidfd(const ash_proc *p);
ASH_API ASH_WUR ash_status ash_proc_wait(ash_proc *p, int *exit_code);
ASH_API void ash_proc_close(ash_proc *p);

#endif
