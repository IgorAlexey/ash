#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "ash/base/status.h"
#include "ash/base/poison.h"

_Thread_local char ash_errbuf[ASH_ERRBUF_CAP];

const char *ash_status_str(ash_status st)
{
    switch (st) {
    case ASH_OK:              return "ok";
    case ASH_ERR_NOMEM:       return "out of memory";
    case ASH_ERR_IO:          return "I/O error";
    case ASH_ERR_EOF:         return "unexpected end of input";
    case ASH_ERR_PARSE:       return "parse error";
    case ASH_ERR_PROTOCOL:    return "protocol error";
    case ASH_ERR_RANGE:       return "value out of range";
    case ASH_ERR_STATE:       return "invalid state";
    case ASH_ERR_UNSUPPORTED: return "unsupported";
    case ASH_ERR_INTERRUPTED: return "interrupted";
    case ASH_ERR_TIMEOUT:     return "timed out";
    case ASH_ERR_NOTFOUND:    return "not found";
    case ASH_ERR_NOSPACE:     return "no space left";
    case ASH_ERR_OS:          return "operating system error";
    case ASH_STATUS_COUNT:    break;
    }
    return "unknown status";
}

ash_status ash_fail(ash_status st, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(ash_errbuf, sizeof ash_errbuf, fmt, ap);
    va_end(ap);
    if (n < 0)
        ash_errbuf[0] = '\0';
    return st;
}

void ash_die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("ash: fatal: ", stderr);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fputc('\n', stderr);
    abort();
}
