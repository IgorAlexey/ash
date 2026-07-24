#ifndef ASH_TERM_SIGNALS_H
#define ASH_TERM_SIGNALS_H

#include "ash/base/api.h"
#include "ash/base/status.h"

ASH_API ASH_WUR ash_status ash_signals_init(void);
ASH_API int  ash_signal_pending(void);
ASH_API void ash_signal_clear(void);

#endif
