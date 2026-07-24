#include <stdio.h>
#include <string.h>

#include "ash/core/manifest.h"
#include "ash_test.h"

static size_t slurp(const char *name, char *buf, size_t cap)
{
    char path[512];
    snprintf(path, sizeof path, "%s/ext/%s", ASH_FIXTURES_DIR, name);
    FILE *f = fopen(path, "rb");
    if (!f)
        return 0;
    size_t n = fread(buf, 1, cap, f);
    fclose(f);
    return n;
}

int main(void)
{
    ash_manifest m;

    {
        char buf[1024];
        memset(buf, 0, sizeof buf);
        size_t n = slurp("bad_version.manifest", buf, sizeof buf - 1);
        ASH_CHECK(n > 0);
        ASH_CHECK(ash_manifest_parse(buf, n, &m) == ASH_OK);
        ASH_CHECK(ash_manifest_check(&m) == ASH_ERR_UNSUPPORTED);
        ASH_CHECK_STREQ(ash_errbuf, "extension 'frobnicator': wants api 999, have 1");
    }

    {
        char buf[1024];
        memset(buf, 0, sizeof buf);
        size_t n = slurp("missing_api.manifest", buf, sizeof buf - 1);
        ASH_CHECK(n > 0);
        ASH_CHECK(ash_manifest_parse(buf, n, &m) == ASH_OK);
        ASH_CHECK(ash_manifest_check(&m) == ASH_ERR_UNSUPPORTED);
        ASH_CHECK_STREQ(ash_errbuf, "extension 'oldext': missing api_version");
    }

    {
        char buf[1024];
        memset(buf, 0, sizeof buf);
        size_t n = slurp("overflow.manifest", buf, sizeof buf - 1);
        ASH_CHECK(n > 0);
        ASH_CHECK(ash_manifest_parse(buf, n, &m) == ASH_ERR_PARSE);
        ASH_CHECK_STREQ(ash_errbuf, "manifest: api_version out of range");
    }

    {
        char buf[1024];
        memset(buf, 0, sizeof buf);
        size_t n = slurp("valid.manifest", buf, sizeof buf - 1);
        ASH_CHECK(n > 0);
        ASH_CHECK(ash_manifest_parse(buf, n, &m) == ASH_OK);
        ASH_CHECK(ash_manifest_check(&m) == ASH_OK);
        ASH_CHECK(m.api_version == ASH_EXT_API_VERSION);
    }

    return ash_test_done();
}
