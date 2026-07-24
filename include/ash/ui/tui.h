#ifndef ASH_UI_TUI_H
#define ASH_UI_TUI_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/fb.h"
#include "ash/term/input.h"
#include "ash/ui/keys.h"
#include "ash/ui/textarea.h"

enum {
    ASH_TK_TAB    = 9,
    ASH_TK_ENTER  = 13,
    ASH_TK_ESCAPE = 27,
    ASH_TK_SPACE  = 32
};

typedef enum ash_position {
    ASH_POS_STRETCH = 0,
    ASH_POS_LEFT,
    ASH_POS_CENTER,
    ASH_POS_RIGHT
} ash_position;

typedef enum ash_anchor {
    ASH_ANCHOR_LAST = 0,
    ASH_ANCHOR_PARENT,
    ASH_ANCHOR_ROOT
} ash_anchor;

typedef struct ash_float_spec {
    ash_anchor anchor;
    float      gravity_x;
    float      gravity_y;
    float      offset_x;
    float      offset_y;
} ash_float_spec;

typedef enum ash_list_sel {
    ASH_LIST_UNCHANGED = 0,
    ASH_LIST_SELECTED,
    ASH_LIST_ACTIVATED
} ash_list_sel;

typedef struct ash_button_style {
    uint32_t accel;
    int8_t   checked;
    int      bracketed;
} ash_button_style;

typedef struct ash_padding {
    int left;
    int top;
    int right;
    int bottom;
} ash_padding;

typedef struct ash_node ash_node;
typedef struct ash_tree ash_tree;
typedef struct ash_nodemap ash_nodemap;
typedef struct ash_ctx ash_ctx;

typedef struct ash_tui {
    ash_arena    arena_a;
    ash_arena    arena_b;
    ash_arena   *arena_prev;
    ash_arena   *arena_next;
    ash_tree    *prev_tree;
    ash_nodemap *prev_map;
    uint64_t     prev_checksum;

    int w;
    int h;

    uint64_t focus_path[32];
    int      focus_len;
    uint64_t focus_for_scroll;

    int settling_have;
    int settling_want;

    ash_ctx *ctx;
} ash_tui;

ASH_API ASH_WUR ash_status ash_tui_init(ash_tui *t);
ASH_API void               ash_tui_destroy(ash_tui *t);

ASH_API ash_ctx *ash_tui_begin(ash_tui *t, int w, int h,
                               const ash_input_event *ev);
ASH_API void     ash_tui_end(ash_ctx *c);
ASH_API void     ash_tui_render(ash_tui *t, ash_fb *fb);
ASH_API int      ash_tui_settling(const ash_tui *t);

ASH_API void ash_block_begin(ash_ctx *c, const char *classname);
ASH_API void ash_block_end(ash_ctx *c);
ASH_API void ash_next_id_mixin(ash_ctx *c, uint64_t mixin);

ASH_API void ash_attr_border(ash_ctx *c);
ASH_API void ash_attr_padding(ash_ctx *c, ash_padding p);
ASH_API void ash_attr_position(ash_ctx *c, ash_position pos);
ASH_API void ash_attr_float(ash_ctx *c, ash_float_spec spec);
ASH_API void ash_attr_bg(ash_ctx *c, ash_rgba bg);
ASH_API void ash_attr_fg(ash_ctx *c, ash_rgba fg);
ASH_API void ash_attr_reverse(ash_ctx *c);
ASH_API void ash_attr_focus_well(ash_ctx *c);
ASH_API void ash_attr_intrinsic_size(ash_ctx *c, int w, int h);

ASH_API void ash_focus_on_first_present(ash_ctx *c);
ASH_API void ash_steal_focus(ash_ctx *c);
ASH_API void ash_inherit_focus(ash_ctx *c);
ASH_API void ash_toss_focus_up(ash_ctx *c);
ASH_API int  ash_is_focused(ash_ctx *c);
ASH_API int  ash_contains_focus(ash_ctx *c);
ASH_API int  ash_consume_shortcut(ash_ctx *c, uint32_t key, uint32_t mods);

ASH_API void ash_label(ash_ctx *c, const char *classname, const char *text);
ASH_API void ash_styled_label_begin(ash_ctx *c, const char *classname);
ASH_API void ash_styled_label_fg(ash_ctx *c, ash_rgba fg);
ASH_API void ash_styled_label_attr(ash_ctx *c, uint16_t attr);
ASH_API void ash_styled_label_text(ash_ctx *c, const char *text);
ASH_API void ash_styled_label_end(ash_ctx *c);

ASH_API ash_button_style ash_button_default(void);
ASH_API int  ash_button(ash_ctx *c, const char *classname, const char *text,
                        ash_button_style style);
ASH_API int  ash_checkbox(ash_ctx *c, const char *classname, const char *text,
                          int *checked);

ASH_API void         ash_list_begin(ash_ctx *c, const char *classname);
ASH_API ash_list_sel ash_list_item(ash_ctx *c, int select, const char *text);
ASH_API void         ash_list_end(ash_ctx *c);

ASH_API void ash_scrollarea_begin(ash_ctx *c, const char *classname,
                                  int intrinsic_w, int intrinsic_h);
ASH_API void ash_scrollarea_end(ash_ctx *c);

ASH_API void ash_modal_begin(ash_ctx *c, const char *classname,
                             const char *title);
ASH_API int  ash_modal_end(ash_ctx *c);

ASH_API void ash_textarea_widget(ash_ctx *c, const char *classname,
                                 ash_textarea *ta);

#endif
