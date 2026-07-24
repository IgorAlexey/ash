#ifndef ASH_EXT_EXT_H
#define ASH_EXT_EXT_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/status.h"
#include "ash/core/config.h"
#include "ash/tools/tools.h"

typedef struct ash_ext ash_ext;

typedef struct ash_ext_limits {
    size_t   mem_budget;
    uint64_t instr_budget;
    int      hook_count;
} ash_ext_limits;

ASH_API ash_ext_limits ash_ext_limits_default(void);

ASH_API ASH_WUR ash_status ash_ext_create(ash_ext **out,
                                          const ash_ext_limits *lim,
                                          const ash_config *cfg);
ASH_API void ash_ext_destroy(ash_ext *e);

ASH_API ASH_WUR ash_status ash_ext_eval(ash_ext *e, const char *src,
                                        size_t len, const char *name);

ASH_API size_t      ash_ext_tool_count(const ash_ext *e);
ASH_API ash_tool   *ash_ext_tool_at(ash_ext *e, size_t i);

ASH_API size_t      ash_ext_keybinding_count(const ash_ext *e);
ASH_API const char *ash_ext_keybinding_name(const ash_ext *e, size_t i);
ASH_API ASH_WUR ash_status ash_ext_keybinding_invoke(ash_ext *e,
                                                     const char *name);

#endif
