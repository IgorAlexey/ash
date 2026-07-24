#ifndef ASH_EDIT_EDITOR_H
#define ASH_EDIT_EDITOR_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/edit/gapbuf.h"
#include "ash/fb/fb.h"
#include "ash/ui/keys.h"

typedef struct ash_editor {
    ash_arena *arena;
    ash_gapbuf buf;
    size_t     cursor;
    size_t     anchor;
    int        goal_col;
    int        top_line;
    int        left_col;
    int        tab_size;
    int        show_gutter;
    ash_rect   view;
    int        gutter_w;
} ash_editor;

ASH_API void   ash_editor_init(ash_editor *ed, ash_arena *arena);
ASH_API void   ash_editor_set_text(ash_editor *ed, const void *text, size_t len);
ASH_API void   ash_editor_text(const ash_editor *ed, ash_buf *out);
ASH_API size_t ash_editor_len(const ash_editor *ed);
ASH_API size_t ash_editor_cursor(const ash_editor *ed);

ASH_API int    ash_editor_selection(const ash_editor *ed, size_t *start,
                                    size_t *end);
ASH_API void   ash_editor_set_cursor(ash_editor *ed, size_t off, int extend);
ASH_API void   ash_editor_move(ash_editor *ed, ash_editcmd cmd, int extend);
ASH_API void   ash_editor_apply(ash_editor *ed, ash_key k);

ASH_API void   ash_editor_line_col(const ash_editor *ed, size_t off, int *line,
                                   int *col);
ASH_API int    ash_editor_hit(const ash_editor *ed, int screen_x, int screen_y,
                              size_t *off);
ASH_API int    ash_editor_click(ash_editor *ed, int screen_x, int screen_y,
                                int extend);

ASH_API void   ash_editor_render(ash_editor *ed, ash_fb *fb, ash_rect rect,
                                 ash_style st, ash_style sel);

#endif
