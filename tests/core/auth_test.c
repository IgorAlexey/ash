#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ash/ai/provider.h"
#include "ash/base/arena.h"
#include "ash/core/auth.h"
#include "ash_test.h"

static ash_arena g_a;

static void write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    ASH_CHECK(f != NULL);
    if (f == NULL)
        return;
    fputs(text, f);
    fclose(f);
}

static int mode_is(const char *path, mode_t want)
{
    struct stat st;
    return stat(path, &st) == 0 && (st.st_mode & 0777) == want;
}

static int exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int main(void)
{
    ASH_CHECK(ash_arena_create(&g_a, "auth", 1u << 20) == ASH_OK);

    char home[] = "/tmp/ash-auth-home-XXXXXX";
    ASH_CHECK(mkdtemp(home) != NULL);
    ASH_CHECK(setenv("HOME", home, 1) == 0);
    unsetenv("ANTHROPIC_API_KEY");

    char path[256], dir[256], tmp[256];
    snprintf(path, sizeof path, "%s/.ash/auth.json", home);
    snprintf(dir, sizeof dir, "%s/.ash", home);
    snprintf(tmp, sizeof tmp, "%s/.ash/auth.json.tmp", home);

    ash_auth s;

    ASH_CHECK(ash_auth_load(&g_a, NULL, &s) == ASH_OK);
    ASH_CHECK(s.count == 0 && s.warnings == 0);
    ASH_CHECK(!exists(path));

    ASH_CHECK(ash_auth_set_api_key(&s, "anthropic", "sk-one") == ASH_OK);
    ASH_CHECK(mode_is(path, 0600));
    ASH_CHECK(mode_is(dir, 0700));
    ASH_CHECK(!exists(tmp));
    ASH_CHECK_STREQ(ash_auth_api_key(&s, "anthropic"), "sk-one");

    ash_auth r;
    ASH_CHECK(ash_auth_load(&g_a, NULL, &r) == ASH_OK);
    ASH_CHECK(r.count == 1 && r.warnings == 0);
    ASH_CHECK_STREQ(ash_auth_api_key(&r, "anthropic"), "sk-one");

    ASH_CHECK(chmod(path, 0644) == 0);
    ASH_CHECK(ash_auth_load(&g_a, NULL, &r) == ASH_OK);
    ASH_CHECK(r.warnings >= 1);
    ASH_CHECK(mode_is(path, 0600));
    ASH_CHECK_STREQ(ash_auth_api_key(&r, "anthropic"), "sk-one");

    ASH_CHECK(ash_auth_set_api_key(&s, "anthropic", "sk-two") == ASH_OK);
    ASH_CHECK(ash_auth_load(&g_a, NULL, &r) == ASH_OK);
    ASH_CHECK(r.count == 1);
    ASH_CHECK_STREQ(ash_auth_api_key(&r, "anthropic"), "sk-two");

    ash_oauth tok = { .access = "at", .refresh = "rt", .expires = 4242 };
    ASH_CHECK(ash_auth_set_oauth(&s, "openai", &tok) == ASH_OK);
    ASH_CHECK(ash_auth_load(&g_a, NULL, &r) == ASH_OK);
    ASH_CHECK(r.count == 2);
    const ash_credential *oc = ash_auth_get(&r, "openai");
    ASH_CHECK(oc != NULL && oc->kind == ASH_CRED_OAUTH);
    ASH_CHECK_STREQ(oc->oauth.access, "at");
    ASH_CHECK_STREQ(oc->oauth.refresh, "rt");
    ASH_CHECK(oc->oauth.expires == 4242);
    ASH_CHECK(ash_auth_api_key(&r, "openai") == NULL);

    ASH_CHECK(ash_auth_delete(&s, "openai") == ASH_OK);
    ASH_CHECK(ash_auth_load(&g_a, NULL, &r) == ASH_OK);
    ASH_CHECK(r.count == 1);
    ASH_CHECK(ash_auth_get(&r, "openai") == NULL);

    write_file(path, "{ this is not json ");
    ASH_CHECK(ash_auth_load(&g_a, NULL, &r) == ASH_OK);
    ASH_CHECK(r.warnings >= 1 && r.count == 0);
    ASH_CHECK(ash_auth_set_api_key(&r, "anthropic", "sk-recovered") == ASH_OK);
    ASH_CHECK(ash_auth_load(&g_a, NULL, &r) == ASH_OK);
    ASH_CHECK(r.count == 1);
    ASH_CHECK_STREQ(ash_auth_api_key(&r, "anthropic"), "sk-recovered");

    const ash_provider_desc *desc = ash_provider_find("anthropic");
    ASH_CHECK(desc != NULL);
    ash_auth_bind(&r);
    unsetenv("ANTHROPIC_API_KEY");
    ASH_CHECK_STREQ(ash_provider_api_key(desc), "sk-recovered");
    ASH_CHECK(setenv("ANTHROPIC_API_KEY", "env-wins", 1) == 0);
    ASH_CHECK_STREQ(ash_provider_api_key(desc), "env-wins");
    unsetenv("ANTHROPIC_API_KEY");

    ASH_CHECK(ash_auth_set_api_key(&s, "", "k") == ASH_ERR_RANGE);
    ASH_CHECK(ash_auth_set_api_key(&s, "x", NULL) == ASH_ERR_RANGE);

    ash_arena_destroy(&g_a);
    return ash_test_done();
}
