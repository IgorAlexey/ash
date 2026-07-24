#ifndef ASH_EDIT_MEASURE_H
#define ASH_EDIT_MEASURE_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/edit/gapbuf.h"

typedef struct ash_grapheme {
    size_t   offset;
    size_t   next;
    int      width;
    uint32_t first_cp;
} ash_grapheme;

ASH_API int    ash_measure_next(const ash_gapbuf *b, size_t off, int column,
                                int tab_size, ash_grapheme *g);
ASH_API size_t ash_measure_prev(const ash_gapbuf *b, size_t off);

ASH_API size_t ash_measure_line_start(const ash_gapbuf *b, size_t off);
ASH_API size_t ash_measure_line_end(const ash_gapbuf *b, size_t off);
ASH_API int    ash_measure_line_index(const ash_gapbuf *b, size_t off);
ASH_API size_t ash_measure_line_at(const ash_gapbuf *b, int line);

ASH_API int    ash_measure_col(const ash_gapbuf *b, size_t line_start,
                               size_t off, int tab_size);
ASH_API size_t ash_measure_col_to_offset(const ash_gapbuf *b, size_t line_start,
                                         int target_col, int tab_size,
                                         int *out_col);

#endif
