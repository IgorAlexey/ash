#ifndef ASH_CORE_AUTH_H
#define ASH_CORE_AUTH_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/status.h"

typedef enum ash_cred_kind {
    ASH_CRED_NONE = 0,
    ASH_CRED_API_KEY,
    ASH_CRED_OAUTH
} ash_cred_kind;

typedef struct ash_oauth {
    const char *access;
    const char *refresh;
    int64_t     expires;
} ash_oauth;

typedef struct ash_credential {
    const char   *provider;
    ash_cred_kind kind;
    const char   *api_key;
    ash_oauth     oauth;
} ash_credential;

typedef struct ash_auth {
    ash_arena      *arena;
    const char     *path;
    ash_credential *creds;
    size_t          count;
    int             warnings;
} ash_auth;

ASH_API ASH_WUR ash_status ash_auth_load(ash_arena *a, const char *path,
                                         ash_auth *out);

ASH_API const ash_credential *ash_auth_get(const ash_auth *s,
                                           const char *provider);

ASH_API const char *ash_auth_api_key(const ash_auth *s, const char *provider);

ASH_API ASH_WUR ash_status ash_auth_set_api_key(ash_auth *s,
                                                const char *provider,
                                                const char *key);

ASH_API ASH_WUR ash_status ash_auth_set_oauth(ash_auth *s, const char *provider,
                                              const ash_oauth *tok);

ASH_API ASH_WUR ash_status ash_auth_delete(ash_auth *s, const char *provider);

ASH_API void ash_auth_bind(const ash_auth *s);

#endif
