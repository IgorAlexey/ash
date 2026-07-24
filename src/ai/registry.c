#include <stdlib.h>
#include <string.h>

#include "ash/ai/provider.h"
#include "ash/base/poison.h"

static const ash_model_info ANTHROPIC_MODELS[] = {
    { "claude-3-5-sonnet-20241022", 200000, 8192, 3.00, 15.00, 3.75, 0.30 },
    { "claude-3-5-haiku-20241022",  200000, 8192, 0.80,  4.00, 1.00, 0.08 },
    { "claude-3-opus-20240229",     200000, 4096, 15.00, 75.00, 18.75, 1.50 },
};

static const ash_model_info DEEPSEEK_MODELS[] = {
    { "deepseek-chat",     65536, 8192, 0.27, 1.10, 0.00, 0.07 },
    { "deepseek-reasoner", 65536, 8192, 0.55, 2.19, 0.00, 0.14 },
};

static const ash_model_info OPENAI_MODELS[] = {
    { "gpt-4o",      128000, 16384, 2.50, 10.00, 0.00, 1.25 },
    { "gpt-4o-mini", 128000, 16384, 0.15,  0.60, 0.00, 0.075 },
    { "gpt-4.1",    1047576, 32768, 2.00,  8.00, 0.00, 0.50 },
};

static const ash_provider_desc PROVIDERS[] = {
    {
        .name = "anthropic",
        .kind = ASH_PROVIDER_ANTHROPIC_MESSAGES,
        .base_url = "https://api.anthropic.com/v1/messages",
        .env_key = "ANTHROPIC_API_KEY",
        .auth = ASH_AUTH_X_API_KEY,
        .api_version = "2023-06-01",
        .default_model = "claude-3-5-sonnet-20241022",
        .models = ANTHROPIC_MODELS,
        .nmodels = sizeof ANTHROPIC_MODELS / sizeof ANTHROPIC_MODELS[0],
    },
    {
        .name = "deepseek",
        .kind = ASH_PROVIDER_OPENAI_CHAT,
        .base_url = "https://api.deepseek.com/chat/completions",
        .env_key = "DEEPSEEK_API_KEY",
        .auth = ASH_AUTH_BEARER,
        .api_version = NULL,
        .default_model = "deepseek-chat",
        .models = DEEPSEEK_MODELS,
        .nmodels = sizeof DEEPSEEK_MODELS / sizeof DEEPSEEK_MODELS[0],
    },
    {
        .name = "openai",
        .kind = ASH_PROVIDER_OPENAI_CHAT,
        .base_url = "https://api.openai.com/v1/chat/completions",
        .env_key = "OPENAI_API_KEY",
        .auth = ASH_AUTH_BEARER,
        .api_version = NULL,
        .default_model = "gpt-4o",
        .models = OPENAI_MODELS,
        .nmodels = sizeof OPENAI_MODELS / sizeof OPENAI_MODELS[0],
    },
};

const ash_provider_desc *ash_provider_find(const char *name)
{
    if (name == NULL)
        return NULL;
    for (size_t i = 0; i < sizeof PROVIDERS / sizeof PROVIDERS[0]; i++)
        if (strcmp(PROVIDERS[i].name, name) == 0)
            return &PROVIDERS[i];
    return NULL;
}

const ash_provider_desc *ash_provider_default(void)
{
    return &PROVIDERS[0];
}

const ash_provider_desc *ash_provider_autodetect(void)
{
    for (size_t i = 0; i < sizeof PROVIDERS / sizeof PROVIDERS[0]; i++) {
        const char *k = ash_provider_env_key(&PROVIDERS[i]);
        if (k != NULL && k[0] != '\0')
            return &PROVIDERS[i];
    }
    return NULL;
}

const char *ash_provider_env_key(const ash_provider_desc *desc)
{
    if (desc == NULL || desc->env_key == NULL)
        return NULL;
    return getenv(desc->env_key);
}

static ash_key_source_fn g_key_source;
static void *g_key_source_ud;

void ash_provider_set_key_source(ash_key_source_fn fn, void *ud)
{
    g_key_source = fn;
    g_key_source_ud = ud;
}

const char *ash_provider_api_key(const ash_provider_desc *desc)
{
    if (desc == NULL)
        return NULL;
    const char *env = ash_provider_env_key(desc);
    if (env != NULL && env[0] != '\0')
        return env;
    if (g_key_source != NULL)
        return g_key_source(g_key_source_ud, desc->name);
    return NULL;
}

void ash_provider_scrub_env(void)
{
    for (size_t i = 0; i < sizeof PROVIDERS / sizeof PROVIDERS[0]; i++)
        if (PROVIDERS[i].env_key != NULL)
            unsetenv(PROVIDERS[i].env_key);
}

const ash_model_info *ash_model_find(const char *model_id)
{
    if (model_id == NULL)
        return NULL;
    for (size_t i = 0; i < sizeof PROVIDERS / sizeof PROVIDERS[0]; i++) {
        const ash_provider_desc *p = &PROVIDERS[i];
        for (size_t j = 0; j < p->nmodels; j++)
            if (strcmp(p->models[j].id, model_id) == 0)
                return &p->models[j];
    }
    return NULL;
}

void ash_ai_usage_add(ash_ai_usage *acc, const ash_ai_usage *u)
{
    if (acc == NULL || u == NULL)
        return;
    acc->input_tokens += u->input_tokens;
    acc->output_tokens += u->output_tokens;
    acc->cache_creation_input_tokens += u->cache_creation_input_tokens;
    acc->cache_read_input_tokens += u->cache_read_input_tokens;
}

double ash_model_cost_usd(const ash_model_info *mi, const ash_ai_usage *u)
{
    if (mi == NULL || u == NULL)
        return 0.0;
    double m = 1.0 / 1000000.0;
    return (double)u->input_tokens * mi->price_in * m +
           (double)u->output_tokens * mi->price_out * m +
           (double)u->cache_creation_input_tokens * mi->price_cache_write * m +
           (double)u->cache_read_input_tokens * mi->price_cache_read * m;
}
