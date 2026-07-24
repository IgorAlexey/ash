#ifndef ASH_BASE_STATUS_H
#define ASH_BASE_STATUS_H

#include "ash/base/api.h"

typedef enum ash_status {
    ASH_OK = 0,
    ASH_ERR_NOMEM,
    ASH_ERR_IO,
    ASH_ERR_EOF,
    ASH_ERR_PARSE,
    ASH_ERR_PROTOCOL,
    ASH_ERR_RANGE,
    ASH_ERR_STATE,
    ASH_ERR_UNSUPPORTED,
    ASH_ERR_INTERRUPTED,
    ASH_ERR_TIMEOUT,
    ASH_ERR_NOTFOUND,
    ASH_ERR_NOSPACE,
    ASH_ERR_OS,
    ASH_STATUS_COUNT
} ash_status;

ASH_API const char *ash_status_str(ash_status st);

#define ASH_ERRBUF_CAP 256
ASH_API extern _Thread_local char ash_errbuf[ASH_ERRBUF_CAP];

ASH_API ASH_WUR ASH_PRINTF(2, 3)
ash_status ash_fail(ash_status st, const char *fmt, ...);

ASH_API ASH_NORETURN ASH_PRINTF(1, 2)
void ash_die(const char *fmt, ...);

#define ASH_TRY(expr)                     \
    do {                                  \
        ash_status ash_try_st_ = (expr);  \
        if (ash_try_st_ != ASH_OK)        \
            return ash_try_st_;           \
    } while (0)

#endif
