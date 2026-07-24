#ifndef ASH_AI_PROVIDER_H
#define ASH_AI_PROVIDER_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/status.h"

typedef enum ash_provider_kind {
    ASH_PROVIDER_ANTHROPIC_MESSAGES = 0,
    ASH_PROVIDER_OPENAI_CHAT,
} ash_provider_kind;

typedef enum ash_auth_style {
    ASH_AUTH_X_API_KEY = 0,
    ASH_AUTH_BEARER,
} ash_auth_style;

typedef struct ash_model_info {
    const char *id;
    int         context_window;
    int         max_output;
    double      price_in;
    double      price_out;
    double      price_cache_write;
    double      price_cache_read;
} ash_model_info;

typedef struct ash_provider_desc {
    const char           *name;
    ash_provider_kind     kind;
    const char           *base_url;
    const char           *env_key;
    ash_auth_style        auth;
    const char           *api_version;
    const char           *default_model;
    const ash_model_info *models;
    size_t                nmodels;
} ash_provider_desc;

ASH_API const ash_provider_desc *ash_provider_find(const char *name);
ASH_API const ash_provider_desc *ash_provider_default(void);
ASH_API const ash_provider_desc *ash_provider_autodetect(void);
ASH_API const char *ash_provider_env_key(const ash_provider_desc *desc);
ASH_API void ash_provider_scrub_env(void);
ASH_API const ash_model_info *ash_model_find(const char *model_id);

typedef const char *(*ash_key_source_fn)(void *ud, const char *provider);

ASH_API void ash_provider_set_key_source(ash_key_source_fn fn, void *ud);
ASH_API const char *ash_provider_api_key(const ash_provider_desc *desc);

typedef struct ash_ai_usage {
    int64_t input_tokens;
    int64_t output_tokens;
    int64_t cache_creation_input_tokens;
    int64_t cache_read_input_tokens;
} ash_ai_usage;

ASH_API void ash_ai_usage_add(ash_ai_usage *acc, const ash_ai_usage *u);
ASH_API double ash_model_cost_usd(const ash_model_info *mi,
                                  const ash_ai_usage *u);

typedef struct ash_msg {
    const char *role;
    const char *content;
    const char *tool_id;
    const char *tool_name;
    const char *tool_input;
    const char *tool_result;
    int         tool_is_error;
} ash_msg;

typedef const char *(*ash_oauth_token_fn)(void *ctx);

typedef struct ash_provider_cfg {
    const ash_provider_desc *provider;
    const char *url;
    const char *api_key;
    const char *model;
    int         max_tokens;
    const char *system;
    const char *tools;
    ash_oauth_token_fn oauth_token;
    void       *oauth_ctx;
} ash_provider_cfg;

typedef void (*ash_delta_sink)(void *ud, const char *text, size_t len);

ASH_API ASH_WUR ash_status ash_provider_build_body(ash_arena *a,
                                                   const ash_provider_cfg *cfg,
                                                   const ash_msg *msgs,
                                                   size_t nmsgs,
                                                   const char **out,
                                                   size_t *out_len);

ASH_API ASH_WUR ash_status ash_provider_turn(ash_arena *a,
                                             const ash_provider_cfg *cfg,
                                             const ash_msg *msgs, size_t nmsgs,
                                             ash_delta_sink on_text, void *ud,
                                             char *stop_reason, size_t stop_cap);

typedef struct ash_provider_stream ash_provider_stream;

ASH_API ASH_WUR ash_status ash_provider_start(ash_provider_stream **out,
                                              ash_arena *a,
                                              const ash_provider_cfg *cfg,
                                              const ash_msg *msgs, size_t nmsgs,
                                              ash_delta_sink on_text, void *ud,
                                              char *stop_reason, size_t stop_cap);

ASH_API ASH_WUR ash_status ash_provider_wait(ash_provider_stream *s,
                                             int extra_fd, int timeout_ms,
                                             int *extra_readable);

ASH_API ASH_WUR ash_status ash_provider_pump(ash_provider_stream *s, int *running);

ASH_API ASH_WUR ash_status ash_provider_finish(ash_provider_stream *s);

ASH_API ASH_WUR ash_status ash_provider_tool_use(ash_provider_stream *s,
                                                 const char **id,
                                                 const char **name,
                                                 const char **input,
                                                 size_t *input_len);

ASH_API ASH_WUR ash_status ash_provider_tool_at(ash_provider_stream *s, int i,
                                                const char **id,
                                                const char **name,
                                                const char **input,
                                                size_t *input_len);

ASH_API int ash_provider_tool_count(const ash_provider_stream *s);

ASH_API ASH_WUR ash_status ash_provider_usage(const ash_provider_stream *s,
                                              ash_ai_usage *out);

ASH_API void ash_provider_stream_close(ash_provider_stream *s);

#endif
