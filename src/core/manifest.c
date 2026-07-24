#include <string.h>

#include "ash/core/manifest.h"
#include "ash/base/poison.h"

static ash_slice trim(const char *s, size_t n)
{
    while (n && (s[0] == ' ' || s[0] == '\t')) {
        s++;
        n--;
    }
    while (n && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r'))
        n--;
    return ash_slice_make(s, n);
}

ash_status ash_manifest_parse(const char *text, size_t len, ash_manifest *out)
{
    if (out == NULL || (text == NULL && len > 0))
        return ash_fail(ASH_ERR_RANGE, "manifest parse: bad arguments");
    memset(out, 0, sizeof *out);

    size_t i = 0;
    while (i < len) {
        size_t start = i;
        while (i < len && text[i] != '\n')
            i++;
        ash_slice line = trim(text + start, i - start);
        if (i < len)
            i++;
        if (line.len == 0 || line.p[0] == '#')
            continue;

        size_t eq = 0;
        while (eq < line.len && line.p[eq] != '=')
            eq++;
        if (eq == line.len)
            return ash_fail(ASH_ERR_PARSE, "manifest: line has no '='");

        ash_slice key = trim(line.p, eq);
        ash_slice val = trim(line.p + eq + 1, line.len - eq - 1);

        if (ash_slice_eq_cstr(key, "name")) {
            out->name = val;
        } else if (ash_slice_eq_cstr(key, "entry")) {
            out->entry = val;
        } else if (ash_slice_eq_cstr(key, "api_version")) {
            uint32_t v = 0;
            if (val.len == 0)
                return ash_fail(ASH_ERR_PARSE, "manifest: empty api_version");
            for (size_t k = 0; k < val.len; k++) {
                if (val.p[k] < '0' || val.p[k] > '9')
                    return ash_fail(ASH_ERR_PARSE, "manifest: api_version not a number");
                uint32_t d = (uint32_t)(val.p[k] - '0');
                if (v > (UINT32_MAX - d) / 10u)
                    return ash_fail(ASH_ERR_PARSE, "manifest: api_version out of range");
                v = v * 10u + d;
            }
            out->api_version = v;
            out->has_api = 1;
        }
    }
    return ASH_OK;
}

ash_status ash_manifest_check(const ash_manifest *m)
{
    if (m == NULL)
        return ash_fail(ASH_ERR_RANGE, "manifest check: null manifest");
    if (m->name.len == 0)
        return ash_fail(ASH_ERR_UNSUPPORTED, "extension manifest: missing name");
    if (!m->has_api)
        return ash_fail(ASH_ERR_UNSUPPORTED, "extension '%.*s': missing api_version",
                        (int)m->name.len, m->name.p);
    if (m->api_version != ASH_EXT_API_VERSION)
        return ash_fail(ASH_ERR_UNSUPPORTED,
                        "extension '%.*s': wants api %u, have %u",
                        (int)m->name.len, m->name.p, m->api_version, ASH_EXT_API_VERSION);
    return ASH_OK;
}
