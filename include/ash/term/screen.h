#ifndef ASH_TERM_SCREEN_H
#define ASH_TERM_SCREEN_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/status.h"

ASH_API ASH_WUR ash_status ash_screen_init(int fd);
ASH_API void ash_screen_shutdown(void);
ASH_API void ash_screen_emergency_restore(void);
ASH_API int  ash_screen_fd(void);

ASH_API void ash_screen_frame_begin(void);
ASH_API void ash_screen_frame_end(void);
ASH_API void ash_screen_write(const void *p, size_t n);
ASH_API void ash_screen_clipboard(const void *b64, size_t n);
ASH_API void ash_screen_finish(int row);

#endif
