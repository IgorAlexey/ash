#include <stdio.h>
#include <string.h>

#include "ash/ui/settings_modal.h"
#include "ash/base/poison.h"

enum { EDIT_WIDTH = 40 };

void ash_settings_modal_init(ash_settings_modal *m, ash_sm_field *fields,
                             int nfields, ash_arena *edit_arena)
{
    memset(m, 0, sizeof *m);
    m->fields = fields;
    m->nfields = nfields;
    m->edit_arena = edit_arena;
}

static void set_commit(ash_settings_modal *m, int idx, const char *v)
{
    size_t n = strlen(v);
    if (n >= sizeof m->commit_value)
        n = sizeof m->commit_value - 1;
    memcpy(m->commit_value, v, n);
    m->commit_value[n] = '\0';
    m->commit = 1;
    m->commit_index = idx;
    m->status = NULL;
}

static const char *cycle(const ash_sm_field *f, const char *cur, int dir)
{
    if (f->noptions <= 0)
        return cur;
    int at = 0;
    if (cur != NULL)
        for (int i = 0; i < f->noptions; i++)
            if (strcmp(f->options[i], cur) == 0) {
                at = i;
                break;
            }
    int step = dir < 0 ? f->noptions - 1 : 1;
    return f->options[(at + step) % f->noptions];
}

static void enter_edit(ash_settings_modal *m, int idx)
{
    m->editing = 1;
    m->edit_index = idx;
    ash_textarea_init(&m->edit, m->edit_arena, EDIT_WIDTH, 0);
    const char *v = m->fields[idx].value;
    if (v != NULL)
        ash_textarea_set(&m->edit, v, strlen(v));
}

static void commit_edit(ash_settings_modal *m)
{
    ash_buf b;
    ash_buf_init(&b, m->edit_arena);
    ash_textarea_text(&m->edit, &b);
    char val[512];
    size_t n = b.len < sizeof val - 1 ? b.len : sizeof val - 1;
    memcpy(val, b.data, n);
    while (n > 0 && val[n - 1] == '\n')
        n--;
    val[n] = '\0';
    set_commit(m, m->edit_index, val);
    m->editing = 0;
}

static void draw_list(ash_ctx *c, ash_settings_modal *m)
{
    ash_list_begin(c, "fields");
    for (int i = 0; i < m->nfields; i++) {
        const ash_sm_field *f = &m->fields[i];
        char row[160];
        snprintf(row, sizeof row, "%s: %s", f->label,
                 f->value != NULL ? f->value : "");
        ash_list_sel sel = ash_list_item(c, i == 0, row);
        if (sel != ASH_LIST_ACTIVATED)
            continue;
        if (f->kind == ASH_SM_ENUM)
            set_commit(m, i, cycle(f, f->value, 1));
        else
            enter_edit(m, i);
    }
    ash_list_end(c);
    ash_focus_on_first_present(c);

    (void)ash_checkbox(c, "scope", "Project scope", &m->project_scope);
    ash_label(c, "hint", "Enter change   Tab scope   Esc close");
    if (m->status != NULL)
        ash_label(c, "status", m->status);
}

static void draw_edit(ash_ctx *c, ash_settings_modal *m)
{
    const ash_sm_field *f = &m->fields[m->edit_index];
    char head[128];
    snprintf(head, sizeof head, "Edit %s:", f->label);
    ash_label(c, "editlabel", head);
    ash_textarea_widget(c, "edit", &m->edit);
    ash_steal_focus(c);
    ash_label(c, "edithint", "Enter save   Esc cancel");
}

void ash_settings_modal_draw(ash_ctx *c, ash_settings_modal *m)
{
    ash_modal_begin(c, "settings", "Settings");

    if (m->editing) {
        if (ash_consume_shortcut(c, ASH_TK_ENTER, 0))
            commit_edit(m);
        else if (ash_consume_shortcut(c, ASH_TK_ESCAPE, 0))
            m->editing = 0;
    }

    if (m->editing)
        draw_edit(c, m);
    else
        draw_list(c, m);

    if (ash_modal_end(c) && !m->editing)
        m->closed = 1;
}
