#ifndef ASH_CORE_OAUTH_H
#define ASH_CORE_OAUTH_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/slice.h"
#include "ash/base/status.h"

#define ASH_OAUTH_CLIENT_ID     "9d1c250a-e61b-44d9-88ed-5944d1962f5e"
#define ASH_OAUTH_AUTHORIZE_URL "https://claude.ai/oauth/authorize"
#define ASH_OAUTH_TOKEN_URL     "https://console.anthropic.com/v1/oauth/token"
#define ASH_OAUTH_REDIRECT_URI  "https://console.anthropic.com/oauth/code/callback"
#define ASH_OAUTH_SCOPE         "org:create_api_key user:profile user:inference"
#define ASH_OAUTH_BETA          "oauth-2025-04-20"

typedef struct ash_oauth_http {
    ash_status (*post_json)(void *ctx, ash_arena *a, const char *url,
                            const char *body, size_t body_len,
                            long *http_status, ash_slice *resp);
    void *ctx;
} ash_oauth_http;

typedef struct ash_oauth_pkce {
    const char *verifier;
    const char *challenge;
    const char *state;
    const char *url;
} ash_oauth_pkce;

typedef struct ash_oauth_token {
    const char *access;
    const char *refresh;
    int64_t     expires;
    const char *account;
} ash_oauth_token;

ASH_API ASH_WUR ash_status ash_oauth_pkce_begin(ash_arena *a,
                                                ash_oauth_pkce *out);

ASH_API ASH_WUR ash_status ash_oauth_exchange(const ash_oauth_http *http,
                                              ash_arena *a,
                                              const char *verifier,
                                              const char *code,
                                              const char *state,
                                              ash_oauth_token *out);

ASH_API ASH_WUR ash_status ash_oauth_refresh(const ash_oauth_http *http,
                                             ash_arena *a,
                                             const char *refresh_token,
                                             ash_oauth_token *out);

ASH_API int ash_oauth_needs_refresh(int64_t expires, int64_t now);

ASH_API int ash_oauth_state_ok(const char *got, const char *want);

ASH_API ash_oauth_http ash_oauth_default_http(void);

#endif
