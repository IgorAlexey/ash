#ifndef ASH_BASE_BASE64_H
#define ASH_BASE_BASE64_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"

ASH_API size_t ash_base64url_encode(const uint8_t *data, size_t len,
                                    char *out, size_t out_cap);

#endif
