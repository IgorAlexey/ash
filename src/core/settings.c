#include <string.h>

#include "ash/core/config.h"
#include "ash/core/settings.h"
#include "ash/base/poison.h"

static const char *const provider_opts[] = {
    "anthropic", "deepseek", "openai"
};

static const char *const thinking_opts[] = {
    "off", "minimal", "low", "medium", "high", "xhigh", "max"
};

static const ash_setting schema[] = {
    { "provider", "Provider", ASH_SETTING_ENUM,
      provider_opts, sizeof provider_opts / sizeof provider_opts[0] },
    { "model", "Model", ASH_SETTING_TEXT, NULL, 0 },
    { "thinking_level", "Thinking", ASH_SETTING_ENUM,
      thinking_opts, sizeof thinking_opts / sizeof thinking_opts[0] },
    { "theme", "Theme", ASH_SETTING_TEXT, NULL, 0 },
    { "system", "System prompt", ASH_SETTING_TEXT, NULL, 0 },
};

const ash_setting *ash_settings_schema(size_t *count)
{
    if (count != NULL)
        *count = sizeof schema / sizeof schema[0];
    return schema;
}

static const ash_cfg_field *field_for(const ash_config *cfg, const ash_setting *s)
{
    if (strcmp(s->key, "provider") == 0) return &cfg->provider;
    if (strcmp(s->key, "model") == 0)    return &cfg->model;
    if (strcmp(s->key, "theme") == 0)    return &cfg->theme;
    if (strcmp(s->key, "system") == 0)   return &cfg->system;
    return NULL;
}

const char *ash_settings_value(const ash_config *cfg, const ash_setting *s)
{
    if (cfg == NULL || s == NULL)
        return NULL;
    if (strcmp(s->key, "thinking_level") == 0)
        return ash_thinking_str(cfg->thinking);
    const ash_cfg_field *f = field_for(cfg, s);
    return f != NULL ? f->value : NULL;
}

ash_config_layer ash_settings_layer(const ash_config *cfg, const ash_setting *s)
{
    if (cfg == NULL || s == NULL)
        return ASH_CFG_DEFAULT;
    if (strcmp(s->key, "thinking_level") == 0)
        return cfg->thinking_layer;
    const ash_cfg_field *f = field_for(cfg, s);
    return f != NULL ? f->layer : ASH_CFG_DEFAULT;
}

static int enum_has(const ash_setting *s, const char *value)
{
    for (size_t i = 0; i < s->noptions; i++)
        if (strcmp(s->options[i], value) == 0)
            return 1;
    return 0;
}

ash_status ash_settings_write(const ash_config *cfg, ash_config_layer layer,
                              const ash_setting *s, const char *value)
{
    if (cfg == NULL || s == NULL || value == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_settings_write: bad arguments");

    switch (s->kind) {
    case ASH_SETTING_ENUM:
        if (!enum_has(s, value))
            return ash_fail(ASH_ERR_PARSE,
                            "settings: '%s' is not a valid %s", value, s->key);
        break;
    case ASH_SETTING_TEXT:
        break;
    }

    if (strcmp(s->key, "thinking_level") == 0) {
        ash_thinking level;
        if (ash_thinking_parse(value, &level) != ASH_OK)
            return ash_fail(ASH_ERR_PARSE,
                            "settings: unknown thinking level '%s'", value);
    }

    return ash_config_set(cfg, layer, s->key, value);
}

const char *ash_settings_cycle(const ash_setting *s, const char *current, int dir)
{
    if (s == NULL || s->noptions == 0)
        return NULL;
    size_t n = s->noptions;
    size_t at = 0;
    if (current != NULL)
        for (size_t i = 0; i < n; i++)
            if (strcmp(s->options[i], current) == 0) {
                at = i;
                break;
            }
    size_t step = dir < 0 ? n - 1 : 1;
    return s->options[(at + step) % n];
}
