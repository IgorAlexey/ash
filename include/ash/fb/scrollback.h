#ifndef ASH_FB_SCROLLBACK_H
#define ASH_FB_SCROLLBACK_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/fb/cell.h"

typedef struct ash_sb_line {
    uint64_t seq;
    uint32_t off;
    uint32_t len;
    uint8_t  cont;
} ash_sb_line;

typedef struct ash_scrollback {
    ash_cell    *cells;
    size_t       cap_cells;
    ash_sb_line *lines;
    size_t       cap_lines;
    size_t       head;
    size_t       count;
    size_t       write;
    uint64_t     next_seq;
    int64_t      view;
} ash_scrollback;

ASH_API void ash_sb_init(ash_scrollback *sb, ash_arena *a,
                         size_t cell_budget, size_t max_lines);
ASH_API void ash_sb_reset(ash_scrollback *sb);
ASH_API void ash_sb_append(ash_scrollback *sb, const ash_cell *cells, size_t n);
ASH_API void ash_sb_append_wrapped(ash_scrollback *sb, const ash_cell *cells,
                                   size_t n, int cont);
ASH_API int  ash_sb_cont(const ash_scrollback *sb, uint64_t seq);
ASH_API void ash_sb_rewind_to(ash_scrollback *sb, uint64_t seq);

ASH_API size_t   ash_sb_count(const ash_scrollback *sb);
ASH_API uint64_t ash_sb_oldest(const ash_scrollback *sb);
ASH_API uint64_t ash_sb_newest(const ash_scrollback *sb);
ASH_API int      ash_sb_line_at(const ash_scrollback *sb, uint64_t seq,
                                const ash_cell **out, size_t *n);

ASH_API void ash_sb_scroll_to(ash_scrollback *sb, uint64_t seq);
ASH_API void ash_sb_scroll_by(ash_scrollback *sb, int64_t delta, int viewport);
ASH_API void ash_sb_follow(ash_scrollback *sb);
ASH_API int  ash_sb_is_following(const ash_scrollback *sb);
ASH_API uint64_t ash_sb_view_top(const ash_scrollback *sb, int viewport);

#endif
