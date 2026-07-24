#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ash/base/arena.h"
#include "ash/base/json.h"
#include "ash/core/config.h"
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

static char *read_file_dup(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        *out_len = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    fclose(f);
    buf[got] = '\0';
    *out_len = got;
    return buf;
}

static void clear_env(void)
{
    const char *names[] = {
        "ASH_API_KEY", "ASH_URL", "ASH_MODEL", "ASH_PROVIDER", "ASH_THEME",
        "ASH_SYSTEM", "ASH_THINKING_LEVEL", "ANTHROPIC_API_KEY",
        "DEEPSEEK_API_KEY", "OPENAI_API_KEY",
    };
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
        unsetenv(names[i]);
}

static int field_is(ash_cfg_field f, const char *want, ash_config_layer layer)
{
    return f.value != NULL && strcmp(f.value, want) == 0 && f.layer == layer;
}

int main(void)
{
    ASH_CHECK(ash_arena_create(&g_a, "config", 1u << 20) == ASH_OK);

    char home_dir[] = "/tmp/ash-cfg-home-XXXXXX";
    char proj_dir[] = "/tmp/ash-cfg-proj-XXXXXX";
    ASH_CHECK(mkdtemp(home_dir) != NULL);
    ASH_CHECK(mkdtemp(proj_dir) != NULL);
    ASH_CHECK(setenv("HOME", home_dir, 1) == 0);
    ASH_CHECK(chdir(proj_dir) == 0);
    clear_env();

    char gpath[256], ppath[256];
    snprintf(gpath, sizeof gpath, "%s/.ash/settings.json", home_dir);
    snprintf(ppath, sizeof ppath, "%s/.ash/settings.json", proj_dir);
    char gdir[256], pdir[256];
    snprintf(gdir, sizeof gdir, "%s/.ash", home_dir);
    snprintf(pdir, sizeof pdir, "%s/.ash", proj_dir);
    mkdir(gdir, 0700);
    mkdir(pdir, 0700);

    ash_config c;

    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(field_is(c.provider, "anthropic", ASH_CFG_DEFAULT));
    ASH_CHECK(c.model.value != NULL && c.model.layer == ASH_CFG_DEFAULT);
    ASH_CHECK(c.api_key.value == NULL);
    ASH_CHECK(c.base_url.value != NULL && c.base_url.layer == ASH_CFG_DEFAULT);
    ASH_CHECK(c.thinking == ASH_THINK_OFF && c.warnings == 0);

    write_file(gpath,
        "{\"provider\":\"anthropic\",\"model\":\"g-model\","
        "\"theme\":\"dark\",\"thinking_level\":\"high\","
        "\"providers\":{\"anthropic\":{\"api_key\":\"g-key\","
        "\"base_url\":\"https://g/\"}}}");
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(field_is(c.model, "g-model", ASH_CFG_GLOBAL));
    ASH_CHECK(field_is(c.theme, "dark", ASH_CFG_GLOBAL));
    ASH_CHECK(c.thinking == ASH_THINK_HIGH && c.thinking_layer == ASH_CFG_GLOBAL);
    ASH_CHECK(field_is(c.api_key, "g-key", ASH_CFG_GLOBAL));
    ASH_CHECK(field_is(c.base_url, "https://g/", ASH_CFG_GLOBAL));

    write_file(ppath,
        "{\"model\":\"p-model\","
        "\"providers\":{\"anthropic\":{\"base_url\":\"https://p/\"}}}");
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(field_is(c.model, "p-model", ASH_CFG_PROJECT));
    ASH_CHECK(field_is(c.theme, "dark", ASH_CFG_GLOBAL));
    ASH_CHECK(field_is(c.base_url, "https://p/", ASH_CFG_PROJECT));
    ASH_CHECK(field_is(c.api_key, "g-key", ASH_CFG_GLOBAL));

    ASH_CHECK(setenv("ASH_MODEL", "e-model", 1) == 0);
    ASH_CHECK(setenv("ANTHROPIC_API_KEY", "e-key", 1) == 0);
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(field_is(c.model, "e-model", ASH_CFG_ENV));
    ASH_CHECK(field_is(c.api_key, "e-key", ASH_CFG_ENV));

    ASH_CHECK(setenv("ASH_API_KEY", "generic-key", 1) == 0);
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(field_is(c.api_key, "e-key", ASH_CFG_ENV));
    clear_env();

    write_file(gpath,
        "{\"model\":\"old\",\"future_flag\":{\"nested\":[1,2,3]},"
        "\"count\":42}");
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(ash_config_set(&c, ASH_CFG_GLOBAL, "model", "new-model") == ASH_OK);

    size_t rn;
    char *raw = read_file_dup(gpath, &rn);
    ASH_CHECK(raw != NULL);
    ash_json root;
    ASH_CHECK(ash_json_parse(&g_a, raw, rn, &root) == ASH_OK);
    const ash_json *mv = ash_json_get(&root, "model");
    ash_slice sl;
    ASH_CHECK(mv != NULL && ash_json_str(mv, &sl) == ASH_OK &&
              sl.len == 9 && memcmp(sl.p, "new-model", 9) == 0);
    const ash_json *ff = ash_json_get(&root, "future_flag");
    ASH_CHECK(ff != NULL && ff->type == ASH_JSON_OBJECT);
    const ash_json *cnt = ash_json_get(&root, "count");
    int64_t cv = 0;
    ASH_CHECK(cnt != NULL && ash_json_int64(cnt, &cv) == ASH_OK && cv == 42);
    free(raw);

    struct stat stt;
    ASH_CHECK(stat(gpath, &stt) == 0 && (stt.st_mode & 0777) == 0600);

    ASH_CHECK(ash_config_set(&c, ASH_CFG_ENV, "model", "x") == ASH_ERR_RANGE);
    ASH_CHECK(ash_config_set(&c, ASH_CFG_GLOBAL, "", "x") == ASH_ERR_RANGE);

    write_file(gpath, "{ this is not json ");
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK && c.warnings >= 1);
    ASH_CHECK(field_is(c.provider, "anthropic", ASH_CFG_DEFAULT));
    ASH_CHECK(ash_config_set(&c, ASH_CFG_GLOBAL, "model", "y") == ASH_ERR_PARSE);

    unlink(ppath);
    write_file(gpath, "{}");
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(ash_config_set(&c, ASH_CFG_PROJECT, "theme", "light") == ASH_OK);
    ASH_CHECK(ash_config_load(&g_a, &c) == ASH_OK);
    ASH_CHECK(field_is(c.theme, "light", ASH_CFG_PROJECT));

    ash_thinking lvl;
    ASH_CHECK(ash_thinking_parse("max", &lvl) == ASH_OK && lvl == ASH_THINK_MAX);
    ASH_CHECK(ash_thinking_parse("bogus", &lvl) == ASH_ERR_PARSE);
    ASH_CHECK(strcmp(ash_thinking_str(ASH_THINK_XHIGH), "xhigh") == 0);
    ASH_CHECK(strcmp(ash_config_layer_str(ASH_CFG_PROJECT), "project") == 0);

    ash_arena_destroy(&g_a);
    return ash_test_done();
}
