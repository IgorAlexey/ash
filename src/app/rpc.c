#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ash/ai/provider.h"
#include "ash/app/agent.h"
#include "ash/app/rpc.h"
#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
#include "ash/core/proc.h"
#include "ash/tools/tools.h"
#include "ash/base/poison.h"

enum { LINE_MAX = 1u << 20 };
enum { IN_CHUNK = 4096 };

struct ash_rpc {
    ash_mem          mem;
    ash_provider_cfg pcfg;

    int              in_fd;
    int              out_fd;

    uint8_t          pending[LINE_MAX];
    size_t           pending_len;
    int              in_eof;

    int              canceled;

    ash_agent        agent;
    char             err[ASH_ERRBUF_CAP];
    int              failed;
};

static void write_bytes(struct ash_rpc *R, const void *p, size_t n)
{
    const char *b = p;
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(R->out_fd, b + off, n - off);
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

static void emit_compact(ash_buf *b, const ash_json *v)
{
    switch (v->type) {
    case ASH_JSON_NULL:
        ash_buf_append_cstr(b, "null");
        return;
    case ASH_JSON_BOOL:
        ash_buf_append_cstr(b, v->u.boolean ? "true" : "false");
        return;
    case ASH_JSON_NUMBER:
        ash_buf_append(b, v->u.num.p, v->u.num.n);
        return;
    case ASH_JSON_STRING:
        ash_json_quote(b, v->u.str.p, v->u.str.n);
        return;
    case ASH_JSON_ARRAY:
        ash_buf_append_byte(b, '[');
        for (size_t i = 0; i < v->u.arr.n; i++) {
            if (i)
                ash_buf_append_byte(b, ',');
            emit_compact(b, &v->u.arr.v[i]);
        }
        ash_buf_append_byte(b, ']');
        return;
    case ASH_JSON_OBJECT:
        ash_buf_append_byte(b, '{');
        for (size_t i = 0; i < v->u.obj.n; i++) {
            if (i)
                ash_buf_append_byte(b, ',');
            ash_json_quote(b, v->u.obj.v[i].key, v->u.obj.v[i].klen);
            ash_buf_append_byte(b, ':');
            emit_compact(b, &v->u.obj.v[i].val);
        }
        ash_buf_append_byte(b, '}');
        return;
    }
}

static void field_str(ash_buf *b, const char *key, const char *p, size_t n)
{
    ash_buf_append_byte(b, '"');
    ash_buf_append_cstr(b, key);
    ash_buf_append_cstr(b, "\":");
    ash_json_quote(b, p, n);
}

static void append_input(struct ash_rpc *R, ash_buf *b,
                         const char *input, size_t ilen)
{
    if (input == NULL || ilen == 0) {
        ash_buf_append_cstr(b, "{}");
        return;
    }
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_json v;
    if (ash_json_parse(&R->mem.scratch, input, ilen, &v) == ASH_OK)
        emit_compact(b, &v);
    else
        ash_buf_append_cstr(b, "{}");
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void emit_line(struct ash_rpc *R, ash_buf *b)
{
    ash_buf_append_byte(b, '\n');
    write_bytes(R, b->data, b->len);
}

static void event_open(ash_buf *b, const char *name)
{
    ash_buf_append_cstr(b, "{\"type\":\"event\",");
    field_str(b, "event", name, strlen(name));
}

static void emit_event(struct ash_rpc *R, const char *name)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    event_open(&b, name);
    ash_buf_append_byte(&b, '}');
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void emit_update(struct ash_rpc *R, const char *text, size_t n)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    event_open(&b, "message_update");
    ash_buf_append_byte(&b, ',');
    field_str(&b, "text", text, n);
    ash_buf_append_byte(&b, '}');
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void emit_message_end(struct ash_rpc *R, const char *stop)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    event_open(&b, "message_end");
    ash_buf_append_byte(&b, ',');
    field_str(&b, "stop_reason", stop, strlen(stop));
    ash_buf_append_byte(&b, '}');
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void emit_error_event(struct ash_rpc *R, const char *msg)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    event_open(&b, "error");
    ash_buf_append_byte(&b, ',');
    field_str(&b, "message", msg, strlen(msg));
    ash_buf_append_byte(&b, '}');
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void emit_tool_start(struct ash_rpc *R, const char *id, const char *name,
                            const char *input, size_t ilen)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    event_open(&b, "tool_execution_start");
    ash_buf_append_byte(&b, ',');
    field_str(&b, "id", id ? id : "", id ? strlen(id) : 0);
    ash_buf_append_byte(&b, ',');
    field_str(&b, "name", name ? name : "", name ? strlen(name) : 0);
    ash_buf_append_cstr(&b, ",\"input\":");
    append_input(R, &b, input, ilen);
    ash_buf_append_byte(&b, '}');
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void emit_tool_end(struct ash_rpc *R, const char *id,
                          const char *result, size_t rlen, int is_error)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    event_open(&b, "tool_execution_end");
    ash_buf_append_byte(&b, ',');
    field_str(&b, "id", id ? id : "", id ? strlen(id) : 0);
    ash_buf_append_byte(&b, ',');
    field_str(&b, "result", result ? result : "", result ? rlen : 0);
    ash_buf_append_cstr(&b, is_error ? ",\"is_error\":true}" : ",\"is_error\":false}");
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void response_open(ash_buf *b, ash_slice command, ash_slice id)
{
    ash_buf_append_cstr(b, "{\"type\":\"response\",");
    field_str(b, "command", command.p ? command.p : "", command.p ? command.len : 0);
    if (id.len > 0) {
        ash_buf_append_byte(b, ',');
        field_str(b, "id", id.p, id.len);
    }
}

static void emit_response_ok(struct ash_rpc *R, ash_slice command, ash_slice id)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    response_open(&b, command, id);
    ash_buf_append_cstr(&b, ",\"success\":true}");
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void emit_response_err(struct ash_rpc *R, ash_slice command,
                              ash_slice id, const char *err)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    response_open(&b, command, id);
    ash_buf_append_cstr(&b, ",\"success\":false,");
    field_str(&b, "error", err, strlen(err));
    ash_buf_append_byte(&b, '}');
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
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

ash_status ash_rpc_parse(ash_arena *a, const char *line, size_t len,
                         ash_rpc_cmd *out)
{
    memset(out, 0, sizeof *out);
    ash_json v;
    ASH_TRY(ash_json_parse(a, line, len, &v));
    if (v.type != ASH_JSON_OBJECT)
        return ash_fail(ASH_ERR_PARSE, "rpc: command is not a JSON object");

    const ash_json *t = ash_json_get(&v, "type");
    if (t == NULL || ash_json_str(t, &out->type) != ASH_OK)
        return ash_fail(ASH_ERR_PARSE, "rpc: missing string field 'type'");

    const ash_json *id = ash_json_get(&v, "id");
    ash_slice tmp;
    if (id != NULL && ash_json_str(id, &tmp) == ASH_OK)
        out->id = tmp;
    const ash_json *m = ash_json_get(&v, "message");
    if (m != NULL && ash_json_str(m, &tmp) == ASH_OK)
        out->message = tmp;

    if (ash_slice_eq_cstr(out->type, "prompt"))
        out->kind = ASH_RPC_PROMPT;
    else if (ash_slice_eq_cstr(out->type, "abort"))
        out->kind = ASH_RPC_ABORT;
    else if (ash_slice_eq_cstr(out->type, "get_state"))
        out->kind = ASH_RPC_GET_STATE;
    else if (ash_slice_eq_cstr(out->type, "get_messages"))
        out->kind = ASH_RPC_GET_MESSAGES;
    else if (ash_slice_eq_cstr(out->type, "new_session"))
        out->kind = ASH_RPC_NEW_SESSION;
    else
        out->kind = ASH_RPC_UNKNOWN;
    return ASH_OK;
}

static int find_nl(const struct ash_rpc *R, size_t *pos)
{
    for (size_t i = 0; i < R->pending_len; i++) {
        if (R->pending[i] == '\n') {
            *pos = i;
            return 1;
        }
    }
    return 0;
}

static int take_line(struct ash_rpc *R, ash_arena *a,
                     const char **line, size_t *len)
{
    size_t pos;
    int have = find_nl(R, &pos);
    int full = R->pending_len == sizeof R->pending;
    if (!have && !full)
        return 0;

    size_t llen = have ? pos : R->pending_len;
    size_t consumed = have ? pos + 1 : R->pending_len;
    while (llen > 0 && R->pending[llen - 1] == '\r')
        llen--;

    char *c = ash_array(a, char, llen + 1);
    memcpy(c, R->pending, llen);
    c[llen] = 0;
    R->pending_len -= consumed;
    memmove(R->pending, R->pending + consumed, R->pending_len);
    *line = c;
    *len = llen;
    return 1;
}

static int fill_input(struct ash_rpc *R)
{
    if (R->pending_len == sizeof R->pending)
        return 1;
    ssize_t n;
    do {
        n = read(R->in_fd, R->pending + R->pending_len,
                 sizeof R->pending - R->pending_len);
    } while (n < 0 && errno == EINTR);
    if (n <= 0) {
        if (n == 0)
            R->in_eof = 1;
        return 0;
    }
    R->pending_len += (size_t)n;
    return 1;
}

static void handle_side_command(struct ash_rpc *R, const ash_rpc_cmd *c)
{
    if (c->kind == ASH_RPC_ABORT) {
        R->canceled = 1;
        return;
    }
    emit_response_err(R, c->type, c->id, "busy: a prompt is already running");
}

static void drain_input(struct ash_rpc *R)
{
    if (!fill_input(R))
        return;
    for (;;) {
        ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
        const char *line;
        size_t len;
        if (!take_line(R, &R->mem.scratch, &line, &len)) {
            ash_arena_rewind(&R->mem.scratch, mk);
            break;
        }
        if (len > 0) {
            ash_rpc_cmd c;
            if (ash_rpc_parse(&R->mem.scratch, line, len, &c) == ASH_OK)
                handle_side_command(R, &c);
            else
                emit_response_err(R, ash_slice_from_cstr(""),
                                  ash_slice_from_cstr(""), "malformed command");
        }
        ash_arena_rewind(&R->mem.scratch, mk);
    }
}

static int rpc_shell(void *ud, const char *cmd, ash_buf *out)
{
    struct ash_rpc *R = ud;
    const char *argv[] = { "sh", "-c", cmd, NULL };
    ash_proc p;
    if (ash_proc_spawn(&p, argv) != ASH_OK) {
        ash_buf_append_cstr(out, "tool error: ");
        ash_buf_append_cstr(out, ash_errbuf);
        return 0;
    }

    int done = 0, child_exited = 0, truncated = 0;
    while (!done) {
        struct pollfd pfds[3];
        nfds_t nfd = 0;
        int slot_out = (int)nfd;
        pfds[nfd++] = (struct pollfd){ .fd = ash_proc_out_fd(&p),
                                       .events = POLLIN, .revents = 0 };
        int slot_pid = -1;
        if (!child_exited) {
            slot_pid = (int)nfd;
            pfds[nfd++] = (struct pollfd){ .fd = ash_proc_pidfd(&p),
                                           .events = POLLIN, .revents = 0 };
        }
        int slot_in = -1;
        if (!R->in_eof) {
            slot_in = (int)nfd;
            pfds[nfd++] = (struct pollfd){ .fd = R->in_fd, .events = POLLIN,
                                           .revents = 0 };
        }
        int r;
        do {
            r = poll(pfds, nfd, child_exited ? 200 : -1);
        } while (r < 0 && errno == EINTR);
        if (slot_pid >= 0 && (pfds[slot_pid].revents & POLLIN))
            child_exited = 1;
        if (pfds[slot_out].revents & (POLLIN | POLLHUP)) {
            uint8_t tb[4096];
            ssize_t n = read(ash_proc_out_fd(&p), tb, sizeof tb);
            if (n > 0) {
                size_t room = ASH_AGENT_TOOL_OUT_CAP > out->len
                                  ? ASH_AGENT_TOOL_OUT_CAP - out->len : 0;
                size_t take = (size_t)n < room ? (size_t)n : room;
                if (take)
                    ash_buf_append(out, tb, take);
                if ((size_t)n > take)
                    truncated = 1;
            } else if (n == 0) {
                done = 1;
            } else if (errno != EINTR && errno != EAGAIN) {
                done = 1;
            }
        } else if (child_exited && r == 0) {
            done = 1;
        }
        if (slot_in >= 0 && (pfds[slot_in].revents & POLLIN)) {
            drain_input(R);
            if (R->canceled)
                break;
        }
    }

    if (R->canceled) {
        ash_proc_close(&p);
        return 1;
    }
    if (truncated)
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

static int rpc_pump(void *ud, ash_provider_stream *s)
{
    struct ash_rpc *R = ud;
    int running = 1;
    while (running) {
        int ready = 0;
        int wfd = R->in_eof ? -1 : R->in_fd;
        ash_status st = ash_provider_wait(s, wfd, 1000, &ready);
        if (st == ASH_OK)
            st = ash_provider_pump(s, &running);
        if (st != ASH_OK)
            running = 0;
        if (ready)
            drain_input(R);
        if (R->canceled)
            break;
    }
    return R->canceled;
}

static void rpc_emit(void *ud, const ash_agent_event *ev)
{
    struct ash_rpc *R = ud;
    switch (ev->kind) {
    case ASH_AGENT_TURN_START:
        emit_event(R, "turn_start");
        return;
    case ASH_AGENT_MSG_START:
        emit_event(R, "message_start");
        return;
    case ASH_AGENT_TEXT:
        emit_update(R, ev->text, ev->len);
        return;
    case ASH_AGENT_MSG_END:
        emit_message_end(R, ev->text);
        return;
    case ASH_AGENT_TOOL_START:
        emit_tool_start(R, ev->id, ev->name, ev->text, ev->len);
        return;
    case ASH_AGENT_TOOL_END:
        emit_tool_end(R, ev->id, ev->text, ev->len, ev->is_error);
        return;
    case ASH_AGENT_ERROR:
        R->failed = 1;
        snprintf(R->err, sizeof R->err, "%s", ev->text);
        emit_error_event(R, ev->text);
        return;
    case ASH_AGENT_TURN_END:
        if (!R->failed)
            emit_event(R, "turn_end");
        return;
    case ASH_AGENT_USAGE:
    case ASH_AGENT_MSG_APPEND:
        return;
    }
}

static void run_prompt(struct ash_rpc *R, ash_slice command, ash_slice id,
                       ash_slice message)
{
    R->canceled = 0;
    R->failed = 0;
    R->err[0] = 0;
    ash_agent_user(&R->agent, message.p ? message.p : "",
                   message.p ? message.len : 0);

    ash_agent_outcome oc = ash_agent_run(&R->agent);
    if (oc == ASH_AGENT_DONE) {
        emit_response_ok(R, command, id);
        return;
    }
    if (oc == ASH_AGENT_ABORTED) {
        emit_response_err(R, command, id, "aborted");
        return;
    }
    emit_response_err(R, command, id, R->err);
}

static void emit_state(struct ash_rpc *R, ash_slice command, ash_slice id)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    response_open(&b, command, id);
    ash_buf_append_cstr(&b, ",\"success\":true,\"data\":{");
    const char *pn = R->pcfg.provider ? R->pcfg.provider->name : "";
    field_str(&b, "provider", pn, strlen(pn));
    ash_buf_append_byte(&b, ',');
    const char *mn = R->pcfg.model ? R->pcfg.model : "";
    field_str(&b, "model", mn, strlen(mn));
    ash_buf_append_cstr(&b, ",\"streaming\":false,\"message_count\":");
    char num[24];
    int nn = snprintf(num, sizeof num, "%zu", ash_agent_msg_count(&R->agent));
    if (nn > 0)
        ash_buf_append(&b, num, (size_t)nn);
    ash_buf_append_cstr(&b, "}}");
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void emit_messages(struct ash_rpc *R, ash_slice command, ash_slice id)
{
    ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
    ash_buf b;
    ash_buf_init(&b, &R->mem.scratch);
    response_open(&b, command, id);
    ash_buf_append_cstr(&b, ",\"success\":true,\"data\":{\"messages\":[");
    size_t nmsgs = ash_agent_msg_count(&R->agent);
    for (size_t i = 0; i < nmsgs; i++) {
        const ash_msg *m = ash_agent_msg_at(&R->agent, i);
        if (i)
            ash_buf_append_byte(&b, ',');
        ash_buf_append_byte(&b, '{');
        const char *role = m->role ? m->role : "user";
        field_str(&b, "role", role, strlen(role));
        if (m->tool_name != NULL) {
            ash_buf_append_byte(&b, ',');
            field_str(&b, "tool_id", m->tool_id ? m->tool_id : "",
                      m->tool_id ? strlen(m->tool_id) : 0);
            ash_buf_append_byte(&b, ',');
            field_str(&b, "tool_name", m->tool_name, strlen(m->tool_name));
            ash_buf_append_cstr(&b, ",\"tool_input\":");
            append_input(R, &b, m->tool_input,
                         m->tool_input ? strlen(m->tool_input) : 0);
            if (m->content != NULL) {
                ash_buf_append_byte(&b, ',');
                field_str(&b, "content", m->content, strlen(m->content));
            }
        } else if (m->tool_result != NULL) {
            ash_buf_append_byte(&b, ',');
            field_str(&b, "tool_id", m->tool_id ? m->tool_id : "",
                      m->tool_id ? strlen(m->tool_id) : 0);
            ash_buf_append_byte(&b, ',');
            field_str(&b, "tool_result", m->tool_result, strlen(m->tool_result));
            ash_buf_append_cstr(&b, m->tool_is_error
                                    ? ",\"is_error\":true" : ",\"is_error\":false");
        } else {
            ash_buf_append_byte(&b, ',');
            field_str(&b, "content", m->content ? m->content : "",
                      m->content ? strlen(m->content) : 0);
        }
        ash_buf_append_byte(&b, '}');
    }
    ash_buf_append_cstr(&b, "]}}");
    emit_line(R, &b);
    ash_arena_rewind(&R->mem.scratch, mk);
}

static void handle_command(struct ash_rpc *R, const char *line, size_t len)
{
    ash_rpc_cmd c;
    if (ash_rpc_parse(&R->mem.scratch, line, len, &c) != ASH_OK) {
        emit_response_err(R, ash_slice_from_cstr(""), ash_slice_from_cstr(""),
                          ash_errbuf);
        return;
    }
    switch (c.kind) {
    case ASH_RPC_PROMPT:
        run_prompt(R, c.type, c.id, c.message);
        return;
    case ASH_RPC_ABORT:
        emit_response_ok(R, c.type, c.id);
        return;
    case ASH_RPC_GET_STATE:
        emit_state(R, c.type, c.id);
        return;
    case ASH_RPC_GET_MESSAGES:
        emit_messages(R, c.type, c.id);
        return;
    case ASH_RPC_NEW_SESSION:
        ash_agent_reset(&R->agent);
        emit_response_ok(R, c.type, c.id);
        return;
    case ASH_RPC_UNKNOWN:
        emit_response_err(R, c.type, c.id, "unknown command type");
        return;
    }
}

ash_status ash_rpc_run(const ash_loop_cfg *cfg, int in_fd, int out_fd)
{
    if (cfg == NULL || cfg->url == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_rpc_run: bad config");

    struct ash_rpc *R = NULL;
    ash_arena boot;
    ASH_TRY(ash_arena_create(&boot, "rpc-boot", sizeof *R + 64));
    R = ash_new(&boot, struct ash_rpc);
    memset(R, 0, sizeof *R);

    ash_status st = ash_mem_create(&R->mem);
    if (st != ASH_OK) {
        ash_arena_destroy(&boot);
        return st;
    }

    R->in_fd = in_fd;
    R->out_fd = out_fd;
    R->pcfg.provider = cfg->provider;
    R->pcfg.url = arena_dup(&boot, cfg->url, strlen(cfg->url));
    R->pcfg.api_key = arena_dup(&boot, cfg->api_key,
                                cfg->api_key ? strlen(cfg->api_key) : 0);
    R->pcfg.model = arena_dup(&boot, cfg->model,
                              cfg->model ? strlen(cfg->model) : 0);
    R->pcfg.system = arena_dup(&boot, cfg->system,
                               cfg->system ? strlen(cfg->system) : 0);
    R->pcfg.max_tokens = cfg->max_tokens;
    const char *tools = NULL;
    if (ash_tools_schema_build(&boot, &tools) != ASH_OK)
        tools = ash_tools_schema();
    R->pcfg.tools = tools;
    R->pcfg.oauth_token = cfg->oauth_token;
    R->pcfg.oauth_ctx = cfg->oauth_ctx;
    ash_agent_host host = { .ud = R, .emit = rpc_emit, .pump = rpc_pump,
                            .shell = rpc_shell };
    ash_agent_init(&R->agent, &R->mem, &R->pcfg, &host);
    ash_provider_scrub_env();

    for (;;) {
        ash_arena_mark mk = ash_arena_mark_get(&R->mem.scratch);
        const char *line;
        size_t len;
        while (!take_line(R, &R->mem.scratch, &line, &len)) {
            if (!fill_input(R)) {
                ash_arena_rewind(&R->mem.scratch, mk);
                goto done;
            }
        }
        if (len > 0)
            handle_command(R, line, len);
        ash_arena_rewind(&R->mem.scratch, mk);
    }

done:
    ash_mem_destroy(&R->mem);
    ash_arena_destroy(&boot);
    return ASH_OK;
}
