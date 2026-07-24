#ifndef ASH_UI_DIALOG_H
#define ASH_UI_DIALOG_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/fb/fb.h"
#include "ash/term/input.h"
#include "ash/ui/textarea.h"
#include "ash/ui/tui.h"

typedef enum ash_dialog_result {
    ASH_DIALOG_NONE = 0,
    ASH_DIALOG_ACCEPT,
    ASH_DIALOG_REJECT
} ash_dialog_result;

ASH_API ash_dialog_result ash_dialog_confirm(ash_ctx *c, const char *title,
                                            const char *message,
                                            const char *accept,
                                            const char *reject);

ASH_API ash_dialog_result ash_dialog_prompt(ash_ctx *c, const char *title,
                                           const char *message,
                                           ash_textarea *input,
                                           const char *accept,
                                           const char *reject);

enum {
    ASH_PICKER_NONE   = -1,
    ASH_PICKER_CANCEL = -2
};

typedef struct ash_picker {
    ash_textarea       query;
    const char *const *items;
    int                nitems;
    ash_arena         *scratch;
    int                sel;
    int                scroll;
} ash_picker;

ASH_API void ash_picker_init(ash_picker *pk, ash_arena *scratch,
                             ash_arena *query_arena,
                             const char *const *items, int nitems,
                             int query_width);

ASH_API int  ash_picker_handle(ash_picker *pk, const ash_input_event *ev);

ASH_API void ash_picker_render(ash_picker *pk, ash_fb *fb, ash_rect rect);

#endif
