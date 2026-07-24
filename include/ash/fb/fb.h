#ifndef ASH_FB_FB_H
#define ASH_FB_FB_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/cell.h"
#include "ash/fb/oklab.h"

typedef struct ash_rect {
    int x;
    int y;
    int w;
    int h;
} ash_rect;

enum ash_cursor_style {
    ASH_CURSOR_HIDDEN = 0,
    ASH_CURSOR_BLOCK,
    ASH_CURSOR_BAR
};

typedef struct ash_cursor {
    int x;
    int y;
    int style;
} ash_cursor;

#define ASH_FB_CLIP_MAX 16

typedef struct ash_fb {
    ash_arena   *arena;
    int          w;
    int          h;
    ash_cell    *buffers[2];
    unsigned     frame;
    int          full_redraw;
    ash_style    fill;
    ash_cursor   cursor[2];
    ash_rect     clip[ASH_FB_CLIP_MAX];
    int          clip_top;
    ash_contrast contrast;
} ash_fb;

ASH_API void ash_fb_init(ash_fb *fb, ash_arena *arena, ash_style fill);
ASH_API void ash_fb_begin(ash_fb *fb, int w, int h);
ASH_API void ash_fb_put_text(ash_fb *fb, int x, int y, ash_style st,
                             const void *utf8, size_t len);
ASH_API void ash_fb_fill_rect(ash_fb *fb, ash_rect r, ash_style st, uint32_t cp);
ASH_API void ash_fb_add_attr(ash_fb *fb, int x, int y, int n, uint16_t attr);
ASH_API void ash_fb_style_range(ash_fb *fb, int x, int y, int n, ash_style st);
ASH_API ash_style ash_selection_style(void);
ASH_API void ash_fb_set_cursor(ash_fb *fb, int x, int y, int style);
ASH_API void ash_fb_hide_cursor(ash_fb *fb);
ASH_API int  ash_fb_clip_push(ash_fb *fb, ash_rect r);
ASH_API void ash_fb_clip_pop(ash_fb *fb);
ASH_API void ash_fb_flip(ash_fb *fb, ash_buf *out);
ASH_API void ash_fb_snapshot(const ash_fb *fb, ash_buf *out);
ASH_API ash_rgba ash_fb_contrasted(ash_fb *fb, ash_rgba color);

ASH_API int    ash_char_width(uint32_t cp);
ASH_API size_t ash_utf8_decode(const void *utf8, size_t len, uint32_t *cp_out);

#endif
