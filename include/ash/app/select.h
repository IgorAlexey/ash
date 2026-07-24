#ifndef ASH_APP_SELECT_H
#define ASH_APP_SELECT_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/buf.h"
#include "ash/fb/fb.h"
#include "ash/fb/scrollback.h"

typedef struct ash_selection {
    int      active;
    uint64_t anchor_seq;
    int      anchor_col;
    uint64_t point_seq;
    int      point_col;
} ash_selection;

typedef struct ash_sb_viewport {
    uint64_t top_seq;
    int      top_row;
    int      rows;
    int      cols;
} ash_sb_viewport;

void ash_sel_clear(ash_selection *s);
int  ash_sel_empty(const ash_selection *s);

int  ash_sel_hittest(const ash_scrollback *sb, const ash_sb_viewport *vp,
                     int row, int col, uint64_t *seq, int *cell_col);

void ash_sel_set(ash_selection *s, uint64_t seq, int col);
void ash_sel_extend(ash_selection *s, uint64_t seq, int col);
void ash_sel_word(ash_selection *s, const ash_scrollback *sb,
                  uint64_t seq, int col);
void ash_sel_line(ash_selection *s, const ash_scrollback *sb,
                  uint64_t seq, int col);

void ash_sel_apply(const ash_selection *s, const ash_scrollback *sb,
                   const ash_sb_viewport *vp, ash_fb *fb, ash_style style);

size_t ash_sel_extract(const ash_selection *s, const ash_scrollback *sb,
                       ash_buf *out);

size_t ash_base64_encode(const void *in, size_t n, ash_buf *out);

#endif
