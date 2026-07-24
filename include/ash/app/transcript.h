#ifndef ASH_APP_TRANSCRIPT_H
#define ASH_APP_TRANSCRIPT_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/fb/cell.h"
#include "ash/fb/scrollback.h"

typedef enum ash_ts_kind {
    ASH_TS_INFO,
    ASH_TS_USER,
    ASH_TS_AGENT,
    ASH_TS_TOOL_HEAD,
    ASH_TS_TOOL_OUT,
    ASH_TS_ERROR
} ash_ts_kind;

typedef struct ash_ts_block {
    ash_ts_kind kind;
    ash_buf     text;
    ash_buf     title;
    uint64_t    id;
    uint64_t    proj_seq;
} ash_ts_block;

typedef struct ash_transcript {
    ash_arena    *arena;
    ash_ts_block *blocks;
    size_t        count;
    size_t        cap;
    uint64_t      next_id;
} ash_transcript;

typedef struct ash_theme {
    ash_style text;
    ash_style user_msg;
    ash_style tool_head;
    ash_style tool_out;
    ash_style marker;
    ash_style error;
} ash_theme;

typedef struct ash_ts_opts {
    int              tools_expanded;
    int              pad_x;
    const ash_theme *theme;
} ash_ts_opts;

enum { ASH_TS_TAIL_LINES = 20 };

ASH_API void     ash_ts_init(ash_transcript *t, ash_arena *a);
ASH_API uint64_t ash_ts_append(ash_transcript *t, ash_ts_kind kind,
                               const char *text, size_t text_len,
                               const char *title, size_t title_len);
ASH_API void     ash_ts_append_stream(ash_transcript *t, const char *text,
                                      size_t n);
ASH_API size_t              ash_ts_count(const ash_transcript *t);
ASH_API const ash_ts_block *ash_ts_get(const ash_transcript *t, size_t i);

ASH_API void ash_ts_project(ash_transcript *t, ash_scrollback *sb, int width,
                            const ash_ts_opts *o);
ASH_API void ash_ts_project_tail(ash_transcript *t, ash_scrollback *sb,
                                 int width, const ash_ts_opts *o);

ASH_API size_t   ash_ts_block_at_seq(const ash_transcript *t, uint64_t seq);

ASH_API const ash_theme *ash_theme_dark(void);
ASH_API const ash_theme *ash_theme_light(void);
ASH_API const ash_theme *ash_theme_select(const char *name);

#endif
