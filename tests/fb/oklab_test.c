#include <stdlib.h>

#include "ash/fb/oklab.h"
#include "ash_test.h"

static int near(uint32_t a, uint32_t b, uint32_t tol)
{
    uint32_t d = a > b ? a - b : b - a;
    return d <= tol;
}

int main(void)
{
    ASH_CHECK(ash_rgba_r(ash_rgb(0x12, 0x34, 0x56)) == 0x12);
    ASH_CHECK(ash_rgba_g(ash_rgb(0x12, 0x34, 0x56)) == 0x34);
    ASH_CHECK(ash_rgba_b(ash_rgb(0x12, 0x34, 0x56)) == 0x56);
    ASH_CHECK(ash_rgba_a(ash_rgb(0x12, 0x34, 0x56)) == 0xff);

    ash_rgba bottom = ash_rgb(0x34, 0x98, 0xdb);
    ash_rgba top = ash_rgba_make(0xe7, 0x4c, 0x3c, 0x7f);
    ash_rgba got = ash_rgba_blend(bottom, top);
    ASH_CHECK(ash_rgba_r(got) == 0xa6);
    ASH_CHECK(ash_rgba_g(got) == 0x7f);
    ASH_CHECK(ash_rgba_b(got) == 0x93);
    ASH_CHECK(ash_rgba_a(got) == 0xff);

    ash_rgba probe[] = {
        ash_rgb(0, 0, 0), ash_rgb(255, 255, 255), ash_rgb(0x34, 0x98, 0xdb),
        ash_rgb(0xe7, 0x4c, 0x3c), ash_rgb(0x3f, 0xae, 0x3a),
    };
    for (size_t i = 0; i < sizeof probe / sizeof probe[0]; i++) {
        ash_rgba rt = ash_oklab_to_rgba(ash_rgba_to_oklab(probe[i]));
        ASH_CHECK(near(ash_rgba_r(rt), ash_rgba_r(probe[i]), 3));
        ASH_CHECK(near(ash_rgba_g(rt), ash_rgba_g(probe[i]), 3));
        ASH_CHECK(near(ash_rgba_b(rt), ash_rgba_b(probe[i]), 3));
    }

    float lb = ash_rgba_lightness(ash_rgb(0, 0, 0));
    float lg = ash_rgba_lightness(ash_rgb(128, 128, 128));
    float lw = ash_rgba_lightness(ash_rgb(255, 255, 255));
    ASH_CHECK(lb < lg);
    ASH_CHECK(lg < lw);

    ash_contrast ct;
    ash_contrast_init(&ct, ash_rgb(0, 0, 0), ash_rgb(255, 255, 255));
    ASH_CHECK(ash_contrasted(&ct, ash_rgb(0, 0, 0)) == ash_rgb(255, 255, 255));
    ASH_CHECK(ash_contrasted(&ct, ash_rgb(255, 255, 255)) == ash_rgb(0, 0, 0));
    ASH_CHECK(ash_contrasted(&ct, ash_rgb(0x20, 0x30, 0x80)) == ash_rgb(255, 255, 255));
    ASH_CHECK(ash_contrasted(&ct, ash_rgb(0xf0, 0xe0, 0xc0)) == ash_rgb(0, 0, 0));
    ASH_CHECK(ash_contrasted(&ct, ash_rgb(0x20, 0x30, 0x80)) == ash_rgb(255, 255, 255));

    ash_contrast swapped;
    ash_contrast_init(&swapped, ash_rgb(255, 255, 255), ash_rgb(0, 0, 0));
    ASH_CHECK(ash_contrasted(&swapped, ash_rgb(0, 0, 0)) == ash_rgb(255, 255, 255));

    return ash_test_done();
}
