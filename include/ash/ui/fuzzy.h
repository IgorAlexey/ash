#ifndef ASH_UI_FUZZY_H
#define ASH_UI_FUZZY_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"

#define ASH_FUZZY_MAX_POS 256

typedef struct ash_fuzzy_match {
    int32_t score;
    int32_t positions[ASH_FUZZY_MAX_POS];
    int32_t npos;
} ash_fuzzy_match;

ASH_API int32_t ash_fuzzy_score(ash_arena *scratch,
                                const char *haystack, size_t hlen,
                                const char *needle, size_t nlen,
                                int allow_non_contiguous,
                                ash_fuzzy_match *out);

#endif
