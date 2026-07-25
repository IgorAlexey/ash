#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ash/ai/http.h"
#include "ash/ai/provider.h"
#include "ash/ai/sse.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
#include "ash/base/poison.h"

static void emit_json(ash_buf *b, const ash_json *v)
{
    switch (v->type) {
    case ASH_JSON_NULL:
        ash_buf_append_cstr(b, "null");
        break;
    case ASH_JSON_BOOL:
        ash_buf_append_cstr(b, v->u.boolean ? "true" : "false");
        break;
    case ASH_JSON_NUMBER:
        ash_buf_append(b, v->u.num.p, v->u.num.n);
        break;
    case ASH_JSON_STRING:
        ash_json_quote(b, v->u.str.p, v->u.str.n);
        break;
    case ASH_JSON_ARRAY:
        ash_buf_append_byte(b, '[');
        for (size_t i = 0; i < v->u.arr.n; i++) {
            if (i)
                ash_buf_append_byte(b, ',');
            emit_json(b, &v->u.arr.v[i]);
        }
        ash_buf_append_byte(b, ']');
        break;
    case ASH_JSON_OBJECT:
        ash_buf_append_byte(b, '{');
        for (size_t i = 0; i < v->u.obj.n; i++) {
            const ash_json_member *m = &v->u.obj.v[i];
            if (i)
                ash_buf_append_byte(b, ',');
            ash_json_quote(b, m->key, m->klen);
            ash_buf_append_byte(b, ':');
            emit_json(b, &m->val);
        }
        ash_buf_append_byte(b, '}');
        break;
    }
}

static void append_int(ash_buf *b, int v)
{
    char tmp[16];
    int n = snprintf(tmp, sizeof tmp, "%d", v);
    if (n > 0)
        ash_buf_append(b, tmp, (size_t)n);
}

static void append_block(ash_buf *b, const ash_msg *m)
{
    assert(!(m->tool_name != NULL && m->tool_result != NULL));
    if (m->tool_name != NULL) {
        if (m->content != NULL && m->content[0] != 0) {
            ash_buf_append_cstr(b, "{\"type\":\"text\",\"text\":");
            ash_json_quote_cstr(b, m->content);
            ash_buf_append_cstr(b, "},");
        }
        ash_buf_append_cstr(b, "{\"type\":\"tool_use\",\"id\":");
        ash_json_quote_cstr(b, m->tool_id ? m->tool_id : "");
        ash_buf_append_cstr(b, ",\"name\":");
        ash_json_quote_cstr(b, m->tool_name);
        ash_buf_append_cstr(b, ",\"input\":");
        ash_buf_append_cstr(b, m->tool_input ? m->tool_input : "{}");
        ash_buf_append_byte(b, '}');
        return;
    }
    ash_buf_append_cstr(b, "{\"type\":\"tool_result\",\"tool_use_id\":");
    ash_json_quote_cstr(b, m->tool_id ? m->tool_id : "");
    ash_buf_append_cstr(b, ",\"content\":");
    ash_json_quote_cstr(b, m->tool_result);
    if (m->tool_is_error)
        ash_buf_append_cstr(b, ",\"is_error\":true");
    ash_buf_append_byte(b, '}');
}

static int msg_is_block(const ash_msg *m)
{
    return m->tool_name != NULL || m->tool_result != NULL;
}

static int msg_mergeable(const ash_msg *a, const ash_msg *b)
{
    if (!msg_is_block(a) || !msg_is_block(b))
        return 0;
    const char *ra = a->role ? a->role : "user";
    const char *rb = b->role ? b->role : "user";
    if (strcmp(ra, rb) != 0)
        return 0;
    return (a->tool_name != NULL) == (b->tool_name != NULL);
}

static ash_provider_kind cfg_kind(const ash_provider_cfg *cfg)
{
    const ash_provider_desc *d = cfg->provider ? cfg->provider
                                               : ash_provider_default();
    return d->kind;
}

static void build_body_anthropic(ash_buf *b, const ash_provider_cfg *cfg,
                                 const ash_msg *msgs, size_t nmsgs)
{
    ash_buf_append_cstr(b, "{\"model\":");
    ash_json_quote_cstr(b, cfg->model);
    ash_buf_append_cstr(b, ",\"max_tokens\":");
    append_int(b, cfg->max_tokens > 0 ? cfg->max_tokens : 1024);
    ash_buf_append_cstr(b, ",\"stream\":true");
    if (cfg->system != NULL) {
        ash_buf_append_cstr(b, ",\"system\":");
        ash_json_quote_cstr(b, cfg->system);
    }
    if (cfg->tools != NULL) {
        ash_buf_append_cstr(b, ",\"tools\":");
        ash_buf_append_cstr(b, cfg->tools);
    }
    ash_buf_append_cstr(b, ",\"messages\":[");
    size_t i = 0;
    int first = 1;
    while (i < nmsgs) {
        const ash_msg *m = &msgs[i];
        if (!first)
            ash_buf_append_byte(b, ',');
        first = 0;
        ash_buf_append_cstr(b, "{\"role\":");
        ash_json_quote_cstr(b, m->role ? m->role : "user");
        ash_buf_append_cstr(b, ",\"content\":");
        if (!msg_is_block(m)) {
            ash_json_quote_cstr(b, m->content ? m->content : "");
            i++;
        } else {
            ash_buf_append_byte(b, '[');
            append_block(b, m);
            size_t j = i + 1;
            while (j < nmsgs && msg_mergeable(m, &msgs[j])) {
                ash_buf_append_byte(b, ',');
                append_block(b, &msgs[j]);
                j++;
            }
            ash_buf_append_byte(b, ']');
            i = j;
        }
        ash_buf_append_byte(b, '}');
    }
    ash_buf_append_cstr(b, "]}");
}

static void build_openai_tools(ash_buf *b, const char *tools_json)
{
    ash_json v;
    if (ash_json_parse(b->arena, tools_json, strlen(tools_json), &v) != ASH_OK ||
        v.type != ASH_JSON_ARRAY) {
        ash_buf_append_cstr(b, tools_json);
        return;
    }
    ash_buf_append_byte(b, '[');
    int wrote = 0;
    for (size_t i = 0; i < v.u.arr.n; i++) {
        const ash_json *t = &v.u.arr.v[i];
        const ash_json *name = t->type == ASH_JSON_OBJECT
                                   ? ash_json_get(t, "name") : NULL;
        ash_slice ns;
        if (name == NULL || ash_json_str(name, &ns) != ASH_OK)
            continue;
        if (wrote)
            ash_buf_append_byte(b, ',');
        wrote = 1;
        ash_buf_append_cstr(b, "{\"type\":\"function\",\"function\":{\"name\":");
        ash_json_quote(b, ns.p, ns.len);
        const ash_json *desc = ash_json_get(t, "description");
        ash_slice ds;
        if (desc != NULL && ash_json_str(desc, &ds) == ASH_OK) {
            ash_buf_append_cstr(b, ",\"description\":");
            ash_json_quote(b, ds.p, ds.len);
        }
        ash_buf_append_cstr(b, ",\"parameters\":");
        const ash_json *schema = ash_json_get(t, "input_schema");
        if (schema != NULL)
            emit_json(b, schema);
        else
            ash_buf_append_cstr(b, "{\"type\":\"object\"}");
        ash_buf_append_cstr(b, "}}");
    }
    ash_buf_append_byte(b, ']');
}

static void append_openai_call(ash_buf *b, const ash_msg *m)
{
    ash_buf_append_cstr(b, "{\"id\":");
    ash_json_quote_cstr(b, m->tool_id ? m->tool_id : "");
    ash_buf_append_cstr(b, ",\"type\":\"function\",\"function\":{\"name\":");
    ash_json_quote_cstr(b, m->tool_name);
    ash_buf_append_cstr(b, ",\"arguments\":");
    ash_json_quote_cstr(b, m->tool_input ? m->tool_input : "{}");
    ash_buf_append_cstr(b, "}}");
}

static int same_role_calls(const ash_msg *a, const ash_msg *b)
{
    if (a->tool_name == NULL || b->tool_name == NULL)
        return 0;
    const char *ra = a->role ? a->role : "assistant";
    const char *rb = b->role ? b->role : "assistant";
    return strcmp(ra, rb) == 0;
}

static void build_body_openai(ash_buf *b, const ash_provider_cfg *cfg,
                              const ash_msg *msgs, size_t nmsgs)
{
    ash_buf_append_cstr(b, "{\"model\":");
    ash_json_quote_cstr(b, cfg->model);
    ash_buf_append_cstr(b, ",\"max_tokens\":");
    append_int(b, cfg->max_tokens > 0 ? cfg->max_tokens : 1024);
    ash_buf_append_cstr(b, ",\"stream\":true");
    ash_buf_append_cstr(b, ",\"stream_options\":{\"include_usage\":true}");
    if (cfg->tools != NULL) {
        ash_buf_append_cstr(b, ",\"tools\":");
        build_openai_tools(b, cfg->tools);
    }
    ash_buf_append_cstr(b, ",\"messages\":[");
    int wrote = 0;
    if (cfg->system != NULL) {
        ash_buf_append_cstr(b, "{\"role\":\"system\",\"content\":");
        ash_json_quote_cstr(b, cfg->system);
        ash_buf_append_byte(b, '}');
        wrote = 1;
    }
    size_t i = 0;
    while (i < nmsgs) {
        const ash_msg *m = &msgs[i];
        assert(!(m->tool_name != NULL && m->tool_result != NULL));
        if (wrote)
            ash_buf_append_byte(b, ',');
        wrote = 1;
        if (m->tool_name != NULL) {
            ash_buf_append_cstr(b, "{\"role\":");
            ash_json_quote_cstr(b, m->role ? m->role : "assistant");
            ash_buf_append_cstr(b, ",\"content\":");
            if (m->content != NULL && m->content[0] != 0) {
                ash_json_quote_cstr(b, m->content);
            } else {
                ash_buf_append_cstr(b, "null");
            }
            ash_buf_append_cstr(b, ",\"tool_calls\":[");
            append_openai_call(b, m);
            size_t j = i + 1;
            while (j < nmsgs && same_role_calls(m, &msgs[j])) {
                ash_buf_append_byte(b, ',');
                append_openai_call(b, &msgs[j]);
                j++;
            }
            ash_buf_append_cstr(b, "]}");
            i = j;
        } else if (m->tool_result != NULL) {
            ash_buf_append_cstr(b, "{\"role\":\"tool\",\"tool_call_id\":");
            ash_json_quote_cstr(b, m->tool_id ? m->tool_id : "");
            ash_buf_append_cstr(b, ",\"content\":");
            ash_json_quote_cstr(b, m->tool_result);
            ash_buf_append_byte(b, '}');
            i++;
        } else {
            ash_buf_append_cstr(b, "{\"role\":");
            ash_json_quote_cstr(b, m->role ? m->role : "user");
            ash_buf_append_cstr(b, ",\"content\":");
            ash_json_quote_cstr(b, m->content ? m->content : "");
            ash_buf_append_byte(b, '}');
            i++;
        }
    }
    ash_buf_append_cstr(b, "]}");
}

ash_status ash_provider_build_body(ash_arena *a, const ash_provider_cfg *cfg,
                                   const ash_msg *msgs, size_t nmsgs,
                                   const char **out, size_t *out_len)
{
    if (a == NULL || cfg == NULL || cfg->model == NULL ||
        out == NULL || out_len == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider: bad arguments");
    if (nmsgs > 0 && msgs == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider: null message array");

    ash_buf b;
    ash_buf_init(&b, a);
    switch (cfg_kind(cfg)) {
    case ASH_PROVIDER_ANTHROPIC_MESSAGES:
        build_body_anthropic(&b, cfg, msgs, nmsgs);
        break;
    case ASH_PROVIDER_OPENAI_CHAT:
        build_body_openai(&b, cfg, msgs, nmsgs);
        break;
    }

    *out = (const char *)b.data;
    *out_len = b.len;
    return ASH_OK;
}

enum { PROV_MAX_TOOLS = 64 };

struct tool_slot {
    int64_t index;
    ash_buf id;
    ash_buf name;
    ash_buf input;
};

struct decoder {
    ash_sse_parser    sse;
    ash_arena        *json;
    ash_arena        *tools;
    ash_provider_kind kind;
    ash_delta_sink    on_text;
    void             *ud;
    char             *stop;
    size_t            stop_cap;
    ash_status        err;
    int               tool_count;
    struct tool_slot  tool[PROV_MAX_TOOLS];
    ash_ai_usage      usage;
};

static void copy_stop(struct decoder *d, ash_slice s)
{
    if (d->stop == NULL || d->stop_cap == 0)
        return;
    size_t n = s.len < d->stop_cap - 1 ? s.len : d->stop_cap - 1;
    memcpy(d->stop, s.p, n);
    d->stop[n] = 0;
}

static void set_stop_cstr(struct decoder *d, const char *s)
{
    copy_stop(d, ash_slice_from_cstr(s));
}

static void normalize_openai_stop(struct decoder *d, ash_slice s)
{
    if (ash_slice_eq_cstr(s, "stop"))
        set_stop_cstr(d, "end_turn");
    else if (ash_slice_eq_cstr(s, "tool_calls"))
        set_stop_cstr(d, "tool_use");
    else if (ash_slice_eq_cstr(s, "length"))
        set_stop_cstr(d, "max_tokens");
    else
        copy_stop(d, s);
}

static void buf_set(ash_buf *b, ash_slice s)
{
    b->len = 0;
    ash_buf_append(b, s.p, s.len);
    ash_buf_append_byte(b, 0);
    b->len = s.len;
}

static int64_t event_index(const ash_json *v)
{
    const ash_json *idx = ash_json_get(v, "index");
    int64_t bi = -1;
    if (idx != NULL) {
        int64_t tmp;
        if (ash_json_int64(idx, &tmp) == ASH_OK)
            bi = tmp;
    }
    return bi;
}

static void field_i64(const ash_json *o, const char *key, int64_t *dst)
{
    const ash_json *f = ash_json_get(o, key);
    int64_t v;
    if (f != NULL && ash_json_int64(f, &v) == ASH_OK)
        *dst = v;
}

static struct tool_slot *slot_get(struct decoder *d, int64_t idx, int create)
{
    for (int i = 0; i < d->tool_count; i++)
        if (d->tool[i].index == idx)
            return &d->tool[i];
    if (!create || d->tool_count >= PROV_MAX_TOOLS)
        return NULL;
    struct tool_slot *ts = &d->tool[d->tool_count];
    ash_buf_init(&ts->id, d->tools);
    ash_buf_init(&ts->name, d->tools);
    ash_buf_init(&ts->input, d->tools);
    ts->index = idx;
    d->tool_count++;
    return ts;
}

static void dispatch_anthropic(struct decoder *d, const ash_json *v)
{
    const ash_json *type = ash_json_get(v, "type");
    ash_slice t;
    if (type == NULL || ash_json_str(type, &t) != ASH_OK)
        return;

    if (ash_slice_eq_cstr(t, "message_start")) {
        const ash_json *msg = ash_json_get(v, "message");
        const ash_json *u = msg ? ash_json_get(msg, "usage") : NULL;
        if (u != NULL) {
            field_i64(u, "input_tokens", &d->usage.input_tokens);
            field_i64(u, "output_tokens", &d->usage.output_tokens);
            field_i64(u, "cache_creation_input_tokens",
                      &d->usage.cache_creation_input_tokens);
            field_i64(u, "cache_read_input_tokens",
                      &d->usage.cache_read_input_tokens);
        }
    } else if (ash_slice_eq_cstr(t, "content_block_start")) {
        const ash_json *cb = ash_json_get(v, "content_block");
        const ash_json *cbt = cb ? ash_json_get(cb, "type") : NULL;
        ash_slice cbts;
        if (cbt != NULL && ash_json_str(cbt, &cbts) == ASH_OK &&
            ash_slice_eq_cstr(cbts, "tool_use")) {
            struct tool_slot *ts = slot_get(d, event_index(v), 1);
            if (ts != NULL) {
                const ash_json *id = ash_json_get(cb, "id");
                const ash_json *name = ash_json_get(cb, "name");
                ash_slice s;
                if (id != NULL && ash_json_str(id, &s) == ASH_OK)
                    buf_set(&ts->id, s);
                if (name != NULL && ash_json_str(name, &s) == ASH_OK)
                    buf_set(&ts->name, s);
            }
        }
    } else if (ash_slice_eq_cstr(t, "content_block_delta")) {
        const ash_json *delta = ash_json_get(v, "delta");
        if (delta == NULL)
            return;
        const ash_json *dt = ash_json_get(delta, "type");
        ash_slice dts;
        if (dt != NULL && ash_json_str(dt, &dts) == ASH_OK &&
            ash_slice_eq_cstr(dts, "input_json_delta")) {
            struct tool_slot *ts = slot_get(d, event_index(v), 0);
            if (ts != NULL) {
                const ash_json *pj = ash_json_get(delta, "partial_json");
                ash_slice pjs;
                if (pj != NULL && ash_json_str(pj, &pjs) == ASH_OK)
                    ash_buf_append(&ts->input, pjs.p, pjs.len);
            }
            return;
        }
        const ash_json *text = ash_json_get(delta, "text");
        ash_slice ts;
        if (text != NULL && ash_json_str(text, &ts) == ASH_OK && ts.len > 0 &&
            d->on_text)
            d->on_text(d->ud, ts.p, ts.len);
    } else if (ash_slice_eq_cstr(t, "message_delta")) {
        const ash_json *delta = ash_json_get(v, "delta");
        const ash_json *sr = delta ? ash_json_get(delta, "stop_reason") : NULL;
        ash_slice s;
        if (sr != NULL && ash_json_str(sr, &s) == ASH_OK)
            copy_stop(d, s);
        const ash_json *u = ash_json_get(v, "usage");
        if (u != NULL) {
            field_i64(u, "input_tokens", &d->usage.input_tokens);
            field_i64(u, "output_tokens", &d->usage.output_tokens);
            field_i64(u, "cache_creation_input_tokens",
                      &d->usage.cache_creation_input_tokens);
            field_i64(u, "cache_read_input_tokens",
                      &d->usage.cache_read_input_tokens);
        }
    } else if (ash_slice_eq_cstr(t, "error")) {
        const ash_json *err = ash_json_get(v, "error");
        const ash_json *msg = err ? ash_json_get(err, "message") : NULL;
        ash_slice m;
        if (msg != NULL && ash_json_str(msg, &m) == ASH_OK) {
            int ml = (int)(m.len < 200 ? m.len : 200);
            d->err = ash_fail(ASH_ERR_PROTOCOL, "provider error: %.*s", ml, m.p);
        } else {
            d->err = ash_fail(ASH_ERR_PROTOCOL, "provider reported an error");
        }
    }
}

static void openai_tool_calls(struct decoder *d, const ash_json *calls)
{
    if (calls->type != ASH_JSON_ARRAY)
        return;
    for (size_t i = 0; i < calls->u.arr.n; i++) {
        const ash_json *tc = &calls->u.arr.v[i];
        struct tool_slot *ts = slot_get(d, event_index(tc), 1);
        if (ts == NULL)
            continue;
        const ash_json *id = ash_json_get(tc, "id");
        ash_slice s;
        if (id != NULL && ash_json_str(id, &s) == ASH_OK && s.len > 0 &&
            ts->id.len == 0)
            buf_set(&ts->id, s);
        const ash_json *fn = ash_json_get(tc, "function");
        if (fn == NULL)
            continue;
        const ash_json *name = ash_json_get(fn, "name");
        if (name != NULL && ash_json_str(name, &s) == ASH_OK && s.len > 0 &&
            ts->name.len == 0)
            buf_set(&ts->name, s);
        const ash_json *args = ash_json_get(fn, "arguments");
        if (args != NULL && ash_json_str(args, &s) == ASH_OK)
            ash_buf_append(&ts->input, s.p, s.len);
    }
}

static void dispatch_openai(struct decoder *d, const ash_json *v)
{
    const ash_json *choices = ash_json_get(v, "choices");
    if (choices != NULL && choices->type == ASH_JSON_ARRAY &&
        choices->u.arr.n > 0) {
        const ash_json *c0 = &choices->u.arr.v[0];
        const ash_json *delta = ash_json_get(c0, "delta");
        if (delta != NULL) {
            const ash_json *content = ash_json_get(delta, "content");
            ash_slice cs;
            if (content != NULL && ash_json_str(content, &cs) == ASH_OK &&
                cs.len > 0 && d->on_text)
                d->on_text(d->ud, cs.p, cs.len);
            const ash_json *calls = ash_json_get(delta, "tool_calls");
            if (calls != NULL)
                openai_tool_calls(d, calls);
        }
        const ash_json *fr = ash_json_get(c0, "finish_reason");
        ash_slice frs;
        if (fr != NULL && ash_json_str(fr, &frs) == ASH_OK)
            normalize_openai_stop(d, frs);
    }

    const ash_json *u = ash_json_get(v, "usage");
    if (u != NULL && u->type == ASH_JSON_OBJECT) {
        int64_t prompt = 0, completion = 0, cached = 0;
        field_i64(u, "prompt_tokens", &prompt);
        field_i64(u, "completion_tokens", &completion);
        field_i64(u, "prompt_cache_hit_tokens", &cached);
        const ash_json *det = ash_json_get(u, "prompt_tokens_details");
        if (det != NULL)
            field_i64(det, "cached_tokens", &cached);
        d->usage.cache_read_input_tokens = cached;
        d->usage.input_tokens = prompt - cached > 0 ? prompt - cached : prompt;
        d->usage.output_tokens = completion;
    }

    const ash_json *err = ash_json_get(v, "error");
    if (err != NULL && err->type == ASH_JSON_OBJECT) {
        const ash_json *msg = ash_json_get(err, "message");
        ash_slice m;
        if (msg != NULL && ash_json_str(msg, &m) == ASH_OK) {
            int ml = (int)(m.len < 200 ? m.len : 200);
            d->err = ash_fail(ASH_ERR_PROTOCOL, "provider error: %.*s", ml, m.p);
        } else {
            d->err = ash_fail(ASH_ERR_PROTOCOL, "provider reported an error");
        }
    }
}

static void on_event(void *ud, const ash_sse_event *ev)
{
    struct decoder *d = ud;
    if (d->err != ASH_OK || ev->data.len == 0)
        return;
    ash_arena_mark m = ash_arena_mark_get(d->json);
    ash_json v;
    if (ash_json_parse(d->json, ev->data.p, ev->data.len, &v) == ASH_OK) {
        switch (d->kind) {
        case ASH_PROVIDER_ANTHROPIC_MESSAGES:
            dispatch_anthropic(d, &v);
            break;
        case ASH_PROVIDER_OPENAI_CHAT:
            dispatch_openai(d, &v);
            break;
        }
    }
    ash_arena_rewind(d->json, m);
}

static void http_sink(void *ud, const char *data, size_t n)
{
    struct decoder *d = ud;
    if (d->err != ASH_OK)
        return;
    ash_status st = ash_sse_feed(&d->sse, data, n, on_event, d);
    if (st != ASH_OK)
        d->err = st;
}

struct ash_provider_stream {
    struct decoder d;
    ash_http      *h;
    ash_arena      json_arena;
    long           http_status;
};

ash_status ash_provider_start(ash_provider_stream **out, ash_arena *a,
                              const ash_provider_cfg *cfg,
                              const ash_msg *msgs, size_t nmsgs,
                              ash_delta_sink on_text, void *ud,
                              char *stop_reason, size_t stop_cap)
{
    if (out == NULL || cfg == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider: bad config");
    const ash_provider_desc *desc = cfg->provider ? cfg->provider
                                                   : ash_provider_default();
    const char *url = cfg->url ? cfg->url : desc->base_url;
    if (url == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider: no url");
    if (stop_reason != NULL && stop_cap > 0)
        stop_reason[0] = 0;

    const char *body = NULL;
    size_t blen = 0;
    ASH_TRY(ash_provider_build_body(a, cfg, msgs, nmsgs, &body, &blen));

    int use_oauth = cfg->oauth_token != NULL;
    const char *bearer = NULL;
    if (use_oauth) {
        bearer = cfg->oauth_token(cfg->oauth_ctx);
        if (bearer == NULL || bearer[0] == '\0')
            return ash_fail(ASH_ERR_STATE,
                            "no valid oauth token; run /login to sign in again");
    }

    ash_buf kb;
    ash_buf_init(&kb, a);
    if (use_oauth) {
        ash_buf_append_cstr(&kb, "Authorization: Bearer ");
        ash_buf_append_cstr(&kb, bearer);
    } else {
        ash_buf_append_cstr(&kb, desc->auth == ASH_AUTH_BEARER
                                     ? "Authorization: Bearer "
                                     : "x-api-key: ");
        ash_buf_append_cstr(&kb, cfg->api_key ? cfg->api_key : "");
    }
    ash_buf_append_byte(&kb, 0);

    const char *headers[6];
    size_t nh = 0;
    headers[nh++] = (const char *)kb.data;
    if (use_oauth)
        headers[nh++] = "anthropic-beta: oauth-2025-04-20";
    if (desc->api_version != NULL) {
        ash_buf vb;
        ash_buf_init(&vb, a);
        ash_buf_append_cstr(&vb, "anthropic-version: ");
        ash_buf_append_cstr(&vb, desc->api_version);
        ash_buf_append_byte(&vb, 0);
        headers[nh++] = (const char *)vb.data;
    }
    headers[nh++] = "content-type: application/json";
    headers[nh] = NULL;

    ash_provider_stream *s = ash_new(a, ash_provider_stream);
    memset(s, 0, sizeof *s);
    ash_sse_init(&s->d.sse, a);
    ASH_TRY(ash_arena_create(&s->json_arena, "prov-json", 1u << 16));
    s->d.json = &s->json_arena;
    s->d.kind = desc->kind;
    s->d.on_text = on_text;
    s->d.ud = ud;
    s->d.stop = stop_reason;
    s->d.stop_cap = stop_cap;
    s->d.err = ASH_OK;
    s->d.tools = a;
    s->d.tool_count = 0;

    ash_status st = ash_http_start(&s->h, a, url, body, blen, headers,
                                   http_sink, &s->d);
    if (st != ASH_OK) {
        ash_arena_destroy(&s->json_arena);
        return st;
    }
    *out = s;
    return ASH_OK;
}

ash_status ash_provider_wait(ash_provider_stream *s, int extra_fd,
                             int timeout_ms, int *extra_readable)
{
    if (s == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider_wait: bad arguments");
    return ash_http_wait(s->h, extra_fd, timeout_ms, extra_readable);
}

ash_status ash_provider_pump(ash_provider_stream *s, int *running)
{
    if (s == NULL || running == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider_pump: bad arguments");
    ash_status st = ash_http_perform(s->h, running, &s->http_status);
    if (st != ASH_OK)
        return st;
    return s->d.err;
}

ash_status ash_provider_finish(ash_provider_stream *s)
{
    if (s == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider_finish: bad arguments");
    if (s->d.err != ASH_OK)
        return s->d.err;
    if (s->http_status != 200)
        return ash_fail(ASH_ERR_PROTOCOL, "provider http status %ld", s->http_status);
    return ASH_OK;
}

ash_status ash_provider_tool_at(ash_provider_stream *s, int i, const char **id,
                                const char **name, const char **input,
                                size_t *input_len)
{
    if (s == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider_tool_at: bad arguments");
    if (i < 0 || i >= s->d.tool_count) {
        if (id)
            *id = NULL;
        if (name)
            *name = NULL;
        if (input)
            *input = NULL;
        if (input_len)
            *input_len = 0;
        return ASH_OK;
    }
    struct tool_slot *ts = &s->d.tool[i];
    if (id)
        *id = (const char *)ts->id.data;
    if (name)
        *name = (const char *)ts->name.data;
    if (input)
        *input = (const char *)ts->input.data;
    if (input_len)
        *input_len = ts->input.len;
    return ASH_OK;
}

ash_status ash_provider_tool_use(ash_provider_stream *s, const char **id,
                                 const char **name, const char **input,
                                 size_t *input_len)
{
    return ash_provider_tool_at(s, 0, id, name, input, input_len);
}

int ash_provider_tool_count(const ash_provider_stream *s)
{
    return s == NULL ? 0 : s->d.tool_count;
}

ash_status ash_provider_usage(const ash_provider_stream *s, ash_ai_usage *out)
{
    if (s == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "provider_usage: bad arguments");
    *out = s->d.usage;
    return ASH_OK;
}

void ash_provider_stream_close(ash_provider_stream *s)
{
    if (s == NULL)
        return;
    ash_http_close(s->h);
    ash_arena_destroy(&s->json_arena);
}

ash_status ash_provider_turn(ash_arena *a, const ash_provider_cfg *cfg,
                             const ash_msg *msgs, size_t nmsgs,
                             ash_delta_sink on_text, void *ud,
                             char *stop_reason, size_t stop_cap)
{
    ash_provider_stream *s = NULL;
    ASH_TRY(ash_provider_start(&s, a, cfg, msgs, nmsgs, on_text, ud,
                               stop_reason, stop_cap));

    ash_status st = ash_http_run(s->h, &s->http_status);
    ash_status derr = s->d.err;
    long http_status = s->http_status;
    ash_provider_stream_close(s);

    if (derr != ASH_OK)
        return derr;
    if (st != ASH_OK)
        return st;
    if (http_status != 200)
        return ash_fail(ASH_ERR_PROTOCOL, "provider http status %ld", http_status);
    return ASH_OK;
}
