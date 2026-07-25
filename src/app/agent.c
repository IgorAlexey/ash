#include <string.h>

#include "ash/app/agent.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/tools/tools.h"
#include "ash/base/poison.h"

enum { TOOL_BATCH_MAX = 64 };

struct tool_call {
    const char *id;
    const char *name;
    const char *input;
    size_t      ilen;
};

static void emit(ash_agent *a, const ash_agent_event *ev)
{
    a->host.emit(a->host.ud, ev);
}

static void emit_kind(ash_agent *a, ash_agent_ev_kind kind)
{
    ash_agent_event ev = { .kind = kind };
    emit(a, &ev);
}

static void emit_text(ash_agent *a, ash_agent_ev_kind kind, const char *text)
{
    ash_agent_event ev = { .kind = kind, .text = text, .len = strlen(text) };
    emit(a, &ev);
}

static const char *session_dup(ash_agent *a, const char *s, size_t len)
{
    if (s == NULL)
        return NULL;
    char *c = ash_array(&a->mem->session, char, len + 1);
    memcpy(c, s, len);
    c[len] = 0;
    return c;
}

static void push_msg(ash_agent *a, ash_msg m)
{
    if (a->nmsgs == a->msgcap) {
        size_t nc = a->msgcap ? a->msgcap * 2 : 8;
        ash_msg *nm = ash_array(&a->mem->session, ash_msg, nc);
        if (a->nmsgs)
            memcpy(nm, a->msgs, a->nmsgs * sizeof *nm);
        a->msgs = nm;
        a->msgcap = nc;
    }
    a->msgs[a->nmsgs++] = m;
    ash_agent_event ev = { .kind = ASH_AGENT_MSG_APPEND,
                           .msg = &a->msgs[a->nmsgs - 1] };
    emit(a, &ev);
}

static void add_msg(ash_agent *a, const char *role, const char *content,
                    size_t len)
{
    push_msg(a, (ash_msg){ .role = role,
                           .content = session_dup(a, content, len) });
}

static void add_tool_use(ash_agent *a, const char *text, size_t text_len,
                         const struct tool_call *c)
{
    push_msg(a, (ash_msg){
        .role = "assistant",
        .content = text_len ? session_dup(a, text, text_len) : NULL,
        .tool_id = session_dup(a, c->id, strlen(c->id)),
        .tool_name = session_dup(a, c->name, strlen(c->name)),
        .tool_input = session_dup(a, c->input, c->ilen),
    });
}

static void tool_result(ash_agent *a, const char *id, const char *result,
                        size_t rlen, int is_error)
{
    push_msg(a, (ash_msg){
        .role = "user",
        .tool_id = session_dup(a, id, strlen(id)),
        .tool_result = session_dup(a, result != NULL ? result : "", rlen),
        .tool_is_error = is_error,
    });
    ash_agent_event ev = { .kind = ASH_AGENT_TOOL_END, .id = id,
                           .text = result, .len = rlen, .is_error = is_error };
    emit(a, &ev);
}

static int run_shell_tool(ash_agent *a, const struct tool_call *c)
{
    ash_buf out;
    ash_buf_init(&out, &a->mem->turn);

    int canceled = 0;
    ash_arena_mark mk = ash_arena_mark_get(&a->mem->scratch);
    const char *cmd = NULL;
    if (ash_bash_command(&a->mem->scratch, c->input, c->ilen, &cmd) != ASH_OK) {
        ash_buf_append_cstr(&out, "tool error: ");
        ash_buf_append_cstr(&out, ash_errbuf);
    } else {
        canceled = a->host.shell(a->host.ud, cmd, &out);
    }
    ash_arena_rewind(&a->mem->scratch, mk);

    if (canceled)
        return 1;
    tool_result(a, c->id, (const char *)out.data, out.len, 0);
    return 0;
}

static void run_pure_tool(ash_agent *a, const ash_tool *t,
                          const struct tool_call *c)
{
    ash_arena_mark mk = ash_arena_mark_get(&a->mem->scratch);
    ash_tool_result res = { 0 };
    ash_status st = ash_tool_dispatch(t, &a->mem->scratch, c->input, c->ilen,
                                      &res);
    if (st != ASH_OK)
        tool_result(a, c->id, ash_errbuf, strlen(ash_errbuf), 1);
    else
        tool_result(a, c->id, res.content ? res.content : "", res.len,
                    res.is_error);
    ash_arena_rewind(&a->mem->scratch, mk);
}

static void unknown_tool(ash_agent *a, const struct tool_call *c)
{
    ash_arena_mark mk = ash_arena_mark_get(&a->mem->scratch);
    ash_buf msg;
    ash_buf_init(&msg, &a->mem->scratch);
    ash_buf_append_cstr(&msg, "tool error: unknown tool '");
    ash_buf_append_cstr(&msg, c->name ? c->name : "");
    ash_buf_append_byte(&msg, '\'');
    tool_result(a, c->id, (const char *)msg.data, msg.len, 1);
    ash_arena_rewind(&a->mem->scratch, mk);
}

static void on_delta(void *ud, const char *text, size_t n)
{
    ash_agent *a = ud;
    ash_buf_append(&a->resp, text, n);
    ash_agent_event ev = { .kind = ASH_AGENT_TEXT, .text = text, .len = n };
    emit(a, &ev);
}

void ash_agent_init(ash_agent *a, ash_mem *mem, const ash_provider_cfg *cfg,
                    const ash_agent_host *host)
{
    memset(a, 0, sizeof *a);
    a->mem = mem;
    a->cfg = cfg;
    a->host = *host;
}

void ash_agent_reset(ash_agent *a)
{
    ash_arena_reset(&a->mem->session);
    a->msgs = NULL;
    a->nmsgs = 0;
    a->msgcap = 0;
}

void ash_agent_user(ash_agent *a, const char *text, size_t len)
{
    add_msg(a, "user", text, len);
}

size_t ash_agent_msg_count(const ash_agent *a)
{
    return a->nmsgs;
}

const ash_msg *ash_agent_msg_at(const ash_agent *a, size_t i)
{
    if (i >= a->nmsgs)
        return NULL;
    return &a->msgs[i];
}

ash_agent_outcome ash_agent_run(ash_agent *a)
{
    a->canceled = 0;
    emit_kind(a, ASH_AGENT_TURN_START);

    for (;;) {
        ash_buf_init(&a->resp, &a->mem->turn);
        a->stop[0] = 0;
        emit_kind(a, ASH_AGENT_MSG_START);

        ash_provider_stream *s = NULL;
        if (ash_provider_start(&s, &a->mem->turn, a->cfg, a->msgs, a->nmsgs,
                               on_delta, a, a->stop, sizeof a->stop) != ASH_OK) {
            emit_text(a, ASH_AGENT_ERROR, ash_errbuf);
            emit_kind(a, ASH_AGENT_TURN_END);
            return ASH_AGENT_FAILED;
        }

        a->canceled = a->host.pump(a->host.ud, s);

        ash_ai_usage u;
        if (ash_provider_usage(s, &u) == ASH_OK) {
            ash_agent_event ev = { .kind = ASH_AGENT_USAGE, .usage = &u };
            emit(a, &ev);
        }

        ash_status fin = ash_provider_finish(s);
        struct tool_call calls[TOOL_BATCH_MAX];
        int nc = ash_provider_tool_count(s);
        if (nc > TOOL_BATCH_MAX)
            nc = TOOL_BATCH_MAX;
        for (int i = 0; i < nc; i++) {
            ash_status ts = ash_provider_tool_at(s, i, &calls[i].id,
                                                 &calls[i].name, &calls[i].input,
                                                 &calls[i].ilen);
            if (ts != ASH_OK || calls[i].id == NULL || calls[i].name == NULL) {
                nc = i;
                break;
            }
        }
        ash_provider_stream_close(s);

        if (a->canceled) {
            emit_text(a, ASH_AGENT_MSG_END, "aborted");
            emit_kind(a, ASH_AGENT_TURN_END);
            return ASH_AGENT_ABORTED;
        }
        if (fin != ASH_OK) {
            emit_text(a, ASH_AGENT_MSG_END, "error");
            emit_text(a, ASH_AGENT_ERROR, ash_errbuf);
            emit_kind(a, ASH_AGENT_TURN_END);
            return ASH_AGENT_FAILED;
        }
        emit_text(a, ASH_AGENT_MSG_END, a->stop[0] ? a->stop : "end_turn");

        int is_tool = strcmp(a->stop, "tool_use") == 0 && nc > 0 &&
                      calls[0].id != NULL && calls[0].id[0] != 0;
        if (!is_tool) {
            add_msg(a, "assistant", (const char *)a->resp.data, a->resp.len);
            emit_kind(a, ASH_AGENT_TURN_END);
            return ASH_AGENT_DONE;
        }

        for (int i = 0; i < nc; i++)
            add_tool_use(a, i == 0 ? (const char *)a->resp.data : NULL,
                         i == 0 ? a->resp.len : 0, &calls[i]);

        int canceled_at = -1;
        for (int i = 0; i < nc; i++) {
            ash_agent_event ev = { .kind = ASH_AGENT_TOOL_START,
                                   .id = calls[i].id, .name = calls[i].name,
                                   .text = calls[i].input, .len = calls[i].ilen };
            emit(a, &ev);

            const ash_tool *t = ash_tool_find(calls[i].name);
            if (t == NULL) {
                unknown_tool(a, &calls[i]);
                continue;
            }
            if (t->kind != ASH_TOOL_SHELL) {
                run_pure_tool(a, t, &calls[i]);
                continue;
            }
            if (run_shell_tool(a, &calls[i])) {
                canceled_at = i;
                break;
            }
        }

        if (canceled_at >= 0) {
            a->canceled = 1;
            for (int i = canceled_at; i < nc; i++)
                tool_result(a, calls[i].id, "[canceled]", strlen("[canceled]"), 1);
            emit_kind(a, ASH_AGENT_TURN_END);
            return ASH_AGENT_ABORTED;
        }
        ash_arena_reset(&a->mem->turn);
    }
}
