#ifndef ASH_EDIT_DIFFVIEW_H
#define ASH_EDIT_DIFFVIEW_H

#include <stddef.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/fb/fb.h"
#include "ash/tools/tools.h"
#include "ash/ui/keys.h"

typedef enum ash_diff_op {
    ASH_DIFF_EQ = 0,
    ASH_DIFF_DEL,
    ASH_DIFF_ADD
} ash_diff_op;

typedef struct ash_diff_line {
    ash_diff_op op;
    const char *text;
    size_t      len;
    int         old_no;
    int         new_no;
} ash_diff_line;

typedef struct ash_diff {
    ash_diff_line *lines;
    int            count;
    int            additions;
    int            deletions;
} ash_diff;

ASH_API void ash_diff_compute(ash_arena *a, const char *old, size_t oldlen,
                              const char *neu, size_t newlen, ash_diff *out);

typedef enum ash_diffview_action {
    ASH_DIFFVIEW_NONE = 0,
    ASH_DIFFVIEW_ACCEPT,
    ASH_DIFFVIEW_REJECT,
    ASH_DIFFVIEW_EDIT
} ash_diffview_action;

typedef struct ash_diffview_theme {
    ash_style context;
    ash_style add;
    ash_style del;
    ash_style gutter;
    ash_style header;
    ash_style hint;
} ash_diffview_theme;

typedef struct ash_diffview {
    ash_arena  *arena;
    const char *path;
    ash_diff    diff;
    int         context;
    int         scroll;
    int         gutter_w;
} ash_diffview;

ASH_API void ash_diffview_init(ash_diffview *dv, ash_arena *arena,
                               const char *path);
ASH_API void ash_diffview_set(ash_diffview *dv, const char *old, size_t oldlen,
                              const char *neu, size_t newlen);
ASH_API int  ash_diffview_propose(ash_diffview *dv, const char *content,
                                  size_t clen, const ash_edit_spec *edits,
                                  int ne, const char **err);

ASH_API ash_diffview_action ash_diffview_key(ash_diffview *dv, ash_key k,
                                             int view_h);
ASH_API void ash_diffview_render(ash_diffview *dv, ash_fb *fb, ash_rect rect,
                                 const ash_diffview_theme *theme);

#endif
