#ifndef ASH_BASE_SHA256_H
#define ASH_BASE_SHA256_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"

#define ASH_SHA256_DIGEST 32u

ASH_API void ash_sha256(const void *data, size_t len,
                        uint8_t out[ASH_SHA256_DIGEST]);

#endif
