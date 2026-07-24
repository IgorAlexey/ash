#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ash/base/arena.h"
#include "ash/core/config.h"
#include "ash/core/settings.h"
#include "ash_test.h"

static ash_arena g_a;

static const ash_setting *find(const char *key)
{
    size_t n;
    const ash_setting *s = ash_settings_schema(&n);
    for (size_t i = 0; i < n; i++)
        if (strcmp(s[i].key, key) == 0)
            return &s[i];
    return NULL;
}

static void clear_env(void)
{
    const char *names[] = {
        "ASH_URL", "ASH_MODEL", "ASH_PROVIDER", "ASH_THEME", "ASH_SYSTEM",
        "ASH_THINKING_LEVEL", "ANTHROPIC_API_KEY", "DEEPSEEK_API_KEY",
        "OPENAI_API_KEY",
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        unsetenv(names[i]);
}

static int val_is(const ash_config *c, const char *key, const char *want,
                  ash_config_layer layer)
{
    const ash_setting *s = find(key);
    const char *v = ash_settings_value(c, s);
    return s != NULL && v != NULL && strcmp(v, want) == 0 &&
           ash_settings_layer(c, s) == layer;
}

int main(void)
{
    ASH_CHECK(ash_arena_create(&g_a, "settings", 1u << 20) == ASH_OK);

    char home_dir[] = "/tmp/ash-set-home-XXXXXX";
    char proj_dir[] = "/tmp/ash-set-proj-XXXXXX";
    ASH_CHECK(mkdtemp(home_dir) != NULL);
    ASH_CHECK(mkdtemp(proj_dir) != NULL);
    ASH_CHECK(setenv("HOME", home_dir, 1) == 0);
    ASH_CHECK(chdir(proj_dir) == 0);
    clear_env();

    size_t nfields;
    const ash_setting *schema = ash_settings_schema(&nfields);
    ASH_CHECK(schema != NULL && nfields >= 4);
    ASH_CHECK(find("provider") != NULL && find("provider")->kind == ASH_SETTING_ENUM);
    ASH_CHECK(find("model") != NULL && find("model")->kind == ASH_SETTING_TEXT);
    ASH_CHECK(find("thinking_level") != NULL);

    ash_config c;
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(val_is(&c, "provider", "anthropic", ASH_CFG_DEFAULT));
    ASH_CHECK(val_is(&c, "thinking_level", "off", ASH_CFG_DEFAULT));

    const ash_setting *prov = find("provider");
    const ash_setting *think = find("thinking_level");
    const ash_setting *theme = find("theme");

    ASH_CHECK(ash_settings_write(&c, ASH_CFG_GLOBAL, prov, "deepseek") == ASH_OK);
    ASH_CHECK(ash_settings_write(&c, ASH_CFG_GLOBAL, prov, "bogus") == ASH_ERR_PARSE);
    ASH_CHECK(ash_settings_write(&c, ASH_CFG_GLOBAL, think, "high") == ASH_OK);
    ASH_CHECK(ash_settings_write(&c, ASH_CFG_GLOBAL, think, "nope") == ASH_ERR_PARSE);
    ASH_CHECK(ash_settings_write(&c, ASH_CFG_PROJECT, theme, "dark") == ASH_OK);

    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(val_is(&c, "provider", "deepseek", ASH_CFG_GLOBAL));
    ASH_CHECK(val_is(&c, "thinking_level", "high", ASH_CFG_GLOBAL));
    ASH_CHECK(val_is(&c, "theme", "dark", ASH_CFG_PROJECT));

    ASH_CHECK(ash_settings_write(&c, ASH_CFG_ENV, prov, "openai") == ASH_ERR_RANGE);

    ASH_CHECK(strcmp(ash_settings_cycle(think, "off", 1), "minimal") == 0);
    ASH_CHECK(strcmp(ash_settings_cycle(think, "max", 1), "off") == 0);
    ASH_CHECK(strcmp(ash_settings_cycle(think, "off", -1), "max") == 0);
    ASH_CHECK(strcmp(ash_settings_cycle(prov, "anthropic", 1), "deepseek") == 0);
    ASH_CHECK(ash_settings_cycle(theme, "x", 1) == NULL);

    struct stat st;
    char gpath[256];
    snprintf(gpath, sizeof gpath, "%s/.ash/settings.json", home_dir);
    ASH_CHECK(stat(gpath, &st) == 0 && (st.st_mode & 0777) == 0600);

    ash_arena_destroy(&g_a);
    return ash_test_done();
}
