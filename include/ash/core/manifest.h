#ifndef ASH_CORE_MANIFEST_H
#define ASH_CORE_MANIFEST_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/slice.h"
#include "ash/base/status.h"

#define ASH_EXT_API_VERSION 1u

typedef struct ash_manifest {
    ash_slice name;
    ash_slice entry;
    uint32_t  api_version;
    int       has_api;
} ash_manifest;

ASH_API ASH_WUR ash_status ash_manifest_parse(const char *text, size_t len,
                                              ash_manifest *out);
ASH_API ASH_WUR ash_status ash_manifest_check(const ash_manifest *m);

#endif
