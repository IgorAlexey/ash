#ifndef ASH_BASE_SLICE_H
#define ASH_BASE_SLICE_H

#include <stddef.h>

#include "ash/base/api.h"

typedef struct ash_slice {
    const char *p;
    size_t      len;
} ash_slice;

ASH_API ash_slice ash_slice_make(const char *p, size_t len);
ASH_API ash_slice ash_slice_from_cstr(const char *s);
ASH_API int       ash_slice_eq(ash_slice a, ash_slice b);
ASH_API int       ash_slice_eq_cstr(ash_slice a, const char *s);

#endif
