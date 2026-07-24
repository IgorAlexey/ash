#include <string.h>

#include "ash/base/slice.h"
#include "ash/base/poison.h"

ash_slice ash_slice_make(const char *p, size_t len)
{
    ash_slice s = { p, len };
    return s;
}

ash_slice ash_slice_from_cstr(const char *s)
{
    ash_slice r = { s, s ? strlen(s) : 0 };
    return r;
}

int ash_slice_eq(ash_slice a, ash_slice b)
{
    if (a.len != b.len)
        return 0;
    return a.len == 0 || memcmp(a.p, b.p, a.len) == 0;
}

int ash_slice_eq_cstr(ash_slice a, const char *s)
{
    return ash_slice_eq(a, ash_slice_from_cstr(s));
}
