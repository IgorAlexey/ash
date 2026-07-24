#ifndef ASH_TERM_VTOUT_H
#define ASH_TERM_VTOUT_H

#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/buf.h"
#include "ash/fb/oklab.h"

ASH_LOCAL void ash_vt_u32(ash_buf *b, uint32_t v);
ASH_LOCAL void ash_vt_reset(ash_buf *b);
ASH_LOCAL void ash_vt_cup(ash_buf *b, int row, int col);
ASH_LOCAL void ash_vt_color(ash_buf *b, int fg, ash_rgba c);
ASH_LOCAL void ash_vt_attr_diff(ash_buf *b, uint16_t old, uint16_t neu);
ASH_LOCAL void ash_vt_cursor_show(ash_buf *b, int row, int col, int style);
ASH_LOCAL void ash_vt_cursor_hide(ash_buf *b);

#endif
