#ifndef ASH_APP_LOOP_H
#define ASH_APP_LOOP_H

#include "ash/ai/provider.h"
#include "ash/base/api.h"
#include "ash/base/status.h"

struct ash_provider_desc;
struct ash_auth;

typedef struct ash_loop_cfg {
    const struct ash_provider_desc *provider;
    const char *url;
    const char *api_key;
    const char *model;
    int         max_tokens;
    const char *system;
    const char *session_path;
    ash_oauth_token_fn oauth_token;
    void       *oauth_ctx;
    struct ash_auth *auth;
    ash_arena  *store_arena;
} ash_loop_cfg;

ASH_API ASH_WUR ash_status ash_loop_run(const ash_loop_cfg *cfg,
                                        int in_fd, int out_fd);

#endif
