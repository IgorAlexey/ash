#ifndef ASH_AI_HTTP_H
#define ASH_AI_HTTP_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/status.h"

typedef void (*ash_http_sink)(void *ud, const char *data, size_t len);

typedef struct ash_http ash_http;

ASH_API ASH_WUR ash_status ash_http_global_init(void);
ASH_API void ash_http_global_cleanup(void);

ASH_API ASH_WUR ash_status ash_http_start(ash_http **out, ash_arena *arena,
                                          const char *url,
                                          const char *body, size_t body_len,
                                          const char *const *headers,
                                          ash_http_sink sink, void *ud);

ASH_API ASH_WUR ash_status ash_http_perform(ash_http *h, int *running,
                                            long *http_status);

ASH_API ASH_WUR ash_status ash_http_wait(ash_http *h, int extra_fd,
                                         int timeout_ms, int *extra_readable);

ASH_API ASH_WUR ash_status ash_http_run(ash_http *h, long *http_status);

ASH_API void ash_http_close(ash_http *h);

#endif
