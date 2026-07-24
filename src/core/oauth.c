#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ash/ai/http.h"
#include "ash/base/base64.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/sha256.h"
#include "ash/core/oauth.h"
#include "ash/base/poison.h"

enum { ASH_OAUTH_REFRESH_SLACK = 60 };

int ash_oauth_needs_refresh(int64_t expires, int64_t now)
{
    return now >= expires - ASH_OAUTH_REFRESH_SLACK;
}

int ash_oauth_state_ok(const char *got, const char *want)
{
    return got != NULL && want != NULL && got[0] != '\0' &&
           strcmp(got, want) == 0;
}

static ash_status fill_random(uint8_t *buf, size_t n)
{
    int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return ash_fail(ASH_ERR_IO, "oauth: open urandom: %s", strerror(errno));
    size_t off = 0;
    while (off < n) {
        ssize_t r = read(fd, buf + off, n - off);
        if (r < 0) {
            if (errno == EINTR)
                continue;
            int e = errno;
            close(fd);
            return ash_fail(ASH_ERR_IO, "oauth: read urandom: %s", strerror(e));
        }
        if (r == 0) {
            close(fd);
            return ash_fail(ASH_ERR_IO, "oauth: urandom exhausted");
        }
        off += (size_t)r;
    }
    close(fd);
    return ASH_OK;
}

static int is_unreserved(unsigned char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
           c == '~';
}

static void url_encode(ash_buf *b, const char *s)
{
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        if (is_unreserved(c)) {
            ash_buf_append_byte(b, c);
            continue;
        }
        ash_buf_append_byte(b, '%');
        ash_buf_append_byte(b, (unsigned char)hex[c >> 4]);
        ash_buf_append_byte(b, (unsigned char)hex[c & 15]);
    }
}

static void json_str(ash_buf *b, const char *s)
{
    ash_buf_append_byte(b, '"');
    for (size_t i = 0; s[i] != '\0'; i++) {
        unsigned char c = (unsigned char)s[i];
        switch (c) {
        case '"':  ash_buf_append_cstr(b, "\\\""); break;
        case '\\': ash_buf_append_cstr(b, "\\\\"); break;
        case '\n': ash_buf_append_cstr(b, "\\n"); break;
        case '\r': ash_buf_append_cstr(b, "\\r"); break;
        case '\t': ash_buf_append_cstr(b, "\\t"); break;
        default:
            if (c < 0x20) {
                char esc[7];
                snprintf(esc, sizeof esc, "\\u%04x", c);
                ash_buf_append_cstr(b, esc);
            } else {
                ash_buf_append_byte(b, c);
            }
        }
    }
    ash_buf_append_byte(b, '"');
}

ash_status ash_oauth_pkce_begin(ash_arena *a, ash_oauth_pkce *out)
{
    if (a == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "oauth_pkce_begin: bad arguments");
    memset(out, 0, sizeof *out);

    uint8_t vraw[32];
    ASH_TRY(fill_random(vraw, sizeof vraw));
    char *verifier = ash_array(a, char, 44);
    if (ash_base64url_encode(vraw, sizeof vraw, verifier, 44) == 0)
        return ash_fail(ASH_ERR_STATE, "oauth: verifier encode");

    uint8_t digest[ASH_SHA256_DIGEST];
    ash_sha256(verifier, strlen(verifier), digest);
    char *challenge = ash_array(a, char, 44);
    if (ash_base64url_encode(digest, sizeof digest, challenge, 44) == 0)
        return ash_fail(ASH_ERR_STATE, "oauth: challenge encode");

    uint8_t sraw[24];
    ASH_TRY(fill_random(sraw, sizeof sraw));
    char *state = ash_array(a, char, 33);
    if (ash_base64url_encode(sraw, sizeof sraw, state, 33) == 0)
        return ash_fail(ASH_ERR_STATE, "oauth: state encode");

    ash_buf u;
    ash_buf_init(&u, a);
    ash_buf_append_cstr(&u, ASH_OAUTH_AUTHORIZE_URL);
    ash_buf_append_cstr(&u, "?code=true&client_id=" ASH_OAUTH_CLIENT_ID);
    ash_buf_append_cstr(&u, "&response_type=code&redirect_uri=");
    url_encode(&u, ASH_OAUTH_REDIRECT_URI);
    ash_buf_append_cstr(&u, "&scope=");
    url_encode(&u, ASH_OAUTH_SCOPE);
    ash_buf_append_cstr(&u, "&code_challenge=");
    ash_buf_append_cstr(&u, challenge);
    ash_buf_append_cstr(&u, "&code_challenge_method=S256&state=");
    ash_buf_append_cstr(&u, state);
    ash_buf_append_byte(&u, 0);

    out->verifier = verifier;
    out->challenge = challenge;
    out->state = state;
    out->url = (const char *)u.data;
    return ASH_OK;
}

static const char *json_dup_str(ash_arena *a, const ash_json *obj,
                                const char *key)
{
    const ash_json *j = ash_json_get(obj, key);
    ash_slice v;
    if (j == NULL || ash_json_str(j, &v) != ASH_OK)
        return NULL;
    char *s = ash_array(a, char, v.len + 1);
    if (v.len)
        memcpy(s, v.p, v.len);
    s[v.len] = '\0';
    return s;
}

static ash_status parse_token(ash_arena *a, ash_slice resp,
                              ash_oauth_token *out)
{
    memset(out, 0, sizeof *out);
    ash_json root;
    if (ash_json_parse(a, resp.p, resp.len, &root) != ASH_OK ||
        root.type != ASH_JSON_OBJECT)
        return ash_fail(ASH_ERR_PARSE, "oauth: token response not JSON");

    out->access = json_dup_str(a, &root, "access_token");
    if (out->access == NULL)
        return ash_fail(ASH_ERR_PROTOCOL, "oauth: response missing access_token");
    out->refresh = json_dup_str(a, &root, "refresh_token");

    int64_t expires_in = 0;
    const ash_json *e = ash_json_get(&root, "expires_in");
    if (e != NULL && ash_json_int64(e, &expires_in) != ASH_OK)
        expires_in = 0;
    out->expires = (int64_t)time(NULL) + expires_in;

    const ash_json *acct = ash_json_get(&root, "account");
    if (acct != NULL && acct->type == ASH_JSON_OBJECT) {
        out->account = json_dup_str(a, acct, "email_address");
        if (out->account == NULL)
            out->account = json_dup_str(a, acct, "email");
    }
    if (out->account == NULL) {
        const ash_json *org = ash_json_get(&root, "organization");
        if (org != NULL && org->type == ASH_JSON_OBJECT)
            out->account = json_dup_str(a, org, "name");
    }
    return ASH_OK;
}

static ash_status post_and_parse(const ash_oauth_http *http, ash_arena *a,
                                 const char *body, size_t blen,
                                 ash_oauth_token *out)
{
    long status = 0;
    ash_slice resp = { 0 };
    ASH_TRY(http->post_json(http->ctx, a, ASH_OAUTH_TOKEN_URL, body, blen,
                            &status, &resp));
    if (status < 200 || status >= 300)
        return ash_fail(ASH_ERR_PROTOCOL, "oauth: token endpoint http %ld",
                        status);
    return parse_token(a, resp, out);
}

ash_status ash_oauth_exchange(const ash_oauth_http *http, ash_arena *a,
                              const char *verifier, const char *code,
                              const char *state, ash_oauth_token *out)
{
    if (http == NULL || http->post_json == NULL || a == NULL ||
        verifier == NULL || code == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "oauth_exchange: bad arguments");

    ash_buf b;
    ash_buf_init(&b, a);
    ash_buf_append_cstr(&b, "{\"grant_type\":\"authorization_code\",\"code\":");
    json_str(&b, code);
    ash_buf_append_cstr(&b, ",\"state\":");
    json_str(&b, state != NULL ? state : "");
    ash_buf_append_cstr(&b, ",\"client_id\":\"" ASH_OAUTH_CLIENT_ID "\"");
    ash_buf_append_cstr(&b, ",\"redirect_uri\":\"" ASH_OAUTH_REDIRECT_URI "\"");
    ash_buf_append_cstr(&b, ",\"code_verifier\":");
    json_str(&b, verifier);
    ash_buf_append_byte(&b, '}');
    return post_and_parse(http, a, (const char *)b.data, b.len, out);
}

ash_status ash_oauth_refresh(const ash_oauth_http *http, ash_arena *a,
                             const char *refresh_token, ash_oauth_token *out)
{
    if (http == NULL || http->post_json == NULL || a == NULL ||
        refresh_token == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "oauth_refresh: bad arguments");

    ash_buf b;
    ash_buf_init(&b, a);
    ash_buf_append_cstr(&b, "{\"grant_type\":\"refresh_token\",\"refresh_token\":");
    json_str(&b, refresh_token);
    ash_buf_append_cstr(&b, ",\"client_id\":\"" ASH_OAUTH_CLIENT_ID "\"}");
    return post_and_parse(http, a, (const char *)b.data, b.len, out);
}

struct sink_ctx {
    ash_buf buf;
};

static void resp_sink(void *ud, const char *data, size_t n)
{
    struct sink_ctx *s = ud;
    ash_buf_append(&s->buf, data, n);
}

static ash_status curl_post_json(void *ctx, ash_arena *a, const char *url,
                                 const char *body, size_t body_len,
                                 long *http_status, ash_slice *resp)
{
    (void)ctx;
    struct sink_ctx sc;
    ash_buf_init(&sc.buf, a);
    const char *headers[] = { "content-type: application/json",
                              "accept: application/json", NULL };
    ash_http *h = NULL;
    ASH_TRY(ash_http_start(&h, a, url, body, body_len, headers, resp_sink, &sc));
    long status = 0;
    ash_status st = ash_http_run(h, &status);
    ash_http_close(h);
    if (st != ASH_OK)
        return st;
    if (http_status != NULL)
        *http_status = status;
    if (resp != NULL)
        *resp = ash_slice_make((const char *)sc.buf.data, sc.buf.len);
    return ASH_OK;
}

ash_oauth_http ash_oauth_default_http(void)
{
    ash_oauth_http h;
    h.post_json = curl_post_json;
    h.ctx = NULL;
    return h;
}
