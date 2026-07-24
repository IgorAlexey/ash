#ifndef ASH_UI_TEXTAREA_H
#define ASH_UI_TEXTAREA_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/fb.h"
#include "ash/ui/keys.h"

enum { ASH_TA_UNDO_CAP = 64 };

typedef struct ash_ta_snap {
    uint8_t *data;
    size_t   cap;
    size_t   len;
    size_t   cursor;
} ash_ta_snap;

typedef struct ash_textarea {
    ash_arena  *arena;
    uint8_t    *data;
    size_t      len;
    size_t      cap;
    size_t      cursor;
    size_t      sel;
    int         sel_on;
    int         width;
    int         max_rows;
    int         scroll;
    ash_ta_snap undo[ASH_TA_UNDO_CAP];
    int         undo_len;
    int         undo_cur;
    int         undo_group;
} ash_textarea;

ASH_API void   ash_textarea_init(ash_textarea *ta, ash_arena *arena,
                                 int width, int max_rows);
ASH_API void   ash_textarea_set_width(ash_textarea *ta, int width);
ASH_API void   ash_textarea_clear(ash_textarea *ta);

ASH_API void   ash_textarea_apply(ash_textarea *ta, ash_key k);
ASH_API void   ash_textarea_insert(ash_textarea *ta, const void *utf8, size_t len);
ASH_API void   ash_textarea_set(ash_textarea *ta, const void *utf8, size_t len);
ASH_API int    ash_textarea_backspace(ash_textarea *ta);

ASH_API size_t ash_textarea_len(const ash_textarea *ta);
ASH_API size_t ash_textarea_cursor(const ash_textarea *ta);
ASH_API int    ash_textarea_at_end(const ash_textarea *ta);
ASH_API size_t ash_textarea_text(const ash_textarea *ta, ash_buf *out);

ASH_API int    ash_textarea_has_selection(const ash_textarea *ta);
ASH_API size_t ash_textarea_selection(const ash_textarea *ta, ash_buf *out);
ASH_API void   ash_textarea_delete_selection(ash_textarea *ta);

ASH_API int    ash_textarea_rows(const ash_textarea *ta);
ASH_API void   ash_textarea_cursor_rc(const ash_textarea *ta, int *row, int *col);
ASH_API int    ash_textarea_height(const ash_textarea *ta);
ASH_API void   ash_textarea_render(ash_textarea *ta, ash_fb *fb, ash_rect rect,
                                   ash_style st);

typedef struct ash_input_rule {
    int first_visible;
    int visible_rows;
    int total_rows;
    int width;
} ash_input_rule;

ASH_API int       ash_input_rule_hidden(ash_input_rule r, int below);
ASH_API void      ash_input_rule_text(ash_input_rule r, int below, ash_buf *out);
ASH_API ash_style ash_input_border_style(void);
ASH_API void      ash_input_bar_render(ash_textarea *ta, ash_fb *fb,
                                       ash_rect rect, ash_style text_st,
                                       ash_style border_st, ash_buf *scratch);

typedef struct ash_history {
    ash_arena    *arena;
    const char  **items;
    uint32_t     *lens;
    size_t        cap;
    size_t        count;
    long          pos;
} ash_history;

ASH_API void        ash_history_init(ash_history *h, ash_arena *arena, size_t cap);
ASH_API void        ash_history_push(ash_history *h, const void *text, size_t len);
ASH_API void        ash_history_reset(ash_history *h);
ASH_API const char *ash_history_prev(ash_history *h, size_t *len);
ASH_API const char *ash_history_next(ash_history *h, size_t *len);

#endif
