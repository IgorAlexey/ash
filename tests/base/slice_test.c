#include "ash/base/slice.h"
#include "ash_test.h"

int main(void)
{
    ash_slice a = ash_slice_from_cstr("foo");
    ASH_CHECK(a.len == 3);
    ASH_CHECK(ash_slice_eq_cstr(a, "foo"));
    ASH_CHECK(!ash_slice_eq_cstr(a, "foobar"));
    ASH_CHECK(!ash_slice_eq_cstr(a, "bar"));

    ash_slice b = ash_slice_make("fooxyz", 3);
    ASH_CHECK(ash_slice_eq(a, b));

    ash_slice empty = ash_slice_from_cstr("");
    ASH_CHECK(empty.len == 0);
    ASH_CHECK(ash_slice_eq_cstr(empty, ""));
    ASH_CHECK(!ash_slice_eq_cstr(empty, "x"));

    return ash_test_done();
}
