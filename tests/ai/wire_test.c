#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#include "ash/ai/http.h"
#include "ash/ai/provider.h"
#include "ash/base/arena.h"
#include "ash/base/json.h"
#include "ash/base/slice.h"
#include "ash_test.h"

static const char ANTH_USAGE[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\","
    "\"usage\":{\"input_tokens\":12,\"output_tokens\":1,"
    "\"cache_creation_input_tokens\":4,\"cache_read_input_tokens\":8}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hi\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"},"
    "\"usage\":{\"output_tokens\":7}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char OAI_TEXT[] =
    "data: {\"id\":\"x\",\"choices\":[{\"index\":0,"
    "\"delta\":{\"role\":\"assistant\",\"content\":\"\"}}]}\n"
    "\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\"Hello\"}}]}\n"
    "\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"content\":\" world\"}}]}\n"
    "\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{},\"finish_reason\":\"stop\"}]}\n"
    "\n"
    "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":20,"
    "\"completion_tokens\":5,\"prompt_cache_hit_tokens\":6}}\n"
    "\n"
    "data: [DONE]\n"
    "\n";

static const char OAI_TOOL[] =
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,"
    "\"id\":\"call_1\",\"type\":\"function\",\"function\":{\"name\":\"bash\","
    "\"arguments\":\"\"}}]}}]}\n"
    "\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,"
    "\"function\":{\"arguments\":\"{\\\"command\\\":\"}}]}}]}\n"
    "\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":[{\"index\":0,"
    "\"function\":{\"arguments\":\" \\\"ls\\\"}\"}}]}}]}\n"
    "\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{},"
    "\"finish_reason\":\"tool_calls\"}]}\n"
    "\n"
    "data: {\"choices\":[],\"usage\":{\"prompt_tokens\":30,"
    "\"completion_tokens\":10}}\n"
    "\n"
    "data: [DONE]\n"
    "\n";

static const char OAI_PARALLEL[] =
    "data: {\"choices\":[{\"index\":0,\"delta\":{\"tool_calls\":["
    "{\"index\":0,\"id\":\"call_a\",\"type\":\"function\","
    "\"function\":{\"name\":\"bash\",\"arguments\":\"{\\\"cmd\\\":\\\"ls\\\"}\"}},"
    "{\"index\":1,\"id\":\"call_b\",\"type\":\"function\","
    "\"function\":{\"name\":\"py\",\"arguments\":\"{\\\"c\\\":1}\"}}]}}]}\n"
    "\n"
    "data: {\"choices\":[{\"index\":0,\"delta\":{},"
    "\"finish_reason\":\"tool_calls\"}]}\n"
    "\n"
    "data: [DONE]\n"
    "\n";

struct sink {
    char   buf[4096];
    size_t len;
};

static void on_text(void *ud, const char *text, size_t n)
{
    struct sink *s = ud;
    if (s->len + n <= sizeof s->buf) {
        memcpy(s->buf + s->len, text, n);
        s->len += n;
    }
}

static void write_all(int c, const char *p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(c, p + off, n - off);
        if (w <= 0)
            break;
        off += (size_t)w;
    }
}

static void serve(int lfd, const char *body)
{
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        return;
    char req[2048];
    (void)read(c, req, sizeof req);
    char hdr[256];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 200 OK\r\n"
                      "Content-Type: text/event-stream\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      strlen(body));
    if (hn > 0) {
        write_all(c, hdr, (size_t)hn);
        write_all(c, body, strlen(body));
    }
    close(c);
}

static ash_status drive(ash_arena *a, const ash_provider_cfg *cfg,
                        const ash_msg *msgs, size_t nmsgs, struct sink *s,
                        char *stop, size_t cap, ash_ai_usage *usage,
                        const char **tid, const char **tname,
                        const char **tinput, size_t *tilen, int *tcount)
{
    ash_provider_stream *ps = NULL;
    ASH_TRY(ash_provider_start(&ps, a, cfg, msgs, nmsgs, on_text, s, stop, cap));
    int running = 1;
    ash_status st = ASH_OK;
    while (running) {
        st = ash_provider_wait(ps, -1, 1000, NULL);
        if (st != ASH_OK)
            break;
        st = ash_provider_pump(ps, &running);
        if (st != ASH_OK)
            break;
    }
    ash_status fin = ash_provider_finish(ps);
    if (usage != NULL)
        ASH_CHECK(ash_provider_usage(ps, usage) == ASH_OK);
    if (tid != NULL)
        ASH_CHECK(ash_provider_tool_use(ps, tid, tname, tinput, tilen) == ASH_OK);
    if (tcount != NULL)
        *tcount = ash_provider_tool_count(ps);
    ash_provider_stream_close(ps);
    return st != ASH_OK ? st : fin;
}

static void test_openai_tools_edge(ash_arena *a)
{
    ash_provider_cfg cfg = {
        .provider = ash_provider_find("openai"), .model = "gpt-4o",
        .max_tokens = 32,
        .tools = "[{\"name\":\"noop\"},{\"description\":\"no name, skipped\"}]",
    };
    const char *body = NULL;
    size_t blen = 0;
    ASH_CHECK(ash_provider_build_body(a, &cfg, NULL, 0, &body, &blen) == ASH_OK);

    ash_json v;
    ASH_CHECK(ash_json_parse(a, body, blen, &v) == ASH_OK);
    const ash_json *tools = ash_json_get(&v, "tools");
    ASH_CHECK(tools != NULL && tools->type == ASH_JSON_ARRAY && tools->u.arr.n == 1);
    const ash_json *fn = ash_json_get(&tools->u.arr.v[0], "function");
    ash_slice sl;
    ASH_CHECK(ash_json_str(ash_json_get(fn, "name"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "noop"));
    ASH_CHECK(ash_json_get(fn, "description") == NULL);
    const ash_json *params = ash_json_get(fn, "parameters");
    ASH_CHECK(params != NULL && ash_json_str(ash_json_get(params, "type"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "object"));
}

static void test_openai_build_body(ash_arena *a)
{
    ash_provider_cfg cfg = {
        .provider = ash_provider_find("deepseek"),
        .model = "deepseek-chat", .max_tokens = 128, .system = "be brief",
        .tools = "[{\"name\":\"bash\",\"description\":\"run a shell command\","
                 "\"input_schema\":{\"type\":\"object\",\"properties\":"
                 "{\"cmd\":{\"type\":\"string\"}},\"required\":[\"cmd\"]}}]",
    };
    ASH_CHECK(cfg.provider != NULL);
    ash_msg msgs[] = {
        { .role = "user", .content = "run ls" },
        { .role = "assistant", .content = "sure", .tool_id = "call_9",
          .tool_name = "bash", .tool_input = "{\"cmd\":\"ls\"}" },
        { .role = "assistant", .tool_id = "call_10",
          .tool_name = "bash", .tool_input = "{\"cmd\":\"pwd\"}" },
        { .role = "tool", .tool_id = "call_9", .tool_result = "file1" },
        { .role = "tool", .tool_id = "call_10", .tool_result = "/home" },
    };
    const char *body = NULL;
    size_t blen = 0;
    ASH_CHECK(ash_provider_build_body(a, &cfg, msgs, 5, &body, &blen) == ASH_OK);

    ash_json v;
    ASH_CHECK(ash_json_parse(a, body, blen, &v) == ASH_OK && v.type == ASH_JSON_OBJECT);
    ash_slice sl;
    ASH_CHECK(ash_json_str(ash_json_get(&v, "model"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "deepseek-chat"));
    const ash_json *so = ash_json_get(&v, "stream_options");
    ASH_CHECK(so != NULL && ash_json_get(so, "include_usage")->u.boolean == 1);

    const ash_json *tools = ash_json_get(&v, "tools");
    ASH_CHECK(tools != NULL && tools->type == ASH_JSON_ARRAY && tools->u.arr.n == 1);
    const ash_json *t0 = &tools->u.arr.v[0];
    ASH_CHECK(ash_json_str(ash_json_get(t0, "type"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "function"));
    const ash_json *tfn = ash_json_get(t0, "function");
    ASH_CHECK(tfn != NULL && ash_json_get(t0, "input_schema") == NULL);
    ASH_CHECK(ash_json_str(ash_json_get(tfn, "name"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "bash"));
    ASH_CHECK(ash_json_str(ash_json_get(tfn, "description"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "run a shell command"));
    const ash_json *params = ash_json_get(tfn, "parameters");
    ASH_CHECK(params != NULL && ash_json_str(ash_json_get(params, "type"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "object"));
    const ash_json *props = ash_json_get(params, "properties");
    ASH_CHECK(props != NULL && ash_json_get(props, "cmd") != NULL);
    const ash_json *req = ash_json_get(params, "required");
    ASH_CHECK(req != NULL && req->type == ASH_JSON_ARRAY && req->u.arr.n == 1 &&
              ash_json_str(&req->u.arr.v[0], &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "cmd"));

    const ash_json *arr = ash_json_get(&v, "messages");
    ASH_CHECK(arr != NULL && arr->type == ASH_JSON_ARRAY && arr->u.arr.n == 5);
    ASH_CHECK(ash_json_str(ash_json_get(&arr->u.arr.v[0], "role"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "system"));

    const ash_json *am = &arr->u.arr.v[2];
    ASH_CHECK(ash_json_str(ash_json_get(am, "role"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "assistant"));
    ASH_CHECK(ash_json_str(ash_json_get(am, "content"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "sure"));
    const ash_json *calls = ash_json_get(am, "tool_calls");
    ASH_CHECK(calls != NULL && calls->type == ASH_JSON_ARRAY && calls->u.arr.n == 2);
    const ash_json *fn0 = ash_json_get(&calls->u.arr.v[0], "function");
    ASH_CHECK(ash_json_str(ash_json_get(&calls->u.arr.v[0], "id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "call_9"));
    ASH_CHECK(ash_json_str(ash_json_get(fn0, "name"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "bash"));
    ASH_CHECK(ash_json_str(ash_json_get(fn0, "arguments"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "{\"cmd\":\"ls\"}"));
    ASH_CHECK(ash_json_str(ash_json_get(&calls->u.arr.v[1], "id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "call_10"));

    const ash_json *rm = &arr->u.arr.v[3];
    ASH_CHECK(ash_json_str(ash_json_get(rm, "role"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "tool"));
    ASH_CHECK(ash_json_str(ash_json_get(rm, "tool_call_id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "call_9"));
    ASH_CHECK(ash_json_str(ash_json_get(rm, "content"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "file1"));

    const ash_json *rm2 = &arr->u.arr.v[4];
    ASH_CHECK(ash_json_str(ash_json_get(rm2, "role"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "tool"));
    ASH_CHECK(ash_json_str(ash_json_get(rm2, "tool_call_id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "call_10"));
}

int main(void)
{
    ASH_CHECK(ash_http_global_init() == ASH_OK);
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "wire", 1u << 16) == ASH_OK);

    test_openai_tools_edge(&a);
    test_openai_build_body(&a);

    int lfd = socket(AF_INET, SOCK_STREAM, 0);
    ASH_CHECK(lfd >= 0);
    int one = 1;
    (void)setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    ASH_CHECK(bind(lfd, (struct sockaddr *)&addr, sizeof addr) == 0);
    socklen_t al = sizeof addr;
    ASH_CHECK(getsockname(lfd, (struct sockaddr *)&addr, &al) == 0);
    int port = ntohs(addr.sin_port);
    ASH_CHECK(listen(lfd, 8) == 0);

    pid_t pid = fork();
    ASH_CHECK(pid >= 0);
    if (pid == 0) {
        serve(lfd, ANTH_USAGE);
        serve(lfd, OAI_TEXT);
        serve(lfd, OAI_TOOL);
        serve(lfd, OAI_PARALLEL);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_msg msgs[] = { { .role = "user", .content = "hi" } };

    {
        ash_provider_cfg cfg = {
            .provider = ash_provider_find("anthropic"), .url = url,
            .api_key = "k", .model = "claude-x", .max_tokens = 64,
        };
        struct sink s = { .len = 0 };
        char stop[32];
        ash_ai_usage u = { 0 };
        ASH_CHECK(drive(&a, &cfg, msgs, 1, &s, stop, sizeof stop, &u,
                        NULL, NULL, NULL, NULL, NULL) == ASH_OK);
        ASH_CHECK(s.len == 2 && memcmp(s.buf, "Hi", 2) == 0);
        ASH_CHECK_STREQ(stop, "end_turn");
        ASH_CHECK(u.input_tokens == 12 && u.output_tokens == 7 &&
                  u.cache_creation_input_tokens == 4 &&
                  u.cache_read_input_tokens == 8);
    }

    {
        ash_provider_cfg cfg = {
            .provider = ash_provider_find("deepseek"), .url = url,
            .api_key = "k", .model = "deepseek-chat", .max_tokens = 64,
        };
        struct sink s = { .len = 0 };
        char stop[32];
        ash_ai_usage u = { 0 };
        ASH_CHECK(drive(&a, &cfg, msgs, 1, &s, stop, sizeof stop, &u,
                        NULL, NULL, NULL, NULL, NULL) == ASH_OK);
        ASH_CHECK(s.len == strlen("Hello world") &&
                  memcmp(s.buf, "Hello world", s.len) == 0);
        ASH_CHECK_STREQ(stop, "end_turn");
        ASH_CHECK(u.cache_read_input_tokens == 6 && u.input_tokens == 14 &&
                  u.output_tokens == 5);
    }

    {
        ash_provider_cfg cfg = {
            .provider = ash_provider_find("openai"), .url = url,
            .api_key = "k", .model = "gpt-4o", .max_tokens = 64,
        };
        struct sink s = { .len = 0 };
        char stop[32];
        ash_ai_usage u = { 0 };
        const char *tid = NULL, *tname = NULL, *tinput = NULL;
        size_t tilen = 0;
        int tcount = 0;
        ASH_CHECK(drive(&a, &cfg, msgs, 1, &s, stop, sizeof stop, &u,
                        &tid, &tname, &tinput, &tilen, &tcount) == ASH_OK);
        ASH_CHECK_STREQ(stop, "tool_use");
        ASH_CHECK(tid != NULL && strcmp(tid, "call_1") == 0);
        ASH_CHECK(tname != NULL && strcmp(tname, "bash") == 0);
        ASH_CHECK(tilen == strlen("{\"command\": \"ls\"}") &&
                  memcmp(tinput, "{\"command\": \"ls\"}", tilen) == 0);
        ASH_CHECK(tcount == 1);
        ASH_CHECK(u.input_tokens == 30 && u.output_tokens == 10);
    }

    {
        ash_provider_cfg cfg = {
            .provider = ash_provider_find("openai"), .url = url,
            .api_key = "k", .model = "gpt-4o", .max_tokens = 64,
        };
        struct sink s = { .len = 0 };
        char stop[32];
        const char *tid = NULL, *tname = NULL, *tinput = NULL;
        size_t tilen = 0;
        int tcount = 0;
        ASH_CHECK(drive(&a, &cfg, msgs, 1, &s, stop, sizeof stop, NULL,
                        &tid, &tname, &tinput, &tilen, &tcount) == ASH_OK);
        ASH_CHECK(tid != NULL && strcmp(tid, "call_a") == 0);
        ASH_CHECK(tname != NULL && strcmp(tname, "bash") == 0);
        ASH_CHECK(tilen == strlen("{\"cmd\":\"ls\"}") &&
                  memcmp(tinput, "{\"cmd\":\"ls\"}", tilen) == 0);
        ASH_CHECK(tcount == 2);
    }

    ash_arena_destroy(&a);
    ash_http_global_cleanup();
    (void)waitpid(pid, NULL, 0);
    return ash_test_done();
}
