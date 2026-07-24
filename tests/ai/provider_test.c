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

static const char HAPPY[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"Hello, \"}}\n"
    "\n"
    "data: [DONE]\n"
    "\n"
    "data: {not valid json\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,"
    "\"delta\":{\"type\":\"text_delta\",\"text\":\"world\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"end_turn\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char ERR[] =
    "event: error\n"
    "data: {\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\","
    "\"message\":\"Overloaded\"}}\n"
    "\n";

static const char TOOL[] =
    "event: message_start\n"
    "data: {\"type\":\"message_start\",\"message\":{\"role\":\"assistant\"}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"command\\\":\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":\" \\\"ls\\\"}\"}}\n"
    "\n"
    "event: content_block_stop\n"
    "data: {\"type\":\"content_block_stop\",\"index\":0}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
    "\n";

static const char TOOL2[] =
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":0,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_a\",\"name\":\"bash\",\"input\":{}}}\n"
    "\n"
    "event: content_block_start\n"
    "data: {\"type\":\"content_block_start\",\"index\":1,\"content_block\":"
    "{\"type\":\"tool_use\",\"id\":\"toolu_b\",\"name\":\"python\",\"input\":{}}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":1,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"code\\\":1}\"}}\n"
    "\n"
    "event: content_block_delta\n"
    "data: {\"type\":\"content_block_delta\",\"index\":0,\"delta\":"
    "{\"type\":\"input_json_delta\",\"partial_json\":\"{\\\"cmd\\\":\\\"ls\\\"}\"}}\n"
    "\n"
    "event: message_delta\n"
    "data: {\"type\":\"message_delta\",\"delta\":{\"stop_reason\":\"tool_use\"}}\n"
    "\n"
    "event: message_stop\n"
    "data: {\"type\":\"message_stop\"}\n"
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

static void serve(int lfd, const char *status, const char *ctype, const char *body)
{
    int c = accept(lfd, NULL, NULL);
    if (c < 0)
        return;
    char req[2048];
    (void)read(c, req, sizeof req);
    char hdr[256];
    int hn = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 %s\r\n"
                      "Content-Type: %s\r\n"
                      "Content-Length: %zu\r\n"
                      "Connection: close\r\n\r\n",
                      status, ctype, strlen(body));
    if (hn > 0) {
        write_all(c, hdr, (size_t)hn);
        write_all(c, body, strlen(body));
    }
    close(c);
}

static ash_status stream_turn(ash_arena *a, const ash_provider_cfg *cfg,
                              const ash_msg *msgs, size_t nmsgs,
                              ash_delta_sink on, void *ud,
                              char *stop, size_t cap)
{
    ash_provider_stream *ps = NULL;
    ash_status st = ash_provider_start(&ps, a, cfg, msgs, nmsgs, on, ud, stop, cap);
    if (st != ASH_OK)
        return st;
    int running = 1;
    while (running) {
        st = ash_provider_wait(ps, -1, 1000, NULL);
        if (st != ASH_OK)
            break;
        st = ash_provider_pump(ps, &running);
        if (st != ASH_OK)
            break;
    }
    ash_status fin = ash_provider_finish(ps);
    ash_provider_stream_close(ps);
    return st != ASH_OK ? st : fin;
}

static void test_build_body(ash_arena *a)
{
    ash_provider_cfg cfg = {
        .url = "http://x", .api_key = "k", .model = "claude-x",
        .max_tokens = 100, .system = "be nice",
    };
    const char *content = "hi \"there\"\nline2";
    ash_msg msgs[] = { { .role = "user", .content = content } };

    const char *body = NULL;
    size_t blen = 0;
    ASH_CHECK(ash_provider_build_body(a, &cfg, msgs, 1, &body, &blen) == ASH_OK);

    ash_json v;
    ASH_CHECK(ash_json_parse(a, body, blen, &v) == ASH_OK && v.type == ASH_JSON_OBJECT);

    const ash_json *model = ash_json_get(&v, "model");
    ash_slice ms;
    ASH_CHECK(model != NULL && ash_json_str(model, &ms) == ASH_OK &&
              ash_slice_eq_cstr(ms, "claude-x"));

    const ash_json *mt = ash_json_get(&v, "max_tokens");
    int64_t n = 0;
    ASH_CHECK(mt != NULL && ash_json_int64(mt, &n) == ASH_OK && n == 100);

    const ash_json *stream = ash_json_get(&v, "stream");
    ASH_CHECK(stream != NULL && stream->type == ASH_JSON_BOOL && stream->u.boolean == 1);

    const ash_json *sys = ash_json_get(&v, "system");
    ash_slice ss;
    ASH_CHECK(sys != NULL && ash_json_str(sys, &ss) == ASH_OK &&
              ash_slice_eq_cstr(ss, "be nice"));

    const ash_json *arr = ash_json_get(&v, "messages");
    ASH_CHECK(arr != NULL && arr->type == ASH_JSON_ARRAY && arr->u.arr.n == 1);

    const ash_json *m0 = &arr->u.arr.v[0];
    const ash_json *role = ash_json_get(m0, "role");
    const ash_json *cnt = ash_json_get(m0, "content");
    ash_slice rs, cs;
    ASH_CHECK(role != NULL && ash_json_str(role, &rs) == ASH_OK &&
              ash_slice_eq_cstr(rs, "user"));
    ASH_CHECK(cnt != NULL && ash_json_str(cnt, &cs) == ASH_OK &&
              cs.len == strlen(content) && memcmp(cs.p, content, cs.len) == 0);
}

static void test_build_body_tools(ash_arena *a)
{
    ash_provider_cfg cfg = {
        .url = "http://x", .api_key = "k", .model = "m", .max_tokens = 100,
        .tools = "[{\"name\":\"bash\",\"input_schema\":{\"type\":\"object\"}}]",
    };
    ash_msg msgs[] = {
        { .role = "user", .content = "run ls" },
        { .role = "assistant", .tool_id = "toolu_9", .tool_name = "bash",
          .tool_input = "{\"command\":\"ls\"}" },
        { .role = "user", .tool_id = "toolu_9", .tool_result = "file1\nfile2" },
    };
    const char *body = NULL;
    size_t blen = 0;
    ASH_CHECK(ash_provider_build_body(a, &cfg, msgs, 3, &body, &blen) == ASH_OK);

    ash_json v;
    ASH_CHECK(ash_json_parse(a, body, blen, &v) == ASH_OK);

    const ash_json *tools = ash_json_get(&v, "tools");
    ASH_CHECK(tools != NULL && tools->type == ASH_JSON_ARRAY && tools->u.arr.n == 1);

    const ash_json *arr = ash_json_get(&v, "messages");
    ASH_CHECK(arr != NULL && arr->type == ASH_JSON_ARRAY && arr->u.arr.n == 3);

    const ash_json *blk = &ash_json_get(&arr->u.arr.v[1], "content")->u.arr.v[0];
    ash_slice sl;
    ASH_CHECK(ash_json_str(ash_json_get(blk, "type"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "tool_use"));
    ASH_CHECK(ash_json_str(ash_json_get(blk, "name"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "bash"));
    const ash_json *cmd = ash_json_get(ash_json_get(blk, "input"), "command");
    ASH_CHECK(cmd != NULL && ash_json_str(cmd, &sl) == ASH_OK && ash_slice_eq_cstr(sl, "ls"));

    const ash_json *rblk = &ash_json_get(&arr->u.arr.v[2], "content")->u.arr.v[0];
    ASH_CHECK(ash_json_str(ash_json_get(rblk, "type"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "tool_result"));
    ASH_CHECK(ash_json_str(ash_json_get(rblk, "tool_use_id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "toolu_9"));
    ASH_CHECK(ash_json_str(ash_json_get(rblk, "content"), &sl) == ASH_OK &&
              sl.len == strlen("file1\nfile2") && memcmp(sl.p, "file1\nfile2", sl.len) == 0);
}

static void test_build_body_parallel(ash_arena *a)
{
    ash_provider_cfg cfg = {
        .url = "http://x", .api_key = "k", .model = "m", .max_tokens = 100,
    };
    ash_msg msgs[] = {
        { .role = "user", .content = "go" },
        { .role = "assistant", .content = "working", .tool_id = "t1",
          .tool_name = "bash", .tool_input = "{\"command\":\"a\"}" },
        { .role = "assistant", .tool_id = "t2",
          .tool_name = "bash", .tool_input = "{\"command\":\"b\"}" },
        { .role = "user", .tool_id = "t1", .tool_result = "ra" },
        { .role = "user", .tool_id = "t2", .tool_result = "rb", .tool_is_error = 1 },
    };
    const char *body = NULL;
    size_t blen = 0;
    ASH_CHECK(ash_provider_build_body(a, &cfg, msgs, 5, &body, &blen) == ASH_OK);

    ash_json v;
    ASH_CHECK(ash_json_parse(a, body, blen, &v) == ASH_OK);
    const ash_json *arr = ash_json_get(&v, "messages");
    ASH_CHECK(arr != NULL && arr->type == ASH_JSON_ARRAY && arr->u.arr.n == 3);

    const ash_json *ac = ash_json_get(&arr->u.arr.v[1], "content");
    ASH_CHECK(ac != NULL && ac->type == ASH_JSON_ARRAY && ac->u.arr.n == 3);
    ash_slice sl;
    ASH_CHECK(ash_json_str(ash_json_get(&ac->u.arr.v[0], "type"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "text"));
    ASH_CHECK(ash_json_str(ash_json_get(&ac->u.arr.v[1], "id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "t1"));
    ASH_CHECK(ash_json_str(ash_json_get(&ac->u.arr.v[2], "id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "t2"));

    const ash_json *uc = ash_json_get(&arr->u.arr.v[2], "content");
    ASH_CHECK(uc != NULL && uc->type == ASH_JSON_ARRAY && uc->u.arr.n == 2);
    ASH_CHECK(ash_json_str(ash_json_get(&uc->u.arr.v[0], "tool_use_id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "t1"));
    const ash_json *e1 = ash_json_get(&uc->u.arr.v[1], "is_error");
    ASH_CHECK(e1 != NULL && e1->type == ASH_JSON_BOOL && e1->u.boolean == 1);
    ASH_CHECK(ash_json_get(&uc->u.arr.v[0], "is_error") == NULL);
}

static void test_build_body_empty_tool_result(ash_arena *a)
{
    ash_msg msgs[] = {
        { .role = "user", .content = "go" },
        { .role = "assistant", .tool_id = "t1",
          .tool_name = "bash", .tool_input = "{\"command\":\"true\"}" },
        { .role = "user", .tool_id = "t1", .tool_result = "" },
    };
    const char *body = NULL;
    size_t blen = 0;
    ash_slice sl;

    ash_provider_cfg acfg = {
        .url = "http://x", .api_key = "k", .model = "m", .max_tokens = 100,
    };
    ASH_CHECK(ash_provider_build_body(a, &acfg, msgs, 3, &body, &blen) == ASH_OK);
    ash_json v;
    ASH_CHECK(ash_json_parse(a, body, blen, &v) == ASH_OK);
    const ash_json *arr = ash_json_get(&v, "messages");
    ASH_CHECK(arr != NULL && arr->type == ASH_JSON_ARRAY && arr->u.arr.n == 3);
    const ash_json *uc = ash_json_get(&arr->u.arr.v[2], "content");
    ASH_CHECK(uc != NULL && uc->type == ASH_JSON_ARRAY && uc->u.arr.n == 1);
    ASH_CHECK(ash_json_str(ash_json_get(&uc->u.arr.v[0], "type"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "tool_result"));
    ASH_CHECK(ash_json_str(ash_json_get(&uc->u.arr.v[0], "tool_use_id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "t1"));
    ASH_CHECK(ash_json_str(ash_json_get(&uc->u.arr.v[0], "content"), &sl) == ASH_OK &&
              sl.len == 0);

    const ash_provider_desc *ds = ash_provider_find("deepseek");
    ASH_CHECK(ds != NULL);
    ash_provider_cfg ocfg = {
        .provider = ds, .model = "deepseek-chat", .max_tokens = 100,
    };
    body = NULL;
    blen = 0;
    ASH_CHECK(ash_provider_build_body(a, &ocfg, msgs, 3, &body, &blen) == ASH_OK);
    ASH_CHECK(ash_json_parse(a, body, blen, &v) == ASH_OK);
    arr = ash_json_get(&v, "messages");
    ASH_CHECK(arr != NULL && arr->type == ASH_JSON_ARRAY && arr->u.arr.n == 3);
    const ash_json *tm = &arr->u.arr.v[2];
    ASH_CHECK(ash_json_str(ash_json_get(tm, "role"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "tool"));
    ASH_CHECK(ash_json_str(ash_json_get(tm, "tool_call_id"), &sl) == ASH_OK &&
              ash_slice_eq_cstr(sl, "t1"));
    ASH_CHECK(ash_json_str(ash_json_get(tm, "content"), &sl) == ASH_OK &&
              sl.len == 0);
}

static void test_openai_build_body_identical(ash_arena *a)
{
    const ash_provider_desc *ds = ash_provider_find("deepseek");
    ASH_CHECK(ds != NULL);
    ash_provider_cfg cfg = {
        .provider = ds, .model = "deepseek-chat", .max_tokens = 100,
        .system = "be brief",
    };
    ash_msg msgs[] = { { .role = "user", .content = "hi" } };
    const char *body = NULL;
    size_t blen = 0;
    ASH_CHECK(ash_provider_build_body(a, &cfg, msgs, 1, &body, &blen) == ASH_OK);

    static const char expect[] =
        "{\"model\":\"deepseek-chat\",\"max_tokens\":100,\"stream\":true,"
        "\"stream_options\":{\"include_usage\":true},\"messages\":["
        "{\"role\":\"system\",\"content\":\"be brief\"},"
        "{\"role\":\"user\",\"content\":\"hi\"}]}";
    ASH_CHECK(blen == sizeof expect - 1 && memcmp(body, expect, blen) == 0);
}

static void test_tool_use(ash_arena *a, const char *url)
{
    ash_provider_cfg cfg = {
        .url = url, .api_key = "k", .model = "m", .max_tokens = 64,
        .tools = "[{\"name\":\"bash\",\"input_schema\":{\"type\":\"object\"}}]",
    };
    ash_msg msgs[] = { { .role = "user", .content = "list files" } };
    struct sink s = { .len = 0 };
    char stop[32];
    ash_provider_stream *ps = NULL;
    ASH_CHECK(ash_provider_start(&ps, a, &cfg, msgs, 1, on_text, &s, stop, sizeof stop) == ASH_OK);

    int running = 1;
    while (running) {
        ASH_CHECK(ash_provider_wait(ps, -1, 1000, NULL) == ASH_OK);
        ASH_CHECK(ash_provider_pump(ps, &running) == ASH_OK);
    }
    ASH_CHECK(ash_provider_finish(ps) == ASH_OK);
    ASH_CHECK(strcmp(stop, "tool_use") == 0);

    const char *id = NULL, *name = NULL, *input = NULL;
    size_t ilen = 0;
    ASH_CHECK(ash_provider_tool_use(ps, &id, &name, &input, &ilen) == ASH_OK);
    ASH_CHECK(id != NULL && strcmp(id, "toolu_1") == 0);
    ASH_CHECK(name != NULL && strcmp(name, "bash") == 0);
    ASH_CHECK(ilen == strlen("{\"command\": \"ls\"}") &&
              memcmp(input, "{\"command\": \"ls\"}", ilen) == 0);
    ash_provider_stream_close(ps);
}

static void test_parallel_tool(ash_arena *a, const char *url)
{
    ash_provider_cfg cfg = {
        .url = url, .api_key = "k", .model = "m", .max_tokens = 64,
        .tools = "[{\"name\":\"bash\",\"input_schema\":{\"type\":\"object\"}}]",
    };
    ash_msg msgs[] = { { .role = "user", .content = "go" } };
    struct sink s = { .len = 0 };
    char stop[32];
    ash_provider_stream *ps = NULL;
    ASH_CHECK(ash_provider_start(&ps, a, &cfg, msgs, 1, on_text, &s, stop, sizeof stop) == ASH_OK);

    int running = 1;
    while (running) {
        ASH_CHECK(ash_provider_wait(ps, -1, 1000, NULL) == ASH_OK);
        ASH_CHECK(ash_provider_pump(ps, &running) == ASH_OK);
    }
    ASH_CHECK(ash_provider_finish(ps) == ASH_OK);

    const char *id = NULL, *name = NULL, *input = NULL;
    size_t ilen = 0;
    ASH_CHECK(ash_provider_tool_use(ps, &id, &name, &input, &ilen) == ASH_OK);
    ASH_CHECK(id != NULL && strcmp(id, "toolu_a") == 0);
    ASH_CHECK(name != NULL && strcmp(name, "bash") == 0);
    ASH_CHECK(ilen == strlen("{\"cmd\":\"ls\"}") &&
              memcmp(input, "{\"cmd\":\"ls\"}", ilen) == 0);
    ASH_CHECK(ash_provider_tool_count(ps) == 2);

    const char *id2 = NULL, *name2 = NULL, *input2 = NULL;
    size_t ilen2 = 0;
    ASH_CHECK(ash_provider_tool_at(ps, 1, &id2, &name2, &input2, &ilen2) == ASH_OK);
    ASH_CHECK(id2 != NULL && strcmp(id2, "toolu_b") == 0);
    ASH_CHECK(name2 != NULL && strcmp(name2, "python") == 0);
    ASH_CHECK(ilen2 == strlen("{\"code\":1}") &&
              memcmp(input2, "{\"code\":1}", ilen2) == 0);
    ash_provider_stream_close(ps);
}

int main(void)
{
    ASH_CHECK(ash_http_global_init() == ASH_OK);

    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "prov", 1u << 16) == ASH_OK);

    test_build_body(&a);
    test_build_body_tools(&a);
    test_build_body_parallel(&a);
    test_build_body_empty_tool_result(&a);
    test_openai_build_body_identical(&a);

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

    const char *rate =
        "{\"type\":\"error\",\"error\":{\"message\":\"rate limit\"}}";
    pid_t pid = fork();
    ASH_CHECK(pid >= 0);
    if (pid == 0) {
        serve(lfd, "200 OK", "text/event-stream", HAPPY);
        serve(lfd, "200 OK", "text/event-stream", HAPPY);
        serve(lfd, "200 OK", "text/event-stream", ERR);
        serve(lfd, "200 OK", "text/event-stream", ERR);
        serve(lfd, "429 Too Many Requests", "application/json", rate);
        serve(lfd, "429 Too Many Requests", "application/json", rate);
        serve(lfd, "200 OK", "text/event-stream", TOOL);
        serve(lfd, "200 OK", "text/event-stream", TOOL2);
        close(lfd);
        _exit(0);
    }
    close(lfd);

    char url[64];
    (void)snprintf(url, sizeof url, "http://127.0.0.1:%d/", port);
    ash_provider_cfg cfg = {
        .url = url, .api_key = "k", .model = "claude-x", .max_tokens = 64,
    };
    ash_msg msgs[] = { { .role = "user", .content = "hello" } };

    char sb[32], ss[32];

    struct sink hb = { .len = 0 }, hs = { .len = 0 };
    ash_status happy_block =
        ash_provider_turn(&a, &cfg, msgs, 1, on_text, &hb, sb, sizeof sb);
    ash_status happy_stream =
        stream_turn(&a, &cfg, msgs, 1, on_text, &hs, ss, sizeof ss);
    ASH_CHECK(happy_block == ASH_OK && happy_stream == ASH_OK);
    ASH_CHECK(hb.len == strlen("Hello, world") &&
              memcmp(hb.buf, "Hello, world", hb.len) == 0 && strcmp(sb, "end_turn") == 0);
    ASH_CHECK(hs.len == hb.len && memcmp(hs.buf, hb.buf, hs.len) == 0 &&
              strcmp(ss, sb) == 0);

    struct sink eb = { .len = 0 }, es = { .len = 0 };
    ash_status err_block =
        ash_provider_turn(&a, &cfg, msgs, 1, on_text, &eb, sb, sizeof sb);
    ash_status err_stream =
        stream_turn(&a, &cfg, msgs, 1, on_text, &es, ss, sizeof ss);
    ASH_CHECK(err_block == ASH_ERR_PROTOCOL && err_block == err_stream);

    struct sink rb = { .len = 0 }, rs = { .len = 0 };
    ash_status rate_block =
        ash_provider_turn(&a, &cfg, msgs, 1, on_text, &rb, sb, sizeof sb);
    ash_status rate_stream =
        stream_turn(&a, &cfg, msgs, 1, on_text, &rs, ss, sizeof ss);
    ASH_CHECK(rate_block == ASH_ERR_PROTOCOL && rate_block == rate_stream);
    ASH_CHECK(strstr(ash_errbuf, "429") != NULL);

    test_tool_use(&a, url);
    test_parallel_tool(&a, url);

    ash_arena_destroy(&a);
    ash_http_global_cleanup();
    (void)waitpid(pid, NULL, 0);
    return ash_test_done();
}
