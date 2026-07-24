#include <string.h>

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash_test.h"

int main(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "buf", 64) == ASH_OK);

    ash_buf b;
    ash_buf_init(&b, &a);
    ash_buf_append_cstr(&b, "hello");
    ash_buf_append_byte(&b, ' ');
    ash_buf_append(&b, "world", 5);
    ASH_CHECK(b.len == 11);
    ASH_CHECK(memcmp(b.data, "hello world", 11) == 0);

    for (int i = 0; i < 1000; i++)
        ash_buf_append_byte(&b, (unsigned char)('a' + (i % 26)));
    ASH_CHECK(b.len == 1011);
    ASH_CHECK(b.data[11] == 'a');
    ASH_CHECK(b.data[1010] == (unsigned char)('a' + (999 % 26)));

    ash_arena_destroy(&a);
    return ash_test_done();
}
