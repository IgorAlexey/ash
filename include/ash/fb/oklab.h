#ifndef ASH_FB_OKLAB_H
#define ASH_FB_OKLAB_H

#include <stdint.h>

#include "ash/base/api.h"

typedef uint32_t ash_rgba;

typedef struct ash_oklab {
    float l;
    float a;
    float b;
    float alpha;
} ash_oklab;

#define ASH_RGBA_DEFAULT ((ash_rgba)0)

ASH_API ash_rgba ash_rgb(uint8_t r, uint8_t g, uint8_t b);
ASH_API ash_rgba ash_rgba_make(uint8_t r, uint8_t g, uint8_t b, uint8_t a);
ASH_API uint32_t ash_rgba_r(ash_rgba c);
ASH_API uint32_t ash_rgba_g(ash_rgba c);
ASH_API uint32_t ash_rgba_b(ash_rgba c);
ASH_API uint32_t ash_rgba_a(ash_rgba c);

ASH_API ash_oklab ash_rgba_to_oklab(ash_rgba c);
ASH_API ash_rgba  ash_oklab_to_rgba(ash_oklab o);
ASH_API float     ash_rgba_lightness(ash_rgba c);
ASH_API ash_rgba  ash_rgba_blend(ash_rgba bottom, ash_rgba top);

typedef struct ash_contrast {
    ash_rgba dark;
    ash_rgba light;
    float    threshold;
    ash_rgba key[256];
    ash_rgba val[256];
    uint8_t  used[256];
} ash_contrast;

ASH_API void     ash_contrast_init(ash_contrast *c, ash_rgba dark, ash_rgba light);
ASH_API ash_rgba ash_contrasted(ash_contrast *c, ash_rgba color);

#endif
