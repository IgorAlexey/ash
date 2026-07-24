#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ash/ai/http.h"
#include "ash/ai/provider.h"
#include "ash/app/loop.h"
#include "ash/app/rpc.h"
#include "ash/app/tokensrc.h"
#include "ash/base/arena.h"
#include "ash/core/auth.h"
#include "ash/core/config.h"
#include "ash/core/version.h"
#include "ash/term/screen.h"
#include "ash/term/signals.h"
#include "ash/base/poison.h"

int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--version") == 0) {
        printf("ash %s (provider %s)\n", ash_core_version(), ash_core_ai_version());
        return 0;
    }

    int want_rpc = 0;
    for (int i = 1; i < argc; i++)
        if (strcmp(argv[i], "--rpc") == 0)
            want_rpc = 1;

    ash_arena cfg_arena;
    if (ash_arena_create(&cfg_arena, "config", 1u << 16) != ASH_OK) {
        fprintf(stderr, "ash: %s\n", ash_errbuf);
        return 1;
    }
    ash_config conf;
    if (ash_config_load(&cfg_arena, &conf) != ASH_OK) {
        fprintf(stderr, "ash: %s\n", ash_errbuf);
        ash_arena_destroy(&cfg_arena);
        return 1;
    }

    const ash_provider_desc *desc = ash_provider_find(conf.provider.value);
    if (desc == NULL) {
        fprintf(stderr, "ash: unknown provider '%s'\n", conf.provider.value);
        ash_arena_destroy(&cfg_arena);
        return 2;
    }

    const char *key = conf.api_key.value;
    if (key == NULL && conf.provider.layer == ASH_CFG_DEFAULT) {
        const ash_provider_desc *detected = ash_provider_autodetect();
        if (detected != NULL) {
            desc = detected;
            key = ash_provider_env_key(desc);
        }
    }
    static ash_auth auth;
    static ash_token_src tsrc;
    int use_oauth = 0;
    if (key == NULL) {
        if (ash_auth_load(&cfg_arena, NULL, &auth) == ASH_OK) {
            ash_auth_bind(&auth);
            key = ash_provider_api_key(desc);
            if (key == NULL) {
                const ash_credential *c = ash_auth_get(&auth, desc->name);
                if (c != NULL && c->kind == ASH_CRED_OAUTH) {
                    tsrc.auth = &auth;
                    tsrc.provider = desc->name;
                    use_oauth = 1;
                }
            }
        }
    }
    if (key == NULL && !use_oauth)
        fprintf(stderr,
                "ash: no API key found; set %s to talk to %s "
                "(run /login for a Claude subscription, or set a key)\n",
                desc->env_key, desc->name);

    const char *model = conf.model.layer == ASH_CFG_DEFAULT
                            ? desc->default_model : conf.model.value;
    const char *url = conf.base_url.layer == ASH_CFG_DEFAULT
                          ? desc->base_url : conf.base_url.value;

    if (want_rpc) {
        if (ash_http_global_init() != ASH_OK) {
            fprintf(stderr, "ash: %s\n", ash_errbuf);
            ash_arena_destroy(&cfg_arena);
            return 1;
        }
        ash_loop_cfg rcfg = {
            .provider = desc,
            .url = url,
            .api_key = key,
            .model = model,
            .max_tokens = 4096,
            .system = conf.system.value,
            .session_path = NULL,
            .oauth_token = use_oauth ? ash_token_src_get : NULL,
            .oauth_ctx = use_oauth ? &tsrc : NULL,
            .auth = &auth,
            .store_arena = &cfg_arena,
        };
        ash_status rst = ash_rpc_run(&rcfg, STDIN_FILENO, STDOUT_FILENO);
        ash_http_global_cleanup();
        ash_arena_destroy(&cfg_arena);
        return rst == ASH_OK ? 0 : 1;
    }

    if (!isatty(STDIN_FILENO)) {
        fprintf(stderr, "ash: stdin is not a terminal\n");
        ash_arena_destroy(&cfg_arena);
        return 3;
    }

    if (ash_http_global_init() != ASH_OK) {
        fprintf(stderr, "ash: %s\n", ash_errbuf);
        ash_arena_destroy(&cfg_arena);
        return 1;
    }
    if (ash_screen_init(STDIN_FILENO) != ASH_OK) {
        fprintf(stderr, "ash: %s\n", ash_errbuf);
        ash_http_global_cleanup();
        ash_arena_destroy(&cfg_arena);
        return 1;
    }
    if (ash_signals_init() != ASH_OK) {
        ash_screen_shutdown();
        ash_http_global_cleanup();
        fprintf(stderr, "ash: %s\n", ash_errbuf);
        ash_arena_destroy(&cfg_arena);
        return 1;
    }

    char session_buf[512];
    const char *session = NULL;
    const char *env_session = getenv("ASH_SESSION");
    const char *home = getenv("HOME");
    if (env_session != NULL) {
        int sn = snprintf(session_buf, sizeof session_buf, "%s", env_session);
        if (sn > 0 && (size_t)sn < sizeof session_buf)
            session = session_buf;
    } else if (home != NULL) {
        int sn = snprintf(session_buf, sizeof session_buf, "%s/.ash-session", home);
        if (sn > 0 && (size_t)sn < sizeof session_buf)
            session = session_buf;
    }

    ash_loop_cfg cfg = {
        .provider = desc,
        .url = url,
        .api_key = key,
        .model = model,
        .max_tokens = 4096,
        .system = conf.system.value,
        .session_path = session,
        .oauth_token = use_oauth ? ash_token_src_get : NULL,
        .oauth_ctx = use_oauth ? &tsrc : NULL,
        .auth = &auth,
        .store_arena = &cfg_arena,
    };
    ash_status st = ash_loop_run(&cfg, ash_screen_fd(), ash_screen_fd());

    ash_screen_shutdown();
    ash_http_global_cleanup();
    ash_arena_destroy(&cfg_arena);
    return st == ASH_OK ? 0 : 1;
}
