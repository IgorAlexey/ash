#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include "ash/ai/provider.h"
#include "ash/app/agent.h"
#include "ash/app/bang.h"
#include "ash/app/loop.h"
#include "ash/app/queue.h"
#include "ash/app/select.h"
#include "ash/app/transcript.h"
#include "ash/app/tokensrc.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
#include "ash/core/auth.h"
#include "ash/core/config.h"
#include "ash/core/coro.h"
#include "ash/core/oauth.h"
#include "ash/core/proc.h"
#include "ash/core/session.h"
#include "ash/core/settings.h"
#include "ash/edit/diffview.h"
#include "ash/edit/editor.h"
#include "ash/fb/fb.h"
#include "ash/fb/scrollback.h"
#include "ash/term/input.h"
#include "ash/term/screen.h"
#include "ash/ui/footer.h"
#include "ash/ui/keys.h"
#include "ash/ui/settings_modal.h"
#include "ash/ui/textarea.h"
#include "ash/ui/tui.h"
#include "ash/tools/tools.h"
#include "ash/base/poison.h"

enum { LOOP_STACK = 1u << 18 };
enum { FEED_EVENTS = 32 };

enum { SB_CELL_BUDGET = 8u << 20 };
enum { SB_MAX_LINES = 8192 };

#define ASH_CMDS \
    "  /help     list the commands\n" \
    "  /login    sign in with a Claude subscription (OAuth)\n" \
    "  /settings open the settings editor\n" \
    "  /clear    forget the conversation and start fresh\n" \
    "  /quit     exit (Ctrl-D also works)\n" \
    "  !<cmd>    run a shell command\n"

static const char INTRO[] =
    "ash - a terminal agent that runs shell commands for you.\n"
    "\n"
    "  enter          to send (shift+enter for a newline)\n"
    "  ctrl+c         to interrupt (copies instead when text is selected)\n"
    "  ctrl+d         to quit\n"
    "  ctrl+o         to expand or collapse tool output\n"
    "  ctrl+z         to undo (ctrl+shift+z to redo)\n"
    "  ctrl+shift+c   to copy (ctrl+shift+v to paste)\n"
    "  pgup/pgdn      to scroll the transcript (mouse wheel works too)\n"
    "  mouse drag     to select text (esc clears the selection)\n"
    "  /              for commands\n"
    "  !<cmd>         to run a shell command yourself\n"
    "\n"
    ASH_CMDS
    "\n"
    "ash can explain its own features. Ask it how to use or configure ash.\n";

static const char HELP[] = "commands:\n" ASH_CMDS;

static const char HINT[] = "/help  /login  /settings  /clear  /quit\n";

struct ash_loop {
    ash_co           co;
    ash_mem          mem;
    ash_provider_cfg pcfg;

    ash_auth        *auth;
    ash_arena       *store_arena;
    const char      *provider_name;
    ash_token_src    login_ctx;

    int              in_fd;
    int              out_fd;

    ash_input        dfa;
    ash_textarea     ta;
    uint8_t          inbuf[4096];
    size_t           inpos;
    size_t           inlen;
    ash_input_event  evs[FEED_EVENTS];
    uint32_t         nevs;
    uint32_t         evcur;
    int              stalled;
    int              in_eof;

    ash_provider_stream *stream;
    int              running;

    ash_proc        *proc;
    ash_buf         *tool_out;
    int              tool_done;
    int              child_exited;
    int              tool_truncated;

    ash_agent        agent;
    int              msgs_this_turn;

    ash_log          log;
    int              has_log;

    ash_buf          line;

    ash_footer       footer;

    int              tui;
    int              modal_open;
    ash_arena        ui;
    ash_fb           fb;
    ash_scrollback   sb;
    ash_buf          frame;
    ash_transcript   ts;
    ash_arena        ts_arena;
    const ash_theme *theme;
    int              tools_expanded;
    int              open_kind;
    int              last_sb_rows;
    int              wrap_w;
    int              ta_live;
    ash_style        st_text;
    ash_style        st_border;
    ash_buf          rule;

    ash_selection    sel;
    ash_sb_viewport  vp;
    int              trans_h;
    int64_t          click_ms;
    int              click_n;
    uint64_t         click_seq;
    int              click_col;

    ash_queue        queue;
    ash_arena        q_arena;
    int              drain_suspended;

    ash_arena        modal_arena;
    int              has_modal_arena;
};

enum { SEL_CLICK_MS = 400 };
enum { QUEUE_ROWS_MAX = 6 };

static int64_t mono_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void capture_usage(struct ash_loop *L, const ash_ai_usage *u)
{
    const ash_model_info *mi = ash_model_find(L->pcfg.model);
    double cost = mi != NULL ? ash_model_cost_usd(mi, u) : 0.0;
    ash_footer_add_usage(&L->footer, u->input_tokens, u->output_tokens, cost);
    int64_t used = u->input_tokens + u->cache_read_input_tokens +
                   u->cache_creation_input_tokens;
    ash_footer_set_context(&L->footer, used,
                           mi != NULL ? mi->context_window : 0);
}

static void write_bytes(struct ash_loop *L, const void *p, size_t n)
{
    const char *b = p;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(L->out_fd, b + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        if (w == 0)
            return;
        off += (size_t)w;
    }
}

static void write_str(struct ash_loop *L, const char *s)
{
    write_bytes(L, s, strlen(s));
}

static void term_size(const struct ash_loop *L, int *w, int *h)
{
    struct winsize ws;
    if (ioctl(L->out_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        *w = ws.ws_col;
        *h = ws.ws_row;
        return;
    }
    *w = 80;
    *h = 24;
}

static ash_ts_opts ui_opts(struct ash_loop *L)
{
    ash_ts_opts o = { L->tools_expanded, 2, L->theme };
    return o;
}

static int ui_width(struct ash_loop *L)
{
    return L->wrap_w > 0 ? L->wrap_w : 80;
}

static void ui_project_tail(struct ash_loop *L)
{
    ash_ts_opts o = ui_opts(L);
    ash_ts_project_tail(&L->ts, &L->sb, ui_width(L), &o);
}

static void ui_block(struct ash_loop *L, ash_ts_kind kind,
                     const char *s, size_t n)
{
    if (!L->tui) {
        write_bytes(L, s, n);
        return;
    }
    L->open_kind = -1;
    ash_ts_append(&L->ts, kind, s, n, NULL, 0);
    ui_project_tail(L);
}

static void ui_block_str(struct ash_loop *L, ash_ts_kind kind, const char *s)
{
    ui_block(L, kind, s, strlen(s));
}

static void ui_stream(struct ash_loop *L, ash_ts_kind kind,
                      const char *s, size_t n)
{
    if (!L->tui) {
        write_bytes(L, s, n);
        return;
    }
    if (L->open_kind == (int)kind)
        ash_ts_append_stream(&L->ts, s, n);
    else {
        ash_ts_append(&L->ts, kind, s, n, NULL, 0);
        L->open_kind = (int)kind;
    }
    ui_project_tail(L);
}

static void ui_tool_head(struct ash_loop *L, const char *cmd)
{
    if (!L->tui) {
        write_str(L, "[running] ");
        write_str(L, cmd);
        write_str(L, "\n");
        return;
    }
    L->open_kind = -1;
    ash_ts_append(&L->ts, ASH_TS_TOOL_HEAD, NULL, 0, cmd, strlen(cmd));
    ui_project_tail(L);
}

static void ui_user(struct ash_loop *L, const char *s, size_t n)
{
    if (!L->tui) {
        write_str(L, "\n");
        return;
    }
    L->open_kind = -1;
    ash_ts_append(&L->ts, ASH_TS_USER, s, n, NULL, 0);
    ui_project_tail(L);
}

static void reproject_full(struct ash_loop *L, int width)
{
    if (!L->tui)
        return;
    ash_sel_clear(&L->sel);
    int following = ash_sb_is_following(&L->sb);
    int vp = L->last_sb_rows > 0 ? L->last_sb_rows : 1;
    uint64_t top = ash_sb_view_top(&L->sb, vp);
    size_t anchor = ash_ts_block_at_seq(&L->ts, top);
    ash_ts_opts o = ui_opts(L);
    ash_ts_project(&L->ts, &L->sb, width, &o);
    if (following) {
        ash_sb_follow(&L->sb);
        return;
    }
    const ash_ts_block *b = ash_ts_get(&L->ts, anchor);
    if (b != NULL && b->proj_seq != UINT64_MAX)
        ash_sb_scroll_to(&L->sb, b->proj_seq);
}

static void draw_cells(ash_fb *fb, int y, const ash_cell *cells, size_t n)
{
    int x = 0;
    for (size_t i = 0; i < n && x < fb->w; i++) {
        const ash_cell *c = &cells[i];
        if (c->width == 0 || c->len == 0)
            continue;
        ash_style st = { c->fg, c->bg, c->attr };
        ash_fb_put_text(fb, x, y, st, c->bytes, c->len);
        x += c->width;
    }
}

static void tui_draw(struct ash_loop *L)
{
    if (!L->tui || L->modal_open)
        return;

    int w, h;
    term_size(L, &w, &h);
    if (w != L->wrap_w && ash_ts_count(&L->ts) > 0)
        reproject_full(L, w);
    L->wrap_w = w;
    ash_fb_begin(&L->fb, w, h);

    int content_h = 1;
    if (L->ta_live) {
        content_h = ash_textarea_height(&L->ta);
        if (content_h < 1)
            content_h = 1;
        if (content_h > h / 3 + 1)
            content_h = h / 3 + 1;
    }
    int qn = 0;
    if (L->ta_live) {
        qn = (int)ash_queue_count(&L->queue);
        if (qn > QUEUE_ROWS_MAX)
            qn = QUEUE_ROWS_MAX;
    }
    int input_h = content_h + (L->ta_live ? 2 : 0) + qn;
    int foot_h = h >= 2 ? 1 : 0;
    int trans_h = h - input_h - foot_h;
    if (trans_h < 0)
        trans_h = 0;

    int sb_rows = trans_h;
    L->last_sb_rows = sb_rows;

    L->trans_h = trans_h;
    L->vp.cols = w;
    L->vp.top_seq = ash_sb_newest(&L->sb);
    L->vp.top_row = 0;
    L->vp.rows = 0;

    int y = 0;
    if (sb_rows > 0) {
        uint64_t vtop = ash_sb_view_top(&L->sb, sb_rows);
        uint64_t newest = ash_sb_newest(&L->sb);
        size_t have = ash_sb_count(&L->sb);
        int fill = have >= (size_t)sb_rows ? sb_rows
                                           : (int)have;
        L->vp.top_seq = vtop;
        L->vp.top_row = sb_rows - fill;
        L->vp.rows = fill;
        y = sb_rows - fill;
        uint64_t seq = vtop;
        for (; y < sb_rows && seq <= newest; seq++) {
            const ash_cell *cells;
            size_t n;
            if (!ash_sb_line_at(&L->sb, seq, &cells, &n))
                break;
            draw_cells(&L->fb, y, cells, n);
            y++;
        }
    }

    ash_sel_apply(&L->sel, &L->sb, &L->vp, &L->fb, ash_selection_style());

    if (L->ta_live) {
        if (qn > 0) {
            ash_rect q_r = { 0, trans_h, w, qn };
            ash_style q_deco = L->theme->marker;
            ash_style q_text = L->theme->marker;
            q_text.attr = (uint16_t)(q_text.attr | ASH_ATTR_CONTENT);
            ash_queue_render(&L->queue, &L->fb, q_r, q_text, q_deco);
        }
        ash_rect input_r = { 0, trans_h + qn, w, input_h - qn };
        ash_input_bar_render(&L->ta, &L->fb, input_r, L->st_text,
                             L->st_border, &L->rule);
    } else {
        ash_fb_put_text(&L->fb, 0, trans_h, L->theme->tool_out,
                        "\xe2\x80\xa6 working \xc2\xb7 Ctrl-C cancels",
                        strlen("\xe2\x80\xa6 working \xc2\xb7 Ctrl-C cancels"));
        ash_fb_hide_cursor(&L->fb);
    }
    if (foot_h)
        ash_footer_render(&L->footer, &L->fb, (ash_rect){ 0, h - 1, w, 1 });

    L->frame.len = 0;
    ash_fb_flip(&L->fb, &L->frame);
    if (L->frame.len) {
        ash_screen_frame_begin();
        ash_screen_write(L->frame.data, L->frame.len);
        ash_screen_frame_end();
    }
}

static void handle_mouse(struct ash_loop *L, const ash_input_event *ev)
{
    if (ev->maction == ASH_MOUSE_WHEEL_UP) {
        ash_sb_scroll_by(&L->sb, -3, L->last_sb_rows);
        return;
    }
    if (ev->maction == ASH_MOUSE_WHEEL_DOWN) {
        ash_sb_scroll_by(&L->sb, 3, L->last_sb_rows);
        return;
    }
    if (ev->mbutton != ASH_MB_LEFT)
        return;

    int row = ev->my;
    int col = ev->mx;
    uint64_t seq;
    int c;

    if (ev->maction == ASH_MOUSE_PRESS) {
        if (row >= L->trans_h ||
            !ash_sel_hittest(&L->sb, &L->vp, row, col, &seq, &c)) {
            ash_sel_clear(&L->sel);
            L->click_n = 0;
            return;
        }
        int64_t now = mono_ms();
        int same = L->click_n > 0 && now - L->click_ms <= SEL_CLICK_MS &&
                   seq == L->click_seq && c == L->click_col;
        L->click_n = same && L->click_n < 3 ? L->click_n + 1 : 1;
        L->click_ms = now;
        L->click_seq = seq;
        L->click_col = c;
        if (L->click_n == 1)
            ash_sel_set(&L->sel, seq, c);
        else if (L->click_n == 2)
            ash_sel_word(&L->sel, &L->sb, seq, c);
        else
            ash_sel_line(&L->sel, &L->sb, seq, c);
        return;
    }
    if (ev->maction == ASH_MOUSE_DRAG) {
        if (L->sel.active && L->click_n <= 1 &&
            ash_sel_hittest(&L->sb, &L->vp, row, col, &seq, &c))
            ash_sel_extend(&L->sel, seq, c);
        return;
    }
    if (ev->maction == ASH_MOUSE_RELEASE) {
        if (L->click_n <= 1 && ash_sel_empty(&L->sel))
            ash_sel_clear(&L->sel);
        return;
    }
}

static int handle_page_key(struct ash_loop *L, const ash_input_event *ev)
{
    if (!L->tui || ev->kind != ASH_EV_KEY)
        return 0;
    if (ev->key != ASH_KEY_PGUP && ev->key != ASH_KEY_PGDN)
        return 0;
    int64_t page = L->last_sb_rows > 1 ? L->last_sb_rows - 1 : 1;
    ash_sb_scroll_by(&L->sb, ev->key == ASH_KEY_PGUP ? -page : page,
                     L->last_sb_rows);
    return 1;
}

static int clip_emit(struct ash_loop *L, ash_buf *text)
{
    int truncated = 0;
    size_t cap = 1u << 20;
    if (text->len > cap) {
        while (cap > 0 && (text->data[cap] & 0xC0) == 0x80)
            cap--;
        text->len = cap;
        truncated = 1;
    }

    ash_buf b64;
    ash_buf_init(&b64, &L->mem.scratch);
    ash_base64_encode(text->data, text->len, &b64);
    ash_screen_clipboard(b64.data, b64.len);
    return truncated;
}

static void do_copy(struct ash_loop *L)
{
    ash_arena_mark mk = ash_arena_mark_get(&L->mem.scratch);
    ash_buf text;
    ash_buf_init(&text, &L->mem.scratch);
    if (!ash_sel_empty(&L->sel))
        ash_sel_extract(&L->sel, &L->sb, &text);
    else if (L->ta_live)
        ash_textarea_selection(&L->ta, &text);

    int truncated = text.len ? clip_emit(L, &text) : 0;
    ash_arena_rewind(&L->mem.scratch, mk);

    if (truncated)
        ui_block_str(L, ASH_TS_INFO, "[selection truncated to 1 MiB]\n");
}

static void do_cut(struct ash_loop *L)
{
    if (!L->ta_live || !ash_textarea_has_selection(&L->ta))
        return;

    ash_arena_mark mk = ash_arena_mark_get(&L->mem.scratch);
    ash_buf text;
    ash_buf_init(&text, &L->mem.scratch);
    ash_textarea_selection(&L->ta, &text);
    int truncated = text.len ? clip_emit(L, &text) : 0;
    ash_arena_rewind(&L->mem.scratch, mk);

    ash_textarea_delete_selection(&L->ta);

    if (truncated)
        ui_block_str(L, ASH_TS_INFO, "[selection truncated to 1 MiB]\n");
}

static int in_pending(const struct ash_loop *L)
{
    return L->evcur < L->nevs || L->inpos < L->inlen;
}

static void read_input(struct ash_loop *L)
{
    if (in_pending(L))
        return;

    ssize_t n;
    do {
        n = read(L->in_fd, L->inbuf, sizeof L->inbuf);
    } while (n < 0 && errno == EINTR);
    L->inpos = 0;
    L->nevs = 0;
    L->evcur = 0;
    L->stalled = 0;
    if (n <= 0) {
        L->inlen = 0;
        if (n == 0)
            L->in_eof = 1;
        return;
    }
    L->inlen = (size_t)n;
}

static const ash_input_event *in_next(struct ash_loop *L)
{
    for (;;) {
        if (L->evcur < L->nevs)
            return &L->evs[L->evcur++];
        if (L->inpos >= L->inlen)
            return NULL;

        uint32_t consumed = 0, produced = 0;
        if (ash_input_feed(&L->dfa, L->inbuf + L->inpos,
                           (uint32_t)(L->inlen - L->inpos), L->evs, FEED_EVENTS,
                           &consumed, &produced) != ASH_OK) {
            L->nevs = 0;
            L->evcur = 0;
            L->inpos = L->inlen;
            L->stalled = 0;
            return NULL;
        }
        L->nevs = produced;
        L->evcur = 0;
        if (consumed == 0 && (produced == 0 || L->stalled)) {
            L->inpos = L->inlen;
            L->stalled = 0;
            continue;
        }
        L->stalled = consumed == 0;
        L->inpos += consumed;
    }
}

static int in_drain_saw_cancel(struct ash_loop *L)
{
    int cancel = 0;
    const ash_input_event *ev;
    while ((ev = in_next(L)) != NULL)
        if (ash_key_map(ev).cmd == ASH_EC_CANCEL)
            cancel = 1;
    return cancel;
}

static void queue_submit(struct ash_loop *L)
{
    if (L->ta.len > 0)
        ash_queue_push(&L->queue, (const char *)L->ta.data, L->ta.len);
    ash_textarea_clear(&L->ta);
}

typedef enum {
    ASH_IN_PROMPT,
    ASH_IN_BUSY,
    ASH_IN_LOGIN
} ash_in_mode;

typedef enum {
    ASH_IN_NONE,
    ASH_IN_SUBMIT,
    ASH_IN_CANCEL,
    ASH_IN_EOF
} ash_in_result;

static ash_in_result input_dispatch(struct ash_loop *L, ash_in_mode mode)
{
    ash_in_result res = ASH_IN_NONE;
    const ash_input_event *ev;
    while ((ev = in_next(L)) != NULL) {
        if (ev->kind == ASH_EV_MOUSE) {
            handle_mouse(L, ev);
            continue;
        }
        if (handle_page_key(L, ev))
            continue;
        if (ev->kind == ASH_EV_KEY && ev->key == 27 && L->sel.active) {
            ash_sel_clear(&L->sel);
            continue;
        }
        if (L->tui && ev->kind == ASH_EV_KEY && ev->key == 'o' &&
            (ev->mods & ASH_MOD_CTRL)) {
            L->tools_expanded = !L->tools_expanded;
            reproject_full(L, ui_width(L));
            continue;
        }

        ash_key k = ash_key_map(ev);
        if (k.cmd == ASH_EC_COPY) {
            do_copy(L);
            continue;
        }
        if (k.cmd == ASH_EC_CUT) {
            do_cut(L);
            continue;
        }
        if (k.cmd == ASH_EC_PASTE)
            continue;
        if (k.cmd == ASH_EC_SUBMIT) {
            if (mode == ASH_IN_BUSY) {
                queue_submit(L);
                continue;
            }
            ash_buf_init(&L->line, &L->mem.turn);
            ash_textarea_text(&L->ta, &L->line);
            if (L->tui)
                ash_textarea_clear(&L->ta);
            else
                L->ta_live = 0;
            if (mode == ASH_IN_LOGIN) {
                write_str(L, "\n");
            } else {
                const char *bc;
                size_t bl;
                if (!ash_bang_split((const char *)L->line.data, L->line.len,
                                    &bc, &bl))
                    ui_user(L, (const char *)L->line.data, L->line.len);
            }
            return ASH_IN_SUBMIT;
        }
        if (k.cmd == ASH_EC_EOF) {
            if (mode == ASH_IN_BUSY || ash_textarea_len(&L->ta) != 0)
                continue;
            return ASH_IN_EOF;
        }
        if (k.cmd == ASH_EC_CANCEL) {
            if (mode == ASH_IN_LOGIN)
                return ASH_IN_CANCEL;
            if (!ash_sel_empty(&L->sel)) {
                do_copy(L);
                ash_sel_clear(&L->sel);
                continue;
            }
            if (mode == ASH_IN_BUSY) {
                res = ASH_IN_CANCEL;
                continue;
            }
            if (ash_textarea_len(&L->ta) == 0)
                return ASH_IN_EOF;
            ash_textarea_clear(&L->ta);
            if (!L->tui) {
                write_str(L, "\n");
                write_str(L, "> ");
            }
            continue;
        }
        if (k.cmd == ASH_EC_INSERT || k.cmd == ASH_EC_PASTE_CHUNK) {
            ash_textarea_insert(&L->ta, k.text, k.len);
            if (!L->tui)
                write_bytes(L, k.text, k.len);
            continue;
        }
        if (k.cmd == ASH_EC_NEWLINE) {
            ash_textarea_insert(&L->ta, "\n", 1);
            if (!L->tui)
                write_str(L, "\n");
            continue;
        }
        if (k.cmd == ASH_EC_BACKSPACE) {
            int at_end = ash_textarea_at_end(&L->ta);
            int w = ash_textarea_backspace(&L->ta);
            if (!L->tui)
                for (int j = 0; at_end && j < w; j++)
                    write_str(L, "\b \b");
            continue;
        }
        ash_textarea_apply(&L->ta, k);
    }
    return res;
}

static int busy_input(struct ash_loop *L)
{
    if (!L->tui)
        return in_drain_saw_cancel(L);
    return input_dispatch(L, ASH_IN_BUSY) == ASH_IN_CANCEL;
}

static const char *arena_dup(ash_arena *a, const char *s, size_t len)
{
    if (s == NULL)
        return NULL;
    char *c = ash_array(a, char, len + 1);
    memcpy(c, s, len);
    c[len] = 0;
    return c;
}

static void disable_log(struct ash_loop *L)
{
    if (!L->has_log)
        return;
    L->has_log = 0;
    ui_block_str(L, ASH_TS_INFO, "session log disabled\n");
}

static void log_msg(struct ash_loop *L, const ash_msg *m)
{
    if (!L->has_log)
        return;
    ash_arena_mark mk = ash_arena_mark_get(&L->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &L->mem.scratch);
    if (m->tool_name != NULL) {
        ash_buf_append_cstr(&b, "tool_use\t");
        ash_buf_append_cstr(&b, m->tool_name);
        ash_buf_append_byte(&b, '\t');
        ash_buf_append_cstr(&b, m->tool_input ? m->tool_input : "{}");
    } else if (m->tool_result != NULL) {
        ash_buf_append_cstr(&b, "tool_result\t");
        ash_buf_append_cstr(&b, m->tool_result);
    } else {
        ash_buf_append_cstr(&b, m->role ? m->role : "user");
        ash_buf_append_byte(&b, '\t');
        ash_buf_append_cstr(&b, m->content ? m->content : "");
    }
    if (b.len > ASH_MAX_PAYLOAD ||
        ash_log_append_turn(&L->log, b.data, (uint32_t)b.len) != ASH_OK)
        disable_log(L);
    ash_arena_rewind(&L->mem.scratch, mk);
}

enum { RL_OK, RL_EOF };

static int read_line(ash_co *co, struct ash_loop *L)
{
    if (!L->tui)
        ash_textarea_init(&L->ta, &L->mem.turn, L->wrap_w > 0 ? L->wrap_w : 80, 0);
    L->ta_live = 1;
    for (;;) {
        if (!in_pending(L))
            ash_co_yield(co, ASH_WAIT_INPUT);
        if (!in_pending(L))
            break;
        ash_in_result r = input_dispatch(L, ASH_IN_PROMPT);
        if (r == ASH_IN_SUBMIT)
            return RL_OK;
        if (r == ASH_IN_EOF)
            break;
    }
    L->ta_live = 0;
    return RL_EOF;
}

static void report_error(struct ash_loop *L, const char *text)
{
    ash_arena_mark mk = ash_arena_mark_get(&L->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &L->mem.scratch);
    ash_buf_append_cstr(&b, "error: ");
    ash_buf_append_cstr(&b, text);
    ash_buf_append_byte(&b, '\n');
    ui_block(L, ASH_TS_ERROR, (const char *)b.data, b.len);
    ash_arena_rewind(&L->mem.scratch, mk);
}

static void loop_emit(void *ud, const ash_agent_event *ev)
{
    struct ash_loop *L = ud;
    switch (ev->kind) {
    case ASH_AGENT_TURN_START:
        L->msgs_this_turn = 0;
        return;
    case ASH_AGENT_MSG_START:
        if (!L->tui && L->msgs_this_turn > 0)
            write_str(L, "\n");
        L->msgs_this_turn++;
        return;
    case ASH_AGENT_TEXT:
        ui_stream(L, ASH_TS_AGENT, ev->text, ev->len);
        return;
    case ASH_AGENT_MSG_END:
        if (!L->tui)
            write_str(L, "\n");
        return;
    case ASH_AGENT_USAGE:
        capture_usage(L, ev->usage);
        return;
    case ASH_AGENT_MSG_APPEND:
        log_msg(L, ev->msg);
        return;
    case ASH_AGENT_ERROR:
        report_error(L, ev->text);
        return;
    case ASH_AGENT_TOOL_START:
    case ASH_AGENT_TOOL_END:
    case ASH_AGENT_TURN_END:
        return;
    }
}

static int loop_pump(void *ud, ash_provider_stream *s)
{
    struct ash_loop *L = ud;
    int canceled = 0;
    L->stream = s;
    L->running = 1;
    while (L->running) {
        ash_co_yield(&L->co, ASH_WAIT_SSE);
        if (in_pending(L) && busy_input(L)) {
            canceled = 1;
            break;
        }
    }
    L->stream = NULL;
    return canceled;
}

static int loop_shell(void *ud, const char *cmd, ash_buf *out)
{
    struct ash_loop *L = ud;
    ui_tool_head(L, cmd);

    const char *argv[] = { "sh", "-c", cmd, NULL };
    ash_proc p;
    if (ash_proc_spawn(&p, argv) != ASH_OK) {
        ash_buf_append_cstr(out, "tool error: ");
        ash_buf_append_cstr(out, ash_errbuf);
        return 0;
    }

    int canceled = 0;
    L->proc = &p;
    L->tool_out = out;
    L->tool_done = 0;
    L->child_exited = 0;
    L->tool_truncated = 0;
    while (!L->tool_done) {
        ash_co_yield(&L->co, ASH_WAIT_TOOL);
        if (in_pending(L) && busy_input(L)) {
            canceled = 1;
            break;
        }
    }
    L->proc = NULL;
    L->tool_out = NULL;

    if (canceled) {
        ash_proc_close(&p);
        return 1;
    }
    if (L->tool_truncated)
        ash_buf_append_cstr(out, ASH_AGENT_TRUNC_MARK);
    int code = 0;
    if (ash_proc_wait(&p, &code) == ASH_OK && code != 0) {
        char note[32];
        int nn = snprintf(note, sizeof note, "\n[exit %d]", code);
        if (nn > 0)
            ash_buf_append(out, note, (size_t)nn);
    }
    ash_proc_close(&p);
    return 0;
}

static void bang_record(struct ash_loop *L, const char *cmd,
                        const ash_buf *out, int canceled)
{
    ash_arena_mark mk = ash_arena_mark_get(&L->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &L->mem.scratch);
    ash_buf_append_cstr(&b, "Ran `");
    ash_buf_append_cstr(&b, cmd);
    ash_buf_append_cstr(&b, "`\n");
    if (out->len > 0) {
        ash_buf_append_cstr(&b, "```\n");
        ash_buf_append(&b, out->data, out->len);
        ash_buf_append_cstr(&b, "\n```");
    } else {
        ash_buf_append_cstr(&b, "(no output)");
    }
    if (canceled)
        ash_buf_append_cstr(&b, "\n\n(command cancelled)");
    ash_agent_user(&L->agent, (const char *)b.data, b.len);
    ash_arena_rewind(&L->mem.scratch, mk);
}

static int run_bang(struct ash_loop *L, const char *cmd, size_t cmd_len)
{
    if (cmd_len == 0) {
        ui_block_str(L, ASH_TS_INFO, "usage: !<command>\n");
        return 0;
    }
    ash_arena_mark mk = ash_arena_mark_get(&L->mem.scratch);
    char *c = ash_array(&L->mem.scratch, char, cmd_len + 1);
    memcpy(c, cmd, cmd_len);
    c[cmd_len] = 0;
    ash_buf out;
    ash_buf_init(&out, &L->mem.turn);
    int canceled = loop_shell(L, c, &out);
    bang_record(L, c, &out, canceled);
    if (canceled)
        ui_block_str(L, ASH_TS_INFO, "[canceled]\n");
    ash_arena_rewind(&L->mem.scratch, mk);
    return canceled;
}

static int line_is(const struct ash_loop *L, const char *cmd)
{
    size_t len = L->line.len;
    while (len > 0 && (L->line.data[len - 1] == ' ' || L->line.data[len - 1] == '\t'))
        len--;
    size_t n = strlen(cmd);
    return len == n && memcmp(L->line.data, cmd, n) == 0;
}

enum { MODAL_SETTLE_MAX = 32 };

static void frame_emit(struct ash_loop *L)
{
    L->frame.len = 0;
    ash_fb_flip(&L->fb, &L->frame);
    if (L->frame.len == 0)
        return;
    ash_screen_frame_begin();
    ash_screen_write(L->frame.data, L->frame.len);
    ash_screen_frame_end();
}

static const ash_input_event *modal_pump_next(struct ash_loop *L)
{
    for (;;) {
        const ash_input_event *ev = in_next(L);
        if (ev != NULL)
            return ev;
        ash_co_yield(&L->co, ASH_WAIT_INPUT);
        if (!in_pending(L))
            return NULL;
    }
}

typedef void (*ash_modal_fn)(ash_ctx *c, void *ud);

static void modal_frame(struct ash_loop *L, ash_tui *t, ash_modal_fn draw,
                        void *ud, const ash_input_event *ev)
{
    int w, h;
    term_size(L, &w, &h);

    int guard = 0;
    do {
        ash_ctx *c = ash_tui_begin(t, w, h, ev);
        if (draw != NULL)
            draw(c, ud);
        ash_tui_end(c);
        ev = NULL;
    } while (ash_tui_settling(t) && ++guard < MODAL_SETTLE_MAX);

    ash_fb_begin(&L->fb, w, h);
    ash_tui_render(t, &L->fb);
    frame_emit(L);
}

static void run_modal(struct ash_loop *L, ash_tui *t, ash_modal_fn draw,
                      void *ud, const int *closed)
{
    L->modal_open = 1;
    modal_frame(L, t, draw, ud, NULL);

    while (!*closed) {
        const ash_input_event *ev = modal_pump_next(L);
        if (ev == NULL)
            break;
        modal_frame(L, t, draw, ud, ev);
    }

    modal_frame(L, t, NULL, NULL, NULL);
    L->modal_open = 0;
}

enum { SETTINGS_MAX_FIELDS = 16 };

struct settings_run {
    ash_arena          edit;
    ash_arena          cfgar;
    ash_config         cfg;
    const ash_setting *schema;
    size_t             nf;
    ash_sm_field       fields[SETTINGS_MAX_FIELDS];
    ash_settings_modal m;
};

static void settings_build_fields(struct settings_run *s)
{
    for (size_t i = 0; i < s->nf; i++) {
        const char *v = ash_settings_value(&s->cfg, &s->schema[i]);
        s->fields[i].label = s->schema[i].label;
        s->fields[i].value = v != NULL ? v : "";
        s->fields[i].kind = s->schema[i].kind == ASH_SETTING_ENUM
                                ? ASH_SM_ENUM : ASH_SM_TEXT;
        s->fields[i].options = s->schema[i].options;
        s->fields[i].noptions = (int)s->schema[i].noptions;
    }
}

static void settings_draw(ash_ctx *c, void *ud)
{
    struct settings_run *s = ud;
    ash_settings_modal_draw(c, &s->m);
    if (!s->m.commit)
        return;

    ash_config_layer layer = s->m.project_scope ? ASH_CFG_PROJECT
                                                : ASH_CFG_GLOBAL;
    ash_status st = ash_settings_write(&s->cfg, layer,
                                       &s->schema[s->m.commit_index],
                                       s->m.commit_value);
    s->m.commit = 0;
    if (st != ASH_OK) {
        s->m.status = "write failed";
        return;
    }
    ash_arena_reset(&s->cfgar);
    if (ash_config_load(&s->cfgar, &s->cfg) == ASH_OK)
        settings_build_fields(s);
    s->m.status = "saved";
}

static void run_settings(struct ash_loop *L)
{
    if (!L->tui) {
        ui_block_str(L, ASH_TS_INFO, "settings unavailable: not a terminal\n");
        return;
    }

    struct settings_run s;
    ash_tui t;
    memset(&s, 0, sizeof s);

    if (ash_arena_create(&s.edit, "settings-edit", 1u << 18) != ASH_OK)
        return;
    if (ash_arena_create(&s.cfgar, "settings-cfg", 1u << 16) != ASH_OK) {
        ash_arena_destroy(&s.edit);
        return;
    }
    if (ash_config_load(&s.cfgar, &s.cfg) != ASH_OK)
        goto out;

    s.schema = ash_settings_schema(&s.nf);
    if (s.nf > SETTINGS_MAX_FIELDS)
        s.nf = SETTINGS_MAX_FIELDS;
    settings_build_fields(&s);
    ash_settings_modal_init(&s.m, s.fields, (int)s.nf, &s.edit);

    if (ash_tui_init(&t) != ASH_OK)
        goto out;
    run_modal(L, &t, settings_draw, &s, &s.m.closed);
    ash_tui_destroy(&t);

out:
    ash_arena_destroy(&s.cfgar);
    ash_arena_destroy(&s.edit);
}

static ash_diffview_theme confirm_theme(const ash_theme *t)
{
    ash_style add = t->tool_head;
    ash_style del = t->error;
    del.attr = (uint16_t)(del.attr | ASH_ATTR_BOLD);
    ash_diffview_theme d = { .context = t->text, .add = add, .del = del,
                             .gutter = t->marker, .header = t->user_msg,
                             .hint = t->user_msg };
    return d;
}

static ash_diffview_action run_diffview(struct ash_loop *L, ash_diffview *dv)
{
    ash_diffview_theme th = confirm_theme(L->theme);
    ash_arena_mark mk = ash_arena_mark_get(&L->modal_arena);
    for (;;) {
        int w, h;
        term_size(L, &w, &h);
        ash_arena_rewind(&L->modal_arena, mk);
        ash_fb_begin(&L->fb, w, h);
        ash_diffview_render(dv, &L->fb, (ash_rect){ 0, 0, w, h }, &th);
        frame_emit(L);

        const ash_input_event *ev = modal_pump_next(L);
        if (ev == NULL)
            return ASH_DIFFVIEW_REJECT;
        if (ev->kind != ASH_EV_KEY)
            continue;
        ash_diffview_action a = ash_diffview_key(dv, ash_key_map(ev), h);
        if (a != ASH_DIFFVIEW_NONE)
            return a;
    }
}

static int run_editor(struct ash_loop *L, ash_editor *ed)
{
    for (;;) {
        int w, h;
        term_size(L, &w, &h);
        ash_fb_begin(&L->fb, w, h);
        ash_editor_render(ed, &L->fb, (ash_rect){ 0, 0, w, h }, L->st_text,
                          ash_selection_style());
        frame_emit(L);

        const ash_input_event *ev = modal_pump_next(L);
        if (ev == NULL)
            return 0;
        if (ev->kind != ASH_EV_KEY)
            continue;
        ash_key k = ash_key_map(ev);
        if (k.cmd == ASH_EC_SUBMIT)
            return 1;
        if (k.cmd == ASH_EC_CANCEL || k.cmd == ASH_EC_EOF)
            return 0;
        ash_editor_apply(ed, k);
    }
}

static int loop_confirm(void *ud, const char *path, const char *old,
                        size_t olen, const char *neu, size_t nlen,
                        const char **edited, size_t *edited_len)
{
    struct ash_loop *L = ud;
    ash_arena_reset(&L->modal_arena);

    ash_diffview dv;
    ash_diffview_init(&dv, &L->modal_arena, path);
    ash_diffview_set(&dv, old, olen, neu, nlen);
    ash_arena_mark mk = ash_arena_mark_get(&L->modal_arena);

    L->modal_open = 1;
    int accepted = 0;
    for (;;) {
        ash_arena_rewind(&L->modal_arena, mk);
        ash_diffview_action a = run_diffview(L, &dv);
        if (a == ASH_DIFFVIEW_ACCEPT) {
            *edited = neu;
            *edited_len = nlen;
            accepted = 1;
            break;
        }
        if (a != ASH_DIFFVIEW_EDIT)
            break;

        ash_editor ed;
        ash_editor_init(&ed, &L->modal_arena);
        ash_editor_set_text(&ed, neu, nlen);
        if (!run_editor(L, &ed))
            continue;
        ash_buf text;
        ash_buf_init(&text, &L->mem.turn);
        ash_editor_text(&ed, &text);
        *edited = text.len > 0 ? (const char *)text.data : "";
        *edited_len = text.len;
        accepted = 1;
        break;
    }
    L->modal_open = 0;
    return accepted;
}

enum { LOGIN_OK, LOGIN_EOF, LOGIN_CANCEL };

static int login_read(ash_co *co, struct ash_loop *L)
{
    if (L->tui)
        ash_textarea_clear(&L->ta);
    else
        ash_textarea_init(&L->ta, &L->mem.turn, L->wrap_w > 0 ? L->wrap_w : 80, 0);
    L->ta_live = 1;
    for (;;) {
        if (!in_pending(L))
            ash_co_yield(co, ASH_WAIT_INPUT);
        if (!in_pending(L))
            break;
        ash_in_result r = input_dispatch(L, ASH_IN_LOGIN);
        if (r == ASH_IN_SUBMIT)
            return LOGIN_OK;
        if (r == ASH_IN_CANCEL) {
            L->ta_live = 0;
            return LOGIN_CANCEL;
        }
        if (r == ASH_IN_EOF)
            break;
    }
    L->ta_live = 0;
    return LOGIN_EOF;
}

static void login_split(const ash_buf *line, ash_arena *a, const char **code,
                        const char **state)
{
    const char *p = (const char *)line->data;
    size_t n = line->len;
    while (n > 0 && (p[n - 1] == '\n' || p[n - 1] == '\r' || p[n - 1] == ' ' ||
                     p[n - 1] == '\t'))
        n--;
    size_t hash = n;
    for (size_t i = 0; i < n; i++)
        if (p[i] == '#') {
            hash = i;
            break;
        }
    char *c = ash_array(a, char, hash + 1);
    if (hash)
        memcpy(c, p, hash);
    c[hash] = '\0';
    *code = c;
    if (hash < n) {
        size_t sl = n - hash - 1;
        char *s = ash_array(a, char, sl + 1);
        if (sl)
            memcpy(s, p + hash + 1, sl);
        s[sl] = '\0';
        *state = s;
    } else {
        *state = "";
    }
}

static void run_login(ash_co *co, struct ash_loop *L)
{
    if (L->auth == NULL || L->provider_name == NULL) {
        ui_block_str(L, ASH_TS_INFO, "login unavailable: no credential store\n");
        return;
    }
    if (L->auth->arena == NULL) {
        if (L->store_arena == NULL ||
            ash_auth_load(L->store_arena, NULL, L->auth) != ASH_OK) {
            ui_block_str(L, ASH_TS_INFO, "login unavailable: could not open store\n");
            return;
        }
    }
    ash_arena la;
    if (ash_arena_create(&la, "login", 1u << 16) != ASH_OK) {
        ui_block_str(L, ASH_TS_INFO, "login: out of memory\n");
        return;
    }

    ash_oauth_pkce pkce;
    if (ash_oauth_pkce_begin(&la, &pkce) != ASH_OK) {
        ui_block_str(L, ASH_TS_INFO, "login: could not start PKCE\n");
        ash_arena_destroy(&la);
        return;
    }

    ui_block_str(L, ASH_TS_INFO,
                 "Open this URL in your browser to authorize ash:\n");
    {
        ash_arena_mark mk = ash_arena_mark_get(&L->mem.scratch);
        ash_buf u;
        ash_buf_init(&u, &L->mem.scratch);
        ash_buf_append_cstr(&u, pkce.url);
        ash_buf_append_byte(&u, '\n');
        ui_block(L, ASH_TS_INFO, (const char *)u.data, u.len);
        ash_arena_rewind(&L->mem.scratch, mk);
    }

    const char *argv[] = { "xdg-open", pkce.url, NULL };
    ash_proc xp;
    if (ash_proc_spawn(&xp, argv) == ASH_OK) {
        ASH_IGNORE(ash_proc_wait(&xp, NULL));
        ash_proc_close(&xp);
    }

    ui_block_str(L, ASH_TS_INFO,
                 "Then paste the code shown (code#state) and press Enter. "
                 "Esc cancels.\n");
    if (!L->tui)
        write_str(L, "> ");

    if (login_read(co, L) != LOGIN_OK) {
        ui_block_str(L, ASH_TS_INFO, "login canceled\n");
        ash_arena_destroy(&la);
        return;
    }

    const char *code = NULL;
    const char *state = NULL;
    login_split(&L->line, &la, &code, &state);

    if (!ash_oauth_state_ok(state, pkce.state)) {
        ui_block_str(L, ASH_TS_INFO, "login aborted: state does not match\n");
        ash_arena_destroy(&la);
        return;
    }

    ash_oauth_http http = ash_oauth_default_http();
    ash_oauth_token tok;
    if (ash_oauth_exchange(&http, &la, pkce.verifier, code, state, &tok)
        != ASH_OK) {
        ui_block_str(L, ASH_TS_INFO, "login failed: could not exchange the code\n");
        ash_arena_destroy(&la);
        return;
    }

    ash_oauth stored = { .access = tok.access, .refresh = tok.refresh,
                         .expires = tok.expires };
    if (ash_auth_set_oauth(L->auth, L->provider_name, &stored) != ASH_OK) {
        ui_block_str(L, ASH_TS_INFO, "login failed: could not save credentials\n");
        ash_arena_destroy(&la);
        return;
    }

    L->login_ctx.auth = L->auth;
    L->login_ctx.provider = L->provider_name;
    L->pcfg.oauth_token = ash_token_src_get;
    L->pcfg.oauth_ctx = &L->login_ctx;
    L->pcfg.api_key = NULL;

    {
        ash_arena_mark mk = ash_arena_mark_get(&L->mem.scratch);
        ash_buf s;
        ash_buf_init(&s, &L->mem.scratch);
        ash_buf_append_cstr(&s, "signed in");
        if (tok.account != NULL) {
            ash_buf_append_cstr(&s, " as ");
            ash_buf_append_cstr(&s, tok.account);
        }
        ash_buf_append_byte(&s, '\n');
        ui_block(L, ASH_TS_INFO, (const char *)s.data, s.len);
        ash_arena_rewind(&L->mem.scratch, mk);
    }
    ash_arena_destroy(&la);
}

static ash_status loop_fn(ash_co *co, void *ud)
{
    struct ash_loop *L = ud;
    ui_block_str(L, ASH_TS_INFO, INTRO);

    for (;;) {
        ash_footer_git_poll(&L->footer, mono_ms());
        ash_arena_reset(&L->mem.turn);
        ash_buf_init(&L->line, &L->mem.turn);
        if (!L->tui) {
            write_str(L, "\n");
            write_str(L, HINT);
            write_str(L, "> ");
        }

        if (L->tui && !L->drain_suspended && ash_queue_count(&L->queue) > 0) {
            const char *qs = NULL;
            size_t qlen = 0;
            ash_queue_pop(&L->queue, &qs, &qlen);
            ash_buf_init(&L->line, &L->mem.turn);
            ash_buf_append(&L->line, qs, qlen);
            const char *qbc;
            size_t qbl;
            if (!ash_bang_split((const char *)L->line.data, L->line.len,
                                &qbc, &qbl))
                ui_user(L, (const char *)L->line.data, L->line.len);
        } else {
            if (read_line(co, L) == RL_EOF)
                break;
            L->drain_suspended = 0;
            if (L->line.len == 0)
                continue;
        }

        const char *bcmd;
        size_t bcmd_len;
        if (ash_bang_split((const char *)L->line.data, L->line.len, &bcmd,
                           &bcmd_len)) {
            if (run_bang(L, bcmd, bcmd_len))
                L->drain_suspended = 1;
            continue;
        }

        if (L->line.data[0] == '/') {
            if (line_is(L, "/help")) {
                ui_block_str(L, ASH_TS_INFO, HELP);
            } else if (line_is(L, "/login")) {
                run_login(co, L);
            } else if (line_is(L, "/settings")) {
                run_settings(L);
            } else if (line_is(L, "/clear")) {
                ash_agent_reset(&L->agent);
                if (L->has_log && ash_log_clear(&L->log) != ASH_OK)
                    disable_log(L);
                if (L->tui) {
                    ash_sel_clear(&L->sel);
                    ash_arena_reset(&L->ts_arena);
                    ash_ts_init(&L->ts, &L->ts_arena);
                    ash_sb_reset(&L->sb);
                    ash_queue_clear(&L->queue);
                    L->drain_suspended = 0;
                    L->open_kind = -1;
                }
                ui_block_str(L, ASH_TS_INFO, "conversation cleared\n");
            } else if (line_is(L, "/quit") || line_is(L, "/exit")) {
                break;
            } else {
                ui_block_str(L, ASH_TS_INFO, "unknown command; /help lists them\n");
            }
            continue;
        }

        ash_agent_user(&L->agent, (const char *)L->line.data, L->line.len);
        if (ash_agent_run(&L->agent) == ASH_AGENT_ABORTED) {
            L->drain_suspended = 1;
            ui_block_str(L, ASH_TS_INFO, "[canceled]\n");
        }
    }

    if (!L->tui)
        write_str(L, "\nbye\n");
    return ASH_OK;
}

static ash_status loop_run(struct ash_loop *L)
{
    ash_wait w = ash_co_resume(&L->co);
    while (L->co.state != ASH_CO_DEAD) {
        tui_draw(L);
        if (w == ASH_WAIT_INPUT) {
            struct pollfd pfd = { .fd = L->in_fd, .events = POLLIN, .revents = 0 };
            int r;
            do {
                r = poll(&pfd, 1, -1);
            } while (r < 0 && errno == EINTR);
            read_input(L);
        } else if (w == ASH_WAIT_SSE) {
            int ready = 0;
            int wfd = L->in_eof ? -1 : L->in_fd;
            ash_status st = ash_provider_wait(L->stream, wfd, 1000, &ready);
            if (st == ASH_OK)
                st = ash_provider_pump(L->stream, &L->running);
            if (st != ASH_OK)
                L->running = 0;
            if (ready)
                read_input(L);
        } else if (w == ASH_WAIT_TOOL) {
            struct pollfd pfds[3];
            nfds_t nfd = 0;
            int slot_out = (int)nfd;
            pfds[nfd++] = (struct pollfd){ .fd = ash_proc_out_fd(L->proc),
                                           .events = POLLIN, .revents = 0 };
            int slot_pid = -1;
            if (!L->child_exited) {
                slot_pid = (int)nfd;
                pfds[nfd++] = (struct pollfd){ .fd = ash_proc_pidfd(L->proc),
                                               .events = POLLIN, .revents = 0 };
            }
            int slot_in = -1;
            if (!L->in_eof) {
                slot_in = (int)nfd;
                pfds[nfd++] = (struct pollfd){ .fd = L->in_fd, .events = POLLIN,
                                               .revents = 0 };
            }
            int r;
            do {
                r = poll(pfds, nfd, L->child_exited ? 200 : -1);
            } while (r < 0 && errno == EINTR);
            if (slot_pid >= 0 && (pfds[slot_pid].revents & POLLIN))
                L->child_exited = 1;
            if (pfds[slot_out].revents & (POLLIN | POLLHUP)) {
                uint8_t tb[4096];
                ssize_t n = read(ash_proc_out_fd(L->proc), tb, sizeof tb);
                if (n > 0) {
                    size_t room = ASH_AGENT_TOOL_OUT_CAP > L->tool_out->len
                                      ? ASH_AGENT_TOOL_OUT_CAP - L->tool_out->len
                                      : 0;
                    size_t take = (size_t)n < room ? (size_t)n : room;
                    while (take > 0 && take < (size_t)n &&
                           (tb[take] & 0xC0) == 0x80)
                        take--;
                    if (take) {
                        ash_buf_append(L->tool_out, tb, take);
                        ui_stream(L, ASH_TS_TOOL_OUT, (const char *)tb, take);
                    }
                    if ((size_t)n > take && !L->tool_truncated) {
                        ui_stream(L, ASH_TS_TOOL_OUT, ASH_AGENT_TRUNC_MARK,
                                  strlen(ASH_AGENT_TRUNC_MARK));
                        L->tool_truncated = 1;
                    }
                } else if (n == 0) {
                    L->tool_done = 1;
                } else if (errno != EINTR && errno != EAGAIN) {
                    L->tool_done = 1;
                }
            } else if (L->child_exited && r == 0) {
                L->tool_done = 1;
            }
            if (slot_in >= 0 && (pfds[slot_in].revents & POLLIN))
                read_input(L);
        }
        w = ash_co_resume(&L->co);
    }
    return L->co.status;
}

static void tui_teardown(struct ash_loop *L)
{
    if (!L->tui || L->fb.frame == 0)
        return;
    ash_screen_finish(L->trans_h + 1);
}

ash_status ash_loop_run(const ash_loop_cfg *cfg, int in_fd, int out_fd)
{
    if (cfg == NULL || cfg->url == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_loop_run: bad config");

    struct ash_loop *L = NULL;
    ash_arena boot;
    ASH_TRY(ash_arena_create(&boot, "loop-boot", sizeof *L + 64));
    L = ash_new(&boot, struct ash_loop);
    memset(L, 0, sizeof *L);

    ash_status st = ash_mem_create(&L->mem);
    if (st != ASH_OK) {
        ash_arena_destroy(&boot);
        return st;
    }

    L->in_fd = in_fd;
    L->out_fd = out_fd;
    L->pcfg.provider = cfg->provider;
    L->pcfg.url = arena_dup(&boot, cfg->url, strlen(cfg->url));
    L->pcfg.api_key = arena_dup(&boot, cfg->api_key,
                                cfg->api_key ? strlen(cfg->api_key) : 0);
    L->pcfg.model = arena_dup(&boot, cfg->model,
                              cfg->model ? strlen(cfg->model) : 0);
    L->pcfg.system = arena_dup(&boot, cfg->system,
                               cfg->system ? strlen(cfg->system) : 0);
    L->pcfg.max_tokens = cfg->max_tokens;
    const char *tools = NULL;
    if (ash_tools_schema_build(&boot, &tools) != ASH_OK)
        tools = ash_tools_schema();
    L->pcfg.tools = tools;
    L->pcfg.oauth_token = cfg->oauth_token;
    L->pcfg.oauth_ctx = cfg->oauth_ctx;
    L->auth = cfg->auth;
    L->store_arena = cfg->store_arena;
    L->provider_name = cfg->provider != NULL ? cfg->provider->name : NULL;
    ash_agent_host host = { .ud = L, .emit = loop_emit, .pump = loop_pump,
                            .shell = loop_shell };
    ash_agent_init(&L->agent, &L->mem, &L->pcfg, &host);
    ash_footer_init(&L->footer);
    ash_footer_set_provider(&L->footer,
                            cfg->provider != NULL ? cfg->provider->name : NULL,
                            L->pcfg.model);
    char cwd[512];
    if (getcwd(cwd, sizeof cwd) != NULL)
        ash_footer_git_init(&L->footer, cwd);
    ash_provider_scrub_env();
    ash_input_init(&L->dfa);

    L->tui = ash_screen_fd() == out_fd && isatty(out_fd);
    if (L->tui && ash_arena_create(&L->ui, "ui", 1u << 20) != ASH_OK)
        L->tui = 0;
    if (L->tui && ash_arena_create(&L->ts_arena, "transcript", 1u << 16)
            != ASH_OK) {
        ash_arena_destroy(&L->ui);
        L->tui = 0;
    }
    if (L->tui && ash_arena_create(&L->q_arena, "queue", 1u << 16) != ASH_OK) {
        ash_arena_destroy(&L->ts_arena);
        ash_arena_destroy(&L->ui);
        L->tui = 0;
    }
    if (L->tui) {
        ash_style fill = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, ASH_ATTR_NONE };
        L->st_text = (ash_style){ ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT,
                                  ASH_ATTR_CONTENT };
        L->theme = ash_theme_select(getenv("ASH_THEME"));
        L->tools_expanded = 0;
        L->open_kind = -1;
        ash_ts_init(&L->ts, &L->ts_arena);
        ash_sel_clear(&L->sel);
        L->footer.style = (ash_style){ ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT,
                                       ASH_ATTR_NONE };
        L->st_border = ash_input_border_style();
        ash_fb_init(&L->fb, &L->ui, fill);
        ash_sb_init(&L->sb, &L->ui, SB_CELL_BUDGET, SB_MAX_LINES);
        ash_buf_init(&L->frame, &L->ui);
        ash_buf_init(&L->rule, &L->ui);
        int tw, th;
        term_size(L, &tw, &th);
        L->wrap_w = tw;
        ash_queue_init(&L->queue, &L->q_arena);
        L->drain_suspended = 0;
        ash_textarea_init(&L->ta, &L->ui, L->wrap_w > 0 ? L->wrap_w : 80, 0);
    }
    if (L->tui && ash_arena_create(&L->modal_arena, "modal", 1u << 20)
            == ASH_OK) {
        L->has_modal_arena = 1;
        ash_tools_set_confirm(loop_confirm, L);
    }

    ash_arena logarena;
    L->has_log = 0;
    if (cfg->session_path != NULL &&
        ash_arena_create(&logarena, "log", 1u << 16) == ASH_OK) {
        if (ash_log_open(&L->log, &logarena, cfg->session_path) == ASH_OK)
            L->has_log = 1;
        else
            ash_arena_destroy(&logarena);
    }

    void *stack = ash_arena_alloc(&L->mem.stack, LOOP_STACK, 16);
    st = ash_co_create(&L->co, stack, LOOP_STACK, loop_fn, L);
    if (st == ASH_OK)
        st = loop_run(L);

    tui_teardown(L);

    if (L->has_log) {
        ash_log_close(&L->log);
        ash_arena_destroy(&logarena);
    }
    if (L->has_modal_arena) {
        ash_tools_set_confirm(NULL, NULL);
        ash_arena_destroy(&L->modal_arena);
    }
    if (L->tui) {
        ash_arena_destroy(&L->q_arena);
        ash_arena_destroy(&L->ts_arena);
        ash_arena_destroy(&L->ui);
    }
    ash_mem_destroy(&L->mem);
    ash_arena_destroy(&boot);
    return st;
}
