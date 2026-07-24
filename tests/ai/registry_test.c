#define _POSIX_C_SOURCE 200809L

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ash/ai/provider.h"
#include "ash_test.h"

int main(void)
{
    const ash_provider_desc *anth = ash_provider_find("anthropic");
    const ash_provider_desc *deep = ash_provider_find("deepseek");
    const ash_provider_desc *oai = ash_provider_find("openai");
    ASH_CHECK(anth != NULL && deep != NULL && oai != NULL);
    ASH_CHECK(ash_provider_find("nope") == NULL);
    ASH_CHECK(ash_provider_default() == anth);

    ASH_CHECK(anth->kind == ASH_PROVIDER_ANTHROPIC_MESSAGES);
    ASH_CHECK(anth->auth == ASH_AUTH_X_API_KEY);
    ASH_CHECK(anth->api_version != NULL &&
              strcmp(anth->api_version, "2023-06-01") == 0);
    ASH_CHECK(strcmp(anth->env_key, "ANTHROPIC_API_KEY") == 0);
    ASH_CHECK(strstr(anth->base_url, "api.anthropic.com") != NULL);

    ASH_CHECK(deep->kind == ASH_PROVIDER_OPENAI_CHAT);
    ASH_CHECK(deep->auth == ASH_AUTH_BEARER && deep->api_version == NULL);
    ASH_CHECK(strcmp(deep->env_key, "DEEPSEEK_API_KEY") == 0);
    ASH_CHECK(strcmp(deep->base_url, "https://api.deepseek.com/chat/completions") == 0);

    ASH_CHECK(oai->kind == ASH_PROVIDER_OPENAI_CHAT);
    ASH_CHECK(strcmp(oai->env_key, "OPENAI_API_KEY") == 0);

    ash_provider_scrub_env();
    ASH_CHECK(ash_provider_autodetect() == NULL);
    ASH_CHECK(setenv("OPENAI_API_KEY", "k", 1) == 0);
    ASH_CHECK(ash_provider_autodetect() == oai);
    ASH_CHECK(setenv("DEEPSEEK_API_KEY", "k", 1) == 0);
    ASH_CHECK(ash_provider_autodetect() == deep);
    ASH_CHECK(setenv("ANTHROPIC_API_KEY", "k", 1) == 0);
    ASH_CHECK(ash_provider_autodetect() == anth);
    ASH_CHECK(setenv("ANTHROPIC_API_KEY", "", 1) == 0);
    ASH_CHECK(ash_provider_autodetect() == deep);
    ash_provider_scrub_env();

    const ash_model_info *m = ash_model_find("claude-3-5-sonnet-20241022");
    ASH_CHECK(m != NULL && m->context_window == 200000);
    ASH_CHECK(ash_model_find("deepseek-chat") != NULL);
    ASH_CHECK(ash_model_find("gpt-4o") != NULL);
    ASH_CHECK(ash_model_find("no-such-model") == NULL);

    ash_ai_usage total = { 0 };
    ash_ai_usage a = { .input_tokens = 100, .output_tokens = 50,
                       .cache_creation_input_tokens = 10,
                       .cache_read_input_tokens = 5 };
    ash_ai_usage b = { .input_tokens = 200, .output_tokens = 25 };
    ash_ai_usage_add(&total, &a);
    ash_ai_usage_add(&total, &b);
    ASH_CHECK(total.input_tokens == 300 && total.output_tokens == 75 &&
              total.cache_creation_input_tokens == 10 &&
              total.cache_read_input_tokens == 5);

    ash_ai_usage u = { .input_tokens = 1000000, .output_tokens = 1000000 };
    double cost = ash_model_cost_usd(m, &u);
    ASH_CHECK(fabs(cost - (m->price_in + m->price_out)) < 1e-9);
    ASH_CHECK(ash_model_cost_usd(NULL, &u) == 0.0);

    return ash_test_done();
}
