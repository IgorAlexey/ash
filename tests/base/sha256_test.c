#include <stdint.h>
#include <string.h>

#include "ash/base/sha256.h"
#include "ash_test.h"

static int hexeq(const uint8_t *d, const char *hex)
{
    static const char *H = "0123456789abcdef";
    for (int i = 0; i < 32; i++) {
        char hi = H[d[i] >> 4];
        char lo = H[d[i] & 15];
        if (hi != hex[i * 2] || lo != hex[i * 2 + 1])
            return 0;
    }
    return hex[64] == '\0';
}

static void vec(const char *msg, const char *hex)
{
    uint8_t d[ASH_SHA256_DIGEST];
    ash_sha256(msg, strlen(msg), d);
    ASH_CHECK(hexeq(d, hex));
}

int main(void)
{
    vec("",
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    vec("abc",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    vec("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");

    char big[1000000];
    memset(big, 'a', sizeof big);
    uint8_t d[ASH_SHA256_DIGEST];
    ash_sha256(big, sizeof big, d);
    ASH_CHECK(hexeq(
        d, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"));

    uint8_t d55[ASH_SHA256_DIGEST];
    uint8_t d56[ASH_SHA256_DIGEST];
    char m55[55], m56[56];
    memset(m55, 'x', sizeof m55);
    memset(m56, 'x', sizeof m56);
    ash_sha256(m55, sizeof m55, d55);
    ash_sha256(m56, sizeof m56, d56);
    ASH_CHECK(memcmp(d55, d56, 32) != 0);

    return ash_test_done();
}
