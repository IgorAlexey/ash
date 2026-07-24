#ifndef ASH_FB_CELL_H
#define ASH_FB_CELL_H

#include <stdint.h>

#include "ash/base/api.h"
#include "ash/fb/oklab.h"

enum {
    ASH_ATTR_NONE          = 0,
    ASH_ATTR_BOLD          = 1 << 0,
    ASH_ATTR_ITALIC        = 1 << 1,
    ASH_ATTR_UNDERLINE     = 1 << 2,
    ASH_ATTR_REVERSE       = 1 << 3,
    ASH_ATTR_STRIKETHROUGH = 1 << 4,
    ASH_ATTR_CONTENT       = 1 << 5
};

typedef struct ash_style {
    ash_rgba fg;
    ash_rgba bg;
    uint16_t attr;
} ash_style;

#define ASH_CELL_BYTES 30

typedef struct ash_cell {
    uint8_t  bytes[ASH_CELL_BYTES];
    uint8_t  len;
    uint8_t  width;
    uint16_t attr;
    ash_rgba fg;
    ash_rgba bg;
} ash_cell;

#endif
