#ifndef ASH_APP_TOKENSRC_H
#define ASH_APP_TOKENSRC_H

#include "ash/base/api.h"

struct ash_auth;

typedef struct ash_token_src {
    struct ash_auth *auth;
    const char      *provider;
} ash_token_src;

ASH_API const char *ash_token_src_get(void *ctx);

#endif
