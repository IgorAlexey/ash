#ifndef ASH_CORE_SETTINGS_H
#define ASH_CORE_SETTINGS_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/status.h"
#include "ash/core/config.h"

typedef enum ash_setting_kind {
    ASH_SETTING_TEXT = 0,
    ASH_SETTING_ENUM
} ash_setting_kind;

typedef struct ash_setting {
    const char        *key;
    const char        *label;
    ash_setting_kind   kind;
    const char *const *options;
    size_t             noptions;
} ash_setting;

ASH_API const ash_setting *ash_settings_schema(size_t *count);

ASH_API const char *ash_settings_value(const ash_config *cfg,
                                       const ash_setting *s);

ASH_API ash_config_layer ash_settings_layer(const ash_config *cfg,
                                            const ash_setting *s);

ASH_API ASH_WUR ash_status ash_settings_write(const ash_config *cfg,
                                              ash_config_layer layer,
                                              const ash_setting *s,
                                              const char *value);

ASH_API const char *ash_settings_cycle(const ash_setting *s,
                                       const char *current, int dir);

#endif
