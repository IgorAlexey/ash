#include <stdint.h>
#include <string.h>

#include "ash/base/base64.h"
#include "ash_test.h"

static void enc(const char *in, const char *want)
{
    char out[128];
    size_t n = ash_base64url_encode((const uint8_t *)in, strlen(in), out,
                                    sizeof out);
    ASH_CHECK(n == strlen(want));
    ASH_CHECK_STREQ(out, want);
}

int main(void)
{
    enc("f", "Zg");
    enc("fo", "Zm8");
    enc("foo", "Zm9v");
    enc("foob", "Zm9vYg");
    enc("fooba", "Zm9vYmE");
    enc("foobar", "Zm9vYmFy");

    char out[16];
    ASH_CHECK(ash_base64url_encode((const uint8_t *)"", 0, out, sizeof out) == 0);
    ASH_CHECK(out[0] == '\0');

    const uint8_t all_ff[] = { 0xff, 0xff, 0xff };
    ASH_CHECK(ash_base64url_encode(all_ff, 3, out, sizeof out) == 4);
    ASH_CHECK_STREQ(out, "____");

    const uint8_t dash[] = { 0xf8, 0x00, 0x00 };
    ASH_CHECK(ash_base64url_encode(dash, 3, out, sizeof out) == 4);
    ASH_CHECK_STREQ(out, "-AAA");

    char tight[3];
    ASH_CHECK(ash_base64url_encode((const uint8_t *)"f", 1, tight, 3) == 2);
    ASH_CHECK_STREQ(tight, "Zg");
    ASH_CHECK(ash_base64url_encode((const uint8_t *)"f", 1, tight, 2) == 0);
    ASH_CHECK(ash_base64url_encode((const uint8_t *)"foo", 3, tight, 3) == 0);

    return ash_test_done();
}
