#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ash/ai/provider.h"
#include "ash/base/buf.h"
#include "ash/base/json.h"
#include "ash/base/poison.h"
#include "ash/core/auth.h"

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

static const char *default_path(ash_arena *a)
{
    const char *home = getenv("HOME");
    if (home == NULL || home[0] == '\0')
        return NULL;
    return arena_join(a, home, "/.ash/auth.json");
}

static ash_credential *find(ash_auth *s, const char *provider)
{
    for (size_t i = 0; i < s->count; i++)
        if (strcmp(s->creds[i].provider, provider) == 0)
            return &s->creds[i];
    return NULL;
}

static ash_credential *push(ash_auth *s, const char *provider)
{
    ash_credential *nc = ash_array(s->arena, ash_credential, s->count + 1);
    if (s->count)
        memcpy(nc, s->creds, s->count * sizeof *nc);
    ash_credential *c = &nc[s->count];
    memset(c, 0, sizeof *c);
    c->provider = arena_cstr(s->arena, provider, strlen(provider));
    s->creds = nc;
    s->count++;
    return c;
}

static ash_status read_file(ash_arena *a, const char *path, ash_slice *out)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT)
            return ASH_ERR_NOTFOUND;
        return ash_fail(ASH_ERR_IO, "auth: open %s: %s", path, strerror(errno));
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
            return ash_fail(ASH_ERR_IO, "auth: read %s: %s", path, strerror(e));
        }
        if (n == 0)
            break;
        b.len += (size_t)n;
        if (b.len > ASH_JSON_MAX_INPUT) {
            close(fd);
            return ash_fail(ASH_ERR_NOSPACE, "auth: %s exceeds %u bytes", path,
                            (unsigned)ASH_JSON_MAX_INPUT);
        }
    }
    close(fd);
    *out = ash_slice_make((const char *)b.data, b.len);
    return ASH_OK;
}

static const char *str_field(ash_arena *a, const ash_json *obj, const char *key)
{
    const ash_json *j = ash_json_get(obj, key);
    ash_slice v;
    if (j == NULL || ash_json_str(j, &v) != ASH_OK)
        return NULL;
    return arena_cstr(a, v.p, v.len);
}

static void parse_member(ash_auth *s, const ash_json_member *m)
{
    if (m->val.type != ASH_JSON_OBJECT)
        return;
    const char *type = str_field(s->arena, &m->val, "type");
    if (type == NULL)
        return;
    ash_credential *c = push(s, arena_cstr(s->arena, m->key, m->klen));
    if (strcmp(type, "api_key") == 0) {
        c->kind = ASH_CRED_API_KEY;
        c->api_key = str_field(s->arena, &m->val, "key");
    } else if (strcmp(type, "oauth") == 0) {
        c->kind = ASH_CRED_OAUTH;
        c->oauth.access = str_field(s->arena, &m->val, "access");
        c->oauth.refresh = str_field(s->arena, &m->val, "refresh");
        const ash_json *e = ash_json_get(&m->val, "expires");
        if (e != NULL && ash_json_int64(e, &c->oauth.expires) != ASH_OK)
            c->oauth.expires = 0;
    }
}

ash_status ash_auth_load(ash_arena *a, const char *path, ash_auth *out)
{
    if (a == NULL || out == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_auth_load: bad arguments");

    memset(out, 0, sizeof *out);
    out->arena = a;
    out->path = path != NULL ? path : default_path(a);

    if (out->path == NULL)
        return ASH_OK;

    struct stat st;
    int loose = stat(out->path, &st) == 0 && (st.st_mode & 0077) != 0;
    if (loose) {
        (void)chmod(out->path, 0600);
        out->warnings++;
    }

    ash_slice text;
    ash_status rd = read_file(a, out->path, &text);
    if (rd == ASH_ERR_NOTFOUND)
        return ASH_OK;
    if (rd != ASH_OK) {
        out->warnings++;
        return ASH_OK;
    }

    ash_json root;
    if (ash_json_parse(a, text.p, text.len, &root) != ASH_OK ||
        root.type != ASH_JSON_OBJECT) {
        out->warnings++;
        return ASH_OK;
    }
    for (size_t i = 0; i < root.u.obj.n; i++)
        parse_member(out, &root.u.obj.v[i]);
    return ASH_OK;
}

const ash_credential *ash_auth_get(const ash_auth *s, const char *provider)
{
    if (s == NULL || provider == NULL)
        return NULL;
    for (size_t i = 0; i < s->count; i++)
        if (strcmp(s->creds[i].provider, provider) == 0)
            return &s->creds[i];
    return NULL;
}

const char *ash_auth_api_key(const ash_auth *s, const char *provider)
{
    const ash_credential *c = ash_auth_get(s, provider);
    if (c == NULL || c->kind != ASH_CRED_API_KEY)
        return NULL;
    return c->api_key;
}

static void emit_str(ash_buf *b, const char *p)
{
    ash_buf_append_byte(b, '"');
    for (size_t i = 0; p[i] != '\0'; i++) {
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

static void emit_pair(ash_buf *b, const char *key, const char *val)
{
    emit_str(b, key);
    ash_buf_append_byte(b, ':');
    emit_str(b, val != NULL ? val : "");
}

static void emit_cred(ash_buf *b, const ash_credential *c)
{
    emit_str(b, c->provider);
    ash_buf_append_cstr(b, ":{");
    if (c->kind == ASH_CRED_API_KEY) {
        emit_pair(b, "type", "api_key");
        ash_buf_append_byte(b, ',');
        emit_pair(b, "key", c->api_key);
    } else {
        emit_pair(b, "type", "oauth");
        ash_buf_append_byte(b, ',');
        emit_pair(b, "access", c->oauth.access);
        ash_buf_append_byte(b, ',');
        emit_pair(b, "refresh", c->oauth.refresh);
        ash_buf_append_byte(b, ',');
        emit_str(b, "expires");
        ash_buf_append_byte(b, ':');
        char num[32];
        snprintf(num, sizeof num, "%lld", (long long)c->oauth.expires);
        ash_buf_append_cstr(b, num);
    }
    ash_buf_append_byte(b, '}');
}

static ash_status write_all(int fd, const unsigned char *p, size_t n)
{
    size_t off = 0;
    while (off < n) {
        ssize_t w = write(fd, p + off, n - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return ash_fail(ASH_ERR_IO, "auth: write: %s", strerror(errno));
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
        return ash_fail(ASH_ERR_IO, "auth: open %s: %s", tmp, strerror(errno));

    ash_status st = write_all(fd, data, len);
    if (st == ASH_OK && fchmod(fd, 0600) < 0)
        st = ash_fail(ASH_ERR_IO, "auth: fchmod %s: %s", tmp, strerror(errno));
    if (st == ASH_OK && fsync(fd) < 0)
        st = ash_fail(ASH_ERR_IO, "auth: fsync %s: %s", tmp, strerror(errno));
    close(fd);
    if (st != ASH_OK) {
        unlink(tmp);
        return st;
    }
    if (rename(tmp, path) < 0) {
        int e = errno;
        unlink(tmp);
        return ash_fail(ASH_ERR_IO, "auth: rename %s: %s", path, strerror(e));
    }
    return ASH_OK;
}

static ash_status save(ash_auth *s)
{
    if (s->path == NULL)
        return ash_fail(ASH_ERR_STATE, "auth: no path to write");
    ash_buf b;
    ash_buf_init(&b, s->arena);
    ash_buf_append_byte(&b, '{');
    int first = 1;
    for (size_t i = 0; i < s->count; i++) {
        if (s->creds[i].kind == ASH_CRED_NONE)
            continue;
        if (!first)
            ash_buf_append_byte(&b, ',');
        first = 0;
        emit_cred(&b, &s->creds[i]);
    }
    ash_buf_append_byte(&b, '}');
    ash_buf_append_byte(&b, '\n');
    return atomic_write(s->arena, s->path, b.data, b.len);
}

ash_status ash_auth_set_api_key(ash_auth *s, const char *provider,
                                const char *key)
{
    if (s == NULL || provider == NULL || key == NULL || provider[0] == '\0')
        return ash_fail(ASH_ERR_RANGE, "ash_auth_set_api_key: bad arguments");
    ash_credential *c = find(s, provider);
    if (c == NULL)
        c = push(s, provider);
    c->kind = ASH_CRED_API_KEY;
    c->api_key = arena_cstr(s->arena, key, strlen(key));
    c->oauth = (ash_oauth){ 0 };
    return save(s);
}

ash_status ash_auth_set_oauth(ash_auth *s, const char *provider,
                              const ash_oauth *tok)
{
    if (s == NULL || provider == NULL || tok == NULL || provider[0] == '\0')
        return ash_fail(ASH_ERR_RANGE, "ash_auth_set_oauth: bad arguments");
    ash_credential *c = find(s, provider);
    if (c == NULL)
        c = push(s, provider);
    c->kind = ASH_CRED_OAUTH;
    c->api_key = NULL;
    c->oauth.access = tok->access ? arena_cstr(s->arena, tok->access,
                                               strlen(tok->access)) : NULL;
    c->oauth.refresh = tok->refresh ? arena_cstr(s->arena, tok->refresh,
                                                 strlen(tok->refresh)) : NULL;
    c->oauth.expires = tok->expires;
    return save(s);
}

ash_status ash_auth_delete(ash_auth *s, const char *provider)
{
    if (s == NULL || provider == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_auth_delete: bad arguments");
    ash_credential *c = find(s, provider);
    if (c == NULL)
        return ASH_OK;
    c->kind = ASH_CRED_NONE;
    return save(s);
}

static const char *key_source(void *ud, const char *provider)
{
    return ash_auth_api_key((const ash_auth *)ud, provider);
}

void ash_auth_bind(const ash_auth *s)
{
    ash_provider_set_key_source(key_source, (void *)s);
}
