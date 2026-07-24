#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "ash/base/arena.h"
#include "ash/base/base64.h"
#include "ash/base/sha256.h"
#include "ash/base/slice.h"
#include "ash/core/auth.h"
#include "ash/core/oauth.h"
#include "ash_test.h"

static ash_arena g_a;

static int is_unreserved(char c)
{
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '-' || c == '.' || c == '_' ||
           c == '~';
}

static int contains(const char *hay, const char *needle)
{
    return strstr(hay, needle) != NULL;
}

struct mock {
    const char *resp;
    long        status;
    char        body[2048];
    char        url[256];
};

static ash_status mock_post(void *ctx, ash_arena *a, const char *url,
                            const char *body, size_t body_len, long *status,
                            ash_slice *resp)
{
    (void)a;
    struct mock *m = ctx;
    size_t n = body_len < sizeof m->body - 1 ? body_len : sizeof m->body - 1;
    memcpy(m->body, body, n);
    m->body[n] = '\0';
    snprintf(m->url, sizeof m->url, "%s", url);
    *status = m->status;
    *resp = ash_slice_make(m->resp, strlen(m->resp));
    return ASH_OK;
}

static void test_pkce(void)
{
    ash_oauth_pkce p;
    ASH_CHECK(ash_oauth_pkce_begin(&g_a, &p) == ASH_OK);

    ASH_CHECK(strlen(p.verifier) == 43);
    for (size_t i = 0; i < strlen(p.verifier); i++)
        ASH_CHECK(is_unreserved(p.verifier[i]));
    ASH_CHECK(strlen(p.state) == 32);
    ASH_CHECK(strlen(p.challenge) == 43);

    uint8_t dg[ASH_SHA256_DIGEST];
    ash_sha256(p.verifier, strlen(p.verifier), dg);
    char want[44];
    ASH_CHECK(ash_base64url_encode(dg, sizeof dg, want, sizeof want) == 43);
    ASH_CHECK_STREQ(p.challenge, want);

    ASH_CHECK(contains(p.url, ASH_OAUTH_AUTHORIZE_URL));
    ASH_CHECK(contains(p.url, "client_id=" ASH_OAUTH_CLIENT_ID));
    ASH_CHECK(contains(p.url, "code=true"));
    ASH_CHECK(contains(p.url, "response_type=code"));
    ASH_CHECK(contains(p.url, "code_challenge_method=S256"));
    ASH_CHECK(contains(p.url, "code_challenge="));
    ASH_CHECK(contains(p.url, "scope=org%3Acreate_api_key"));
    ASH_CHECK(contains(p.url,
                       "redirect_uri=https%3A%2F%2Fconsole.anthropic.com"));
    ASH_CHECK(contains(p.url, p.state));
    ASH_CHECK(contains(p.url, p.challenge));
}

static void test_rfc7636_vector(void)
{
    const char *verifier = "dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk";
    uint8_t dg[ASH_SHA256_DIGEST];
    ash_sha256(verifier, strlen(verifier), dg);
    char ch[44];
    ASH_CHECK(ash_base64url_encode(dg, sizeof dg, ch, sizeof ch) == 43);
    ASH_CHECK_STREQ(ch, "E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM");
}

static void test_needs_refresh(void)
{
    int64_t exp = 100000;
    ASH_CHECK(ash_oauth_needs_refresh(exp, exp - 61) == 0);
    ASH_CHECK(ash_oauth_needs_refresh(exp, exp - 60) == 1);
    ASH_CHECK(ash_oauth_needs_refresh(exp, exp - 59) == 1);
    ASH_CHECK(ash_oauth_needs_refresh(exp, exp) == 1);
    ASH_CHECK(ash_oauth_needs_refresh(exp, exp - 600) == 0);
}

static void test_state_ok(void)
{
    ASH_CHECK(ash_oauth_state_ok("abc", "abc") == 1);
    ASH_CHECK(ash_oauth_state_ok("abc", "abd") == 0);
    ASH_CHECK(ash_oauth_state_ok("", "abc") == 0);
    ASH_CHECK(ash_oauth_state_ok(NULL, "abc") == 0);
    ASH_CHECK(ash_oauth_state_ok("abc", NULL) == 0);
}

static void test_exchange(void)
{
    struct mock m = {
        .resp = "{\"access_token\":\"acc-123\",\"refresh_token\":\"ref-456\","
                "\"expires_in\":3600,"
                "\"account\":{\"email_address\":\"user@example.com\"}}",
        .status = 200,
    };
    ash_oauth_http http = { .post_json = mock_post, .ctx = &m };
    ash_oauth_token tok;
    int64_t before = (int64_t)time(NULL);
    ASH_CHECK(ash_oauth_exchange(&http, &g_a, "ver", "the-code", "the-state",
                                 &tok) == ASH_OK);
    int64_t after = (int64_t)time(NULL);

    ASH_CHECK_STREQ(m.url, ASH_OAUTH_TOKEN_URL);
    ASH_CHECK(contains(m.body, "\"grant_type\":\"authorization_code\""));
    ASH_CHECK(contains(m.body, "\"code\":\"the-code\""));
    ASH_CHECK(contains(m.body, "\"state\":\"the-state\""));
    ASH_CHECK(contains(m.body, "\"code_verifier\":\"ver\""));
    ASH_CHECK(contains(m.body, "\"client_id\":\"" ASH_OAUTH_CLIENT_ID "\""));
    ASH_CHECK(contains(m.body, "\"redirect_uri\":\"" ASH_OAUTH_REDIRECT_URI "\""));

    ASH_CHECK_STREQ(tok.access, "acc-123");
    ASH_CHECK_STREQ(tok.refresh, "ref-456");
    ASH_CHECK(tok.account != NULL);
    ASH_CHECK_STREQ(tok.account, "user@example.com");
    ASH_CHECK(tok.expires >= before + 3600 && tok.expires <= after + 3600);
}

static void test_refresh(void)
{
    struct mock m = {
        .resp = "{\"access_token\":\"acc-new\",\"refresh_token\":\"ref-new\","
                "\"expires_in\":100}",
        .status = 200,
    };
    ash_oauth_http http = { .post_json = mock_post, .ctx = &m };
    ash_oauth_token tok;
    ASH_CHECK(ash_oauth_refresh(&http, &g_a, "old-refresh", &tok) == ASH_OK);
    ASH_CHECK(contains(m.body, "\"grant_type\":\"refresh_token\""));
    ASH_CHECK(contains(m.body, "\"refresh_token\":\"old-refresh\""));
    ASH_CHECK(contains(m.body, "\"client_id\":\"" ASH_OAUTH_CLIENT_ID "\""));
    ASH_CHECK_STREQ(tok.access, "acc-new");
    ASH_CHECK_STREQ(tok.refresh, "ref-new");
}

static void test_error_paths(void)
{
    struct mock m = { .resp = "{\"error\":\"invalid_grant\"}", .status = 400 };
    ash_oauth_http http = { .post_json = mock_post, .ctx = &m };
    ash_oauth_token tok;
    ASH_CHECK(ash_oauth_exchange(&http, &g_a, "v", "c", "s", &tok) != ASH_OK);

    struct mock m2 = { .resp = "{\"refresh_token\":\"r\"}", .status = 200 };
    ash_oauth_http http2 = { .post_json = mock_post, .ctx = &m2 };
    ASH_CHECK(ash_oauth_refresh(&http2, &g_a, "r", &tok) != ASH_OK);

    ASH_CHECK(ash_oauth_exchange(NULL, &g_a, "v", "c", "s", &tok) ==
              ASH_ERR_RANGE);
}

static void test_persistence_roundtrip(void)
{
    char home[] = "/tmp/ash-oauth-home-XXXXXX";
    ASH_CHECK(mkdtemp(home) != NULL);
    ASH_CHECK(setenv("HOME", home, 1) == 0);
    unsetenv("ANTHROPIC_API_KEY");

    struct mock m = {
        .resp = "{\"access_token\":\"persist-acc\",\"refresh_token\":"
                "\"persist-ref\",\"expires_in\":7200}",
        .status = 200,
    };
    ash_oauth_http http = { .post_json = mock_post, .ctx = &m };
    ash_oauth_token tok;
    ASH_CHECK(ash_oauth_exchange(&http, &g_a, "v", "c", "s", &tok) == ASH_OK);

    ash_auth s;
    ASH_CHECK(ash_auth_load(&g_a, NULL, &s) == ASH_OK);
    ash_oauth stored = { .access = tok.access, .refresh = tok.refresh,
                         .expires = tok.expires };
    ASH_CHECK(ash_auth_set_oauth(&s, "anthropic", &stored) == ASH_OK);

    ash_auth r;
    ASH_CHECK(ash_auth_load(&g_a, NULL, &r) == ASH_OK);
    const ash_credential *c = ash_auth_get(&r, "anthropic");
    ASH_CHECK(c != NULL && c->kind == ASH_CRED_OAUTH);
    ASH_CHECK_STREQ(c->oauth.access, "persist-acc");
    ASH_CHECK_STREQ(c->oauth.refresh, "persist-ref");
    ASH_CHECK(c->oauth.expires == tok.expires);
}

int main(void)
{
    ASH_CHECK(ash_arena_create(&g_a, "oauth", 1u << 20) == ASH_OK);
    test_pkce();
    test_rfc7636_vector();
    test_needs_refresh();
    test_state_ok();
    test_exchange();
    test_refresh();
    test_error_paths();
    test_persistence_roundtrip();
    ash_arena_destroy(&g_a);
    return ash_test_done();
}
