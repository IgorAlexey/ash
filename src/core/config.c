#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/poison.h"
#include "ash/core/config.h"

static const char DEFAULT_PROVIDER[] = "anthropic";
static const char DEFAULT_MODEL[]    = "claude-3-5-sonnet-20241022";
static const char DEFAULT_URL[]      = "https://api.anthropic.com/v1/messages";

static char *arena_cstr(ash_arena *a, const char *p, size_t n)
{
    char *s = ash_array(a, char, n + 1);
    if (n)
        memcpy(s, p, n);
    s[n] = '\0';
    return s;
}

static char *arena_join(ash_arena *a, const char *dir, const char *rest)
{
    size_t dn = strlen(dir);
    size_t rn = strlen(rest);
    char *s = ash_array(a, char, dn + rn + 1);
    memcpy(s, dir, dn);
    memcpy(s + dn, rest, rn);
    s[dn + rn] = '\0';
    return s;
}

const char *ash_config_layer_str(ash_config_layer layer)
{
    switch (layer) {
    case ASH_CFG_DEFAULT: return "default";
    case ASH_CFG_GLOBAL:  return "global";
    case ASH_CFG_PROJECT: return "project";
    case ASH_CFG_ENV:     return "env";
    }
    return "unknown";
}

const char *ash_thinking_str(ash_thinking level)
{
    switch (level) {
    case ASH_THINK_OFF:     return "off";
    case ASH_THINK_MINIMAL: return "minimal";
    case ASH_THINK_LOW:     return "low";
    case ASH_THINK_MEDIUM:  return "medium";
    case ASH_THINK_HIGH:    return "high";
    case ASH_THINK_XHIGH:   return "xhigh";
    case ASH_THINK_MAX:     return "max";
    }
    return "off";
}

ash_status ash_thinking_parse(const char *s, ash_thinking *out)
{
    static const struct { const char *name; ash_thinking level; } tab[] = {
        { "off",     ASH_THINK_OFF },
        { "minimal", ASH_THINK_MINIMAL },
        { "low",     ASH_THINK_LOW },
        { "medium",  ASH_THINK_MEDIUM },
        { "high",    ASH_THINK_HIGH },
        { "xhigh",   ASH_THINK_XHIGH },
        { "max",     ASH_THINK_MAX },
    };
    if (s == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_thinking_parse: bad arguments");
    for (size_t i = 0; i < sizeof tab / sizeof tab[0]; i++)
        if (strcmp(s, tab[i].name) == 0) {
            *out = tab[i].level;
            return ASH_OK;
        }
    return ash_fail(ASH_ERR_PARSE, "config: unknown thinking level '%s'", s);
}

static ash_status read_file(ash_arena *a, const char *path, ash_slice *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT)
            return ASH_ERR_NOTFOUND;
        return ash_fail(ASH_ERR_IO, "config: open %s: %s", path, strerror(errno));
    }
    ash_buf b;
    ash_buf_init(&b, a);
    for (;;) {
        ash_buf_reserve(&b, 4096);
        ssize_t n = read(fd, b.data + b.len, b.cap - b.len);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            int e = errno;
            close(fd);
            return ash_fail(ASH_ERR_IO, "config: read %s: %s", path, strerror(e));
        }
        if (n == 0)
            break;
        b.len += (size_t)n;
        if (b.len > ASH_JSON_MAX_INPUT) {
            close(fd);
            return ash_fail(ASH_ERR_NOSPACE, "config: %s exceeds %u bytes",
                            path, (unsigned)ASH_JSON_MAX_INPUT);
        }
    }
    close(fd);
    *out = ash_slice_make((const char *)b.data, b.len);
    return ASH_OK;
}

static void set_field(ash_arena *a, ash_cfg_field *f, ash_slice v,
                      ash_config_layer layer)
{
    f->value = arena_cstr(a, v.p, v.len);
    f->layer = layer;
}

static void apply_str_key(ash_config *c, ash_cfg_field *f, const ash_json *root,
                          const char *key, ash_config_layer layer)
{
    const ash_json *j = ash_json_get(root, key);
    ash_slice v;
    if (j == NULL || ash_json_str(j, &v) != ASH_OK)
        return;
    set_field(c->arena, f, v, layer);
}

static ash_cfg_provider *find_provider(ash_config *c, const char *name,
                                       size_t nlen)
{
    for (size_t i = 0; i < c->provider_count; i++)
        if (strlen(c->providers[i].name) == nlen &&
            memcmp(c->providers[i].name, name, nlen) == 0)
            return &c->providers[i];
    return NULL;
}

static ash_cfg_provider *push_provider(ash_config *c, const char *name,
                                       size_t nlen)
{
    ash_cfg_provider *np = ash_array(c->arena, ash_cfg_provider,
                                     c->provider_count + 1);
    if (c->provider_count)
        memcpy(np, c->providers, c->provider_count * sizeof *np);
    ash_cfg_provider *p = &np[c->provider_count];
    memset(p, 0, sizeof *p);
    p->name = arena_cstr(c->arena, name, nlen);
    c->providers = np;
    c->provider_count++;
    return p;
}

static void apply_providers(ash_config *c, const ash_json *root,
                            ash_config_layer layer)
{
    const ash_json *provs = ash_json_get(root, "providers");
    if (provs == NULL || provs->type != ASH_JSON_OBJECT)
        return;
    for (size_t i = 0; i < provs->u.obj.n; i++) {
        const ash_json_member *m = &provs->u.obj.v[i];
        if (m->val.type != ASH_JSON_OBJECT)
            continue;
        ash_cfg_provider *p = find_provider(c, m->key, m->klen);
        if (p == NULL)
            p = push_provider(c, m->key, m->klen);
        apply_str_key(c, &p->api_key, &m->val, "api_key", layer);
        apply_str_key(c, &p->base_url, &m->val, "base_url", layer);
    }
}

static void apply_root(ash_config *c, const ash_json *root,
                       ash_config_layer layer)
{
    apply_str_key(c, &c->provider, root, "provider", layer);
    apply_str_key(c, &c->model, root, "model", layer);
    apply_str_key(c, &c->theme, root, "theme", layer);
    apply_str_key(c, &c->system, root, "system", layer);

    const ash_json *t = ash_json_get(root, "thinking_level");
    ash_slice tv;
    ash_thinking level;
    if (t != NULL && ash_json_str(t, &tv) == ASH_OK) {
        char *ts = arena_cstr(c->arena, tv.p, tv.len);
        if (ash_thinking_parse(ts, &level) == ASH_OK) {
            c->thinking = level;
            c->thinking_layer = layer;
        }
    }
    apply_providers(c, root, layer);
}

static void apply_file(ash_config *c, const char *path, ash_config_layer layer)
{
    if (path == NULL)
        return;
    ash_slice text;
    ash_status st = read_file(c->arena, path, &text);
    if (st == ASH_ERR_NOTFOUND)
        return;
    if (st != ASH_OK) {
        c->warnings++;
        return;
    }
    ash_json root;
    if (ash_json_parse(c->arena, text.p, text.len, &root) != ASH_OK ||
        root.type != ASH_JSON_OBJECT) {
        c->warnings++;
        return;
    }
    apply_root(c, &root, layer);
}

static void apply_env_str(ash_config *c, ash_cfg_field *f, const char *name)
{
    const char *v = getenv(name);
    if (v == NULL || v[0] == '\0')
        return;
    set_field(c->arena, f, ash_slice_from_cstr(v), ASH_CFG_ENV);
}

static void provider_env_name(const char *name, char *buf, size_t cap)
{
    size_t j = 0;
    for (size_t i = 0; name[i] != '\0' && j + 1 < cap; i++) {
        unsigned char ch = (unsigned char)name[i];
        if (ch >= 'a' && ch <= 'z')
            ch = (unsigned char)(ch - 'a' + 'A');
        else if (!((ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9')))
            ch = '_';
        buf[j++] = (char)ch;
    }
    const char suffix[] = "_API_KEY";
    for (size_t i = 0; suffix[i] != '\0' && j + 1 < cap; i++)
        buf[j++] = suffix[i];
    buf[j] = '\0';
}

static void resolve_provider(ash_config *c)
{
    const char *name = c->provider.value;
    ash_cfg_provider *p = find_provider(c, name, strlen(name));
    if (p != NULL) {
        if (p->api_key.value != NULL)
            c->api_key = p->api_key;
        if (p->base_url.value != NULL)
            c->base_url = p->base_url;
    }

    const char *url = getenv("ASH_URL");
    if (url != NULL && url[0] != '\0')
        set_field(c->arena, &c->base_url, ash_slice_from_cstr(url), ASH_CFG_ENV);

    char envname[64];
    provider_env_name(name, envname, sizeof envname);
    const char *key = getenv(envname);
    if (key != NULL && key[0] != '\0')
        set_field(c->arena, &c->api_key, ash_slice_from_cstr(key), ASH_CFG_ENV);
}

ash_status ash_config_load(ash_arena *a, ash_config *out)
{
    if (a == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_config_load: bad arguments");

    memset(out, 0, sizeof *out);
    out->arena = a;

    const char *home = getenv("HOME");
    if (home != NULL && home[0] != '\0')
        out->global_path = arena_join(a, home, "/.ash/settings.json");
    out->project_path = ".ash/settings.json";

    set_field(a, &out->provider, ash_slice_from_cstr(DEFAULT_PROVIDER), ASH_CFG_DEFAULT);
    set_field(a, &out->model, ash_slice_from_cstr(DEFAULT_MODEL), ASH_CFG_DEFAULT);
    set_field(a, &out->base_url, ash_slice_from_cstr(DEFAULT_URL), ASH_CFG_DEFAULT);
    out->thinking = ASH_THINK_OFF;
    out->thinking_layer = ASH_CFG_DEFAULT;

    apply_file(out, out->global_path, ASH_CFG_GLOBAL);
    apply_file(out, out->project_path, ASH_CFG_PROJECT);

    apply_env_str(out, &out->provider, "ASH_PROVIDER");
    apply_env_str(out, &out->model, "ASH_MODEL");
    apply_env_str(out, &out->theme, "ASH_THEME");
    apply_env_str(out, &out->system, "ASH_SYSTEM");
    const char *tl = getenv("ASH_THINKING_LEVEL");
    ash_thinking level;
    if (tl != NULL && tl[0] != '\0' && ash_thinking_parse(tl, &level) == ASH_OK) {
        out->thinking = level;
        out->thinking_layer = ASH_CFG_ENV;
    }

    resolve_provider(out);
    return ASH_OK;
}

static void emit_string(ash_buf *b, const char *p, size_t n)
{
    ash_buf_append_byte(b, '"');
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)p[i];
        switch (c) {
        case '"':  ash_buf_append_cstr(b, "\\\""); break;
        case '\\': ash_buf_append_cstr(b, "\\\\"); break;
        case '\b': ash_buf_append_cstr(b, "\\b"); break;
        case '\f': ash_buf_append_cstr(b, "\\f"); break;
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

static void emit_value(ash_buf *b, const ash_json *v)
{
    switch (v->type) {
    case ASH_JSON_NULL:
        ash_buf_append_cstr(b, "null");
        return;
    case ASH_JSON_BOOL:
        ash_buf_append_cstr(b, v->u.boolean ? "true" : "false");
        return;
    case ASH_JSON_NUMBER:
        ash_buf_append(b, v->u.num.p, v->u.num.n);
        return;
    case ASH_JSON_STRING:
        emit_string(b, v->u.str.p, v->u.str.n);
        return;
    case ASH_JSON_ARRAY:
        ash_buf_append_byte(b, '[');
        for (size_t i = 0; i < v->u.arr.n; i++) {
            if (i)
                ash_buf_append_byte(b, ',');
            emit_value(b, &v->u.arr.v[i]);
        }
        ash_buf_append_byte(b, ']');
        return;
    case ASH_JSON_OBJECT:
        ash_buf_append_byte(b, '{');
        for (size_t i = 0; i < v->u.obj.n; i++) {
            if (i)
                ash_buf_append_byte(b, ',');
            emit_string(b, v->u.obj.v[i].key, v->u.obj.v[i].klen);
            ash_buf_append_byte(b, ':');
            emit_value(b, &v->u.obj.v[i].val);
        }
        ash_buf_append_byte(b, '}');
        return;
    }
}

static ash_status write_all(int fd, const unsigned char *p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return ash_fail(ASH_ERR_IO, "config: write: %s", strerror(errno));
        }
        off += (size_t)w;
    }
    return ASH_OK;
}

static void mkdir_parent(ash_arena *a, const char *path)
{
    const char *slash = strrchr(path, '/');
    if (slash == NULL || slash == path)
        return;
    char *dir = arena_cstr(a, path, (size_t)(slash - path));
    (void)mkdir(dir, 0700);
}

static ash_status atomic_write(ash_arena *a, const char *path,
                               const unsigned char *data, size_t len)
{
    mkdir_parent(a, path);
    char *tmp = arena_join(a, path, ".tmp");
    int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return ash_fail(ASH_ERR_IO, "config: open %s: %s", tmp, strerror(errno));

    ash_status st = write_all(fd, data, len);
    if (st == ASH_OK && fchmod(fd, 0600) < 0)
        st = ash_fail(ASH_ERR_IO, "config: fchmod %s: %s", tmp, strerror(errno));
    if (st == ASH_OK && fsync(fd) < 0)
        st = ash_fail(ASH_ERR_IO, "config: fsync %s: %s", tmp, strerror(errno));
    close(fd);
    if (st != ASH_OK) {
        unlink(tmp);
        return st;
    }
    if (rename(tmp, path) < 0) {
        int e = errno;
        unlink(tmp);
        return ash_fail(ASH_ERR_IO, "config: rename %s: %s", path, strerror(e));
    }
    return ASH_OK;
}

ash_status ash_config_set(const ash_config *cfg, ash_config_layer layer,
                          const char *key, const char *value)
{
    if (cfg == NULL || key == NULL || value == NULL || key[0] == '\0')
        return ash_fail(ASH_ERR_RANGE, "ash_config_set: bad arguments");
    if (layer != ASH_CFG_GLOBAL && layer != ASH_CFG_PROJECT)
        return ash_fail(ASH_ERR_RANGE, "ash_config_set: layer is not writable");

    const char *path = layer == ASH_CFG_PROJECT ? cfg->project_path
                                                : cfg->global_path;
    if (path == NULL)
        return ash_fail(ASH_ERR_STATE, "ash_config_set: no path for %s layer",
                        ash_config_layer_str(layer));

    ash_arena *a = cfg->arena;
    ash_json root;
    ash_slice text;
    ash_status st = read_file(a, path, &text);
    if (st == ASH_ERR_NOTFOUND) {
        root.type = ASH_JSON_OBJECT;
        root.u.obj.v = NULL;
        root.u.obj.n = 0;
    } else if (st == ASH_OK) {
        if (ash_json_parse(a, text.p, text.len, &root) != ASH_OK)
            return ash_fail(ASH_ERR_PARSE,
                            "config: %s is malformed; refusing to overwrite", path);
        if (root.type != ASH_JSON_OBJECT)
            return ash_fail(ASH_ERR_STATE, "config: %s root is not an object", path);
    } else {
        return st;
    }

    size_t klen = strlen(key);
    ash_buf out;
    ash_buf_init(&out, a);
    ash_buf_append_byte(&out, '{');
    int first = 1;
    for (size_t i = 0; i < root.u.obj.n; i++) {
        const ash_json_member *m = &root.u.obj.v[i];
        if (m->klen == klen && memcmp(m->key, key, klen) == 0)
            continue;
        if (!first)
            ash_buf_append_byte(&out, ',');
        first = 0;
        emit_string(&out, m->key, m->klen);
        ash_buf_append_byte(&out, ':');
        emit_value(&out, &m->val);
    }
    if (!first)
        ash_buf_append_byte(&out, ',');
    emit_string(&out, key, klen);
    ash_buf_append_byte(&out, ':');
    emit_string(&out, value, strlen(value));
    ash_buf_append_byte(&out, '}');
    ash_buf_append_byte(&out, '\n');

    return atomic_write(a, path, out.data, out.len);
}
