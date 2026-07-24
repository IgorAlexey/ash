#include <time.h>

#include "ash/app/tokensrc.h"
#include "ash/base/arena.h"
#include "ash/core/auth.h"
#include "ash/core/oauth.h"
#include "ash/base/poison.h"

const char *ash_token_src_get(void *ctx)
{
    ash_token_src *ts = ctx;
    if (ts == NULL || ts->auth == NULL || ts->provider == NULL)
        return NULL;

    const ash_credential *c = ash_auth_get(ts->auth, ts->provider);
    if (c == NULL || c->kind != ASH_CRED_OAUTH)
        return NULL;
    if (c->oauth.access != NULL &&
        !ash_oauth_needs_refresh(c->oauth.expires, (int64_t)time(NULL)))
        return c->oauth.access;
    if (c->oauth.refresh == NULL)
        return c->oauth.access;

    ash_arena ra;
    if (ash_arena_create(&ra, "oauth-refresh", 1u << 16) != ASH_OK)
        return NULL;
    ash_oauth_http http = ash_oauth_default_http();
    ash_oauth_token tok;
    ash_status st = ash_oauth_refresh(&http, &ra, c->oauth.refresh, &tok);
    if (st != ASH_OK) {
        ash_arena_destroy(&ra);
        return NULL;
    }

    ash_oauth stored = {
        .access = tok.access,
        .refresh = tok.refresh != NULL ? tok.refresh : c->oauth.refresh,
        .expires = tok.expires,
    };
    st = ash_auth_set_oauth(ts->auth, ts->provider, &stored);
    ash_arena_destroy(&ra);
    if (st != ASH_OK)
        return NULL;

    const ash_credential *nc = ash_auth_get(ts->auth, ts->provider);
    return nc != NULL ? nc->oauth.access : NULL;
}
