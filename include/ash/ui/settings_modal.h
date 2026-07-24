#ifndef ASH_UI_SETTINGS_MODAL_H
#define ASH_UI_SETTINGS_MODAL_H

#include "ash/base/api.h"
#include "ash/base/arena.h"
#include "ash/ui/textarea.h"
#include "ash/ui/tui.h"

typedef enum ash_sm_kind {
    ASH_SM_TEXT = 0,
    ASH_SM_ENUM
} ash_sm_kind;

typedef struct ash_sm_field {
    const char        *label;
    const char        *value;
    ash_sm_kind        kind;
    const char *const *options;
    int                noptions;
} ash_sm_field;

typedef struct ash_settings_modal {
    ash_sm_field *fields;
    int           nfields;
    ash_arena    *edit_arena;

    int           project_scope;
    int           editing;
    int           edit_index;
    ash_textarea  edit;

    int           closed;
    int           commit;
    int           commit_index;
    char          commit_value[512];
    const char   *status;
} ash_settings_modal;

ASH_API void ash_settings_modal_init(ash_settings_modal *m,
                                     ash_sm_field *fields, int nfields,
                                     ash_arena *edit_arena);

ASH_API void ash_settings_modal_draw(ash_ctx *c, ash_settings_modal *m);

#endif
