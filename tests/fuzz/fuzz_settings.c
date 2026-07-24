#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ash/base/arena.h"
#include "ash/core/config.h"
#include "ash/core/settings.h"

static int  g_init;
static char g_home[] = "/tmp/ashfz-set-XXXXXX";

static void write_file(const char *path, const uint8_t *d, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL)
        return;
    if (n)
        fwrite(d, 1, n, f);
    fclose(f);
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (!g_init) {
        if (mkdtemp(g_home) == NULL)
            return 0;
        setenv("HOME", g_home, 1);
        if (chdir(g_home) != 0)
            return 0;
        g_init = 1;
    }

    char dir[256], path[256];
    snprintf(dir, sizeof dir, "%s/.ash", g_home);
    mkdir(dir, 0700);
    snprintf(path, sizeof path, "%s/.ash/settings.json", g_home);
    write_file(path, data, size);

    ash_arena a;
    if (ash_arena_create(&a, "fuzz", 1u << 18) != ASH_OK)
        return 0;

    ash_config c;
    if (ash_config_load(&a, &c) == ASH_OK) {
        size_t n;
        const ash_setting *s = ash_settings_schema(&n);
        for (size_t i = 0; i < n; i++) {
            (void)ash_settings_value(&c, &s[i]);
            (void)ash_settings_layer(&c, &s[i]);
        }
        (void)ash_settings_write(&c, ASH_CFG_GLOBAL, &s[1], "x");
    }

    ash_arena_destroy(&a);
    return 0;
}
