#ifndef ASH_CORE_CONFIG_H
#define ASH_CORE_CONFIG_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/status.h"

typedef enum ash_config_layer {
    ASH_CFG_DEFAULT = 0,
    ASH_CFG_GLOBAL,
    ASH_CFG_PROJECT,
    ASH_CFG_ENV
} ash_config_layer;

typedef enum ash_thinking {
    ASH_THINK_OFF = 0,
    ASH_THINK_MINIMAL,
    ASH_THINK_LOW,
    ASH_THINK_MEDIUM,
    ASH_THINK_HIGH,
    ASH_THINK_XHIGH,
    ASH_THINK_MAX
} ash_thinking;

typedef struct ash_cfg_field {
    const char      *value;
    ash_config_layer layer;
} ash_cfg_field;

typedef struct ash_cfg_provider {
    const char   *name;
    ash_cfg_field api_key;
    ash_cfg_field base_url;
} ash_cfg_provider;

typedef struct ash_config {
    ash_arena       *arena;
    const char      *global_path;
    const char      *project_path;

    ash_cfg_field    provider;
    ash_cfg_field    model;
    ash_cfg_field    theme;
    ash_cfg_field    system;
    ash_cfg_field    base_url;
    ash_cfg_field    api_key;

    ash_thinking     thinking;
    ash_config_layer thinking_layer;

    ash_cfg_provider *providers;
    size_t            provider_count;

    int               warnings;
} ash_config;

ASH_API ASH_WUR ash_status ash_config_load(ash_arena *a, ash_config *out);

ASH_API ASH_WUR ash_status ash_config_set(const ash_config *cfg,
                                          ash_config_layer layer,
                                          const char *key, const char *value);

ASH_API const char *ash_config_layer_str(ash_config_layer layer);
ASH_API const char *ash_thinking_str(ash_thinking level);
ASH_API ASH_WUR ash_status ash_thinking_parse(const char *s, ash_thinking *out);

#endif
