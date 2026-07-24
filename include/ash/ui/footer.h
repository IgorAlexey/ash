#ifndef ASH_UI_FOOTER_H
#define ASH_UI_FOOTER_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/fb/fb.h"

enum {
    ASH_FOOTER_NAME_MAX   = 48,
    ASH_FOOTER_BRANCH_MAX = 96,
    ASH_FOOTER_PATH_MAX   = 512,
    ASH_FOOTER_EXT_MAX    = 8,
    ASH_FOOTER_EXT_KEY    = 32,
    ASH_FOOTER_EXT_TEXT   = 64
};

typedef struct ash_footer_ext {
    char key[ASH_FOOTER_EXT_KEY];
    char text[ASH_FOOTER_EXT_TEXT];
} ash_footer_ext;

typedef struct ash_footer {
    char    provider[ASH_FOOTER_NAME_MAX];
    char    model[ASH_FOOTER_NAME_MAX];

    int64_t context_used;
    int64_t context_window;
    int64_t in_tokens;
    int64_t out_tokens;
    double  cost_usd;

    int  have_branch;
    char branch[ASH_FOOTER_BRANCH_MAX];

    int     git_ready;
    int     has_polled;
    char    head_path[ASH_FOOTER_PATH_MAX];
    int64_t head_mtime_ns;
    int64_t last_poll_ms;
    int64_t debounce_ms;

    ash_footer_ext ext[ASH_FOOTER_EXT_MAX];
    int            ext_count;

    ash_style style;
} ash_footer;

ASH_API void ash_footer_init(ash_footer *f);
ASH_API void ash_footer_set_provider(ash_footer *f, const char *provider,
                                     const char *model);
ASH_API void ash_footer_set_context(ash_footer *f, int64_t used,
                                     int64_t window);
ASH_API void ash_footer_add_usage(ash_footer *f, int64_t in_delta,
                                   int64_t out_delta, double cost_delta);
ASH_API void ash_footer_set_status(ash_footer *f, const char *key,
                                    const char *text);
ASH_API void ash_footer_clear_status(ash_footer *f);

ASH_API int  ash_footer_git_init(ash_footer *f, const char *cwd);
ASH_API int  ash_footer_git_poll(ash_footer *f, int64_t now_ms);

ASH_API void ash_footer_render(const ash_footer *f, ash_fb *fb, ash_rect r);

ASH_API size_t ash_footer_fmt_tokens(int64_t n, char *out, size_t cap);

#endif
