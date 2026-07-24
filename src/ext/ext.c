#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"

#include "ash/base/arena.h"
#include "ash/base/buf.h"
#include "ash/base/status.h"
#include "ash/core/config.h"
#include "ash/ext/ext.h"
#include "ash/tools/tools.h"

enum { EXT_MAX_TOOLS = 32 };
enum { EXT_JSON_DEPTH = 64 };
enum { EXT_LUA_ALIGN = 16 };

typedef struct ext_alloc {
    ash_arena arena;
    size_t    live;
    size_t    budget;
} ext_alloc;

typedef struct ext_keybind {
    const char *name;
    int         cb_ref;
} ext_keybind;

typedef struct ext_tool_slot {
    ash_tool tool;
    ash_ext *e;
    int      cb_ref;
    int      in_use;
} ext_tool_slot;

struct ash_ext {
    lua_State        *L;
    ext_alloc         alloc;
    ash_arena         host;
    const ash_config *cfg;
    ash_ext_limits    limits;
    uint64_t          instr_used;

    ext_keybind      *keys;
    size_t            key_count;
    size_t            key_cap;

    size_t           *tools;
    size_t            tool_count;
    size_t            tool_cap;
};

static ext_tool_slot g_slots[EXT_MAX_TOOLS];

static ash_status ext_tool_run(size_t idx, ash_arena *out,
                               const ash_json *args, ash_tool_result *res);

#define EXT_SLOTS \
    X(0)  X(1)  X(2)  X(3)  X(4)  X(5)  X(6)  X(7)  \
    X(8)  X(9)  X(10) X(11) X(12) X(13) X(14) X(15) \
    X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) \
    X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31)

#define X(i) \
    static ash_status ext_tramp_##i(ash_arena *out, const ash_json *args, \
                                     ash_tool_result *res) \
    { return ext_tool_run((i), out, args, res); }
EXT_SLOTS
#undef X

static const ash_tool_fn EXT_TRAMPS[EXT_MAX_TOOLS] = {
#define X(i) ext_tramp_##i,
    EXT_SLOTS
#undef X
};

static ash_ext *ext_self(lua_State *L)
{
    return *(ash_ext **)lua_getextraspace(L);
}

static void *ext_alloc_fn(void *ud, void *ptr, size_t osize, size_t nsize)
{
    ext_alloc *al = ud;
    size_t old = ptr ? osize : 0;

    if (nsize == 0) {
        al->live = al->live >= old ? al->live - old : 0;
        return NULL;
    }
    if (nsize > old && al->live + (nsize - old) > al->budget)
        return NULL;

    void *np = ash_arena_alloc(&al->arena, nsize, EXT_LUA_ALIGN);
    if (old > 0)
        memcpy(np, ptr, old < nsize ? old : nsize);
    al->live = al->live - old + nsize;
    return np;
}

static void ext_hook(lua_State *L, lua_Debug *ar)
{
    (void)ar;
    ash_ext *e = ext_self(L);
    e->instr_used += (uint64_t)e->limits.hook_count;
    if (e->instr_used >= e->limits.instr_budget)
        (void)luaL_error(L, "instruction budget exceeded");
}

static const char *ext_arena_dup(ash_arena *a, const char *s, size_t n)
{
    char *b = ash_array(a, char, n + 1);
    memcpy(b, s, n);
    b[n] = 0;
    return b;
}

static void ext_json_escape(ash_buf *b, const char *s)
{
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            ash_buf_append_byte(b, '\\');
            ash_buf_append_byte(b, c);
        } else if (c == '\n') {
            ash_buf_append_cstr(b, "\\n");
        } else if (c == '\t') {
            ash_buf_append_cstr(b, "\\t");
        } else if (c == '\r') {
            ash_buf_append_cstr(b, "\\r");
        } else if (c < 0x20) {
            static const char hex[] = "0123456789abcdef";
            char esc[6] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 0xf] };
            ash_buf_append(b, esc, sizeof esc);
        } else {
            ash_buf_append_byte(b, c);
        }
    }
}

static const char *ext_build_schema(ash_arena *a, const char *name,
                                    const char *desc, const char *input_schema)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_buf_append_cstr(&b, "{\"name\":\"");
    ext_json_escape(&b, name);
    ash_buf_append_cstr(&b, "\",\"description\":\"");
    ext_json_escape(&b, desc);
    ash_buf_append_cstr(&b, "\",\"input_schema\":");
    ash_buf_append_cstr(&b, input_schema);
    ash_buf_append_byte(&b, '}');
    ash_buf_append_byte(&b, 0);
    return (const char *)b.data;
}

static void ext_keys_push(ash_ext *e, const char *name, int cb_ref)
{
    if (e->key_count == e->key_cap) {
        size_t ncap = e->key_cap ? e->key_cap * 2 : 8;
        ext_keybind *nk = ash_array(&e->host, ext_keybind, ncap);
        if (e->key_count)
            memcpy(nk, e->keys, e->key_count * sizeof *nk);
        e->keys = nk;
        e->key_cap = ncap;
    }
    e->keys[e->key_count].name = name;
    e->keys[e->key_count].cb_ref = cb_ref;
    e->key_count++;
}

static void ext_tools_push(ash_ext *e, size_t slot)
{
    if (e->tool_count == e->tool_cap) {
        size_t ncap = e->tool_cap ? e->tool_cap * 2 : 8;
        size_t *nt = ash_array(&e->host, size_t, ncap);
        if (e->tool_count)
            memcpy(nt, e->tools, e->tool_count * sizeof *nt);
        e->tools = nt;
        e->tool_cap = ncap;
    }
    e->tools[e->tool_count++] = slot;
}

static int ext_slot_alloc(void)
{
    for (int i = 0; i < EXT_MAX_TOOLS; i++)
        if (!g_slots[i].in_use)
            return i;
    return -1;
}

static void ext_push_json(lua_State *L, const ash_json *v, int depth)
{
    if (depth > EXT_JSON_DEPTH)
        (void)luaL_error(L, "ext: json nesting too deep");
    luaL_checkstack(L, 4, "ext json");

    switch (v->type) {
    case ASH_JSON_NULL:
        lua_pushnil(L);
        break;
    case ASH_JSON_BOOL:
        lua_pushboolean(L, v->u.boolean);
        break;
    case ASH_JSON_NUMBER: {
        char nb[64];
        if (v->u.num.n >= sizeof nb)
            (void)luaL_error(L, "ext: number token too long");
        memcpy(nb, v->u.num.p, v->u.num.n);
        nb[v->u.num.n] = 0;
        if (lua_stringtonumber(L, nb) == 0)
            (void)luaL_error(L, "ext: malformed number");
        break;
    }
    case ASH_JSON_STRING:
        lua_pushlstring(L, v->u.str.p, v->u.str.n);
        break;
    case ASH_JSON_ARRAY:
        lua_createtable(L, (int)v->u.arr.n, 0);
        for (size_t i = 0; i < v->u.arr.n; i++) {
            ext_push_json(L, &v->u.arr.v[i], depth + 1);
            lua_rawseti(L, -2, (lua_Integer)(i + 1));
        }
        break;
    case ASH_JSON_OBJECT:
        lua_createtable(L, 0, (int)v->u.obj.n);
        for (size_t i = 0; i < v->u.obj.n; i++) {
            const ash_json_member *m = &v->u.obj.v[i];
            lua_pushlstring(L, m->key, m->klen);
            ext_push_json(L, &m->val, depth + 1);
            lua_rawset(L, -3);
        }
        break;
    }
}

typedef struct ext_call {
    int             cb_ref;
    const ash_json *args;
} ext_call;

static int ext_do_call(lua_State *L)
{
    ext_call *c = lua_touserdata(L, 1);
    lua_rawgeti(L, LUA_REGISTRYINDEX, c->cb_ref);
    int nargs = 0;
    if (c->args) {
        ext_push_json(L, c->args, 0);
        nargs = 1;
    }
    lua_call(L, nargs, 2);

    int is_error = lua_toboolean(L, -1);
    lua_pop(L, 1);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        lua_pushliteral(L, "");
    } else {
        (void)luaL_tolstring(L, -1, NULL);
        lua_remove(L, -2);
    }
    lua_pushboolean(L, is_error);
    return 2;
}

static void ext_result(ash_arena *out, ash_tool_result *res,
                       const char *s, size_t n, int is_error)
{
    char *buf = ash_array(out, char, n + 1);
    memcpy(buf, s, n);
    buf[n] = 0;
    res->content = buf;
    res->len = n;
    res->is_error = is_error;
}

static ash_status ext_invoke(ash_ext *e, int cb_ref, const ash_json *args,
                             ash_arena *out, ash_tool_result *res)
{
    lua_State *L = e->L;
    e->instr_used = 0;
    lua_settop(L, 0);

    ext_call c = { cb_ref, args };
    lua_pushcfunction(L, ext_do_call);
    lua_pushlightuserdata(L, &c);
    int st = lua_pcall(L, 1, 2, 0);

    if (st != LUA_OK) {
        const char *msg = lua_tostring(L, -1);
        if (out && res)
            ext_result(out, res, msg ? msg : "ext error",
                       msg ? strlen(msg) : 9, 1);
        lua_settop(L, 0);
        return st == LUA_ERRMEM ? ASH_ERR_NOMEM : ASH_ERR_STATE;
    }

    if (out && res) {
        size_t clen = 0;
        const char *cs = lua_tolstring(L, -2, &clen);
        int is_error = lua_toboolean(L, -1);
        ext_result(out, res, cs ? cs : "", cs ? clen : 0, is_error);
    }
    lua_settop(L, 0);
    return ASH_OK;
}

static ash_status ext_tool_run(size_t idx, ash_arena *out,
                               const ash_json *args, ash_tool_result *res)
{
    res->content = NULL;
    res->len = 0;
    res->is_error = 0;

    ext_tool_slot *slot = &g_slots[idx];
    if (!slot->in_use || slot->e == NULL)
        return ash_fail(ASH_ERR_STATE, "ext: tool slot not live");

    (void)ext_invoke(slot->e, slot->cb_ref, args, out, res);
    return ASH_OK;
}

static int ext_os_getenv(lua_State *L)
{
    static const char *const allow[] = {
        "TERM", "LANG", "LC_ALL", "LC_CTYPE", "TZ", NULL
    };
    const char *name = luaL_checkstring(L, 1);
    for (int i = 0; allow[i]; i++) {
        if (strcmp(name, allow[i]) == 0) {
            const char *v = getenv(name);
            if (v)
                lua_pushstring(L, v);
            else
                lua_pushnil(L);
            return 1;
        }
    }
    lua_pushnil(L);
    return 1;
}

static int ext_os_time(lua_State *L)
{
    lua_pushinteger(L, (lua_Integer)time(NULL));
    return 1;
}

static int ext_os_clock(lua_State *L)
{
    lua_pushnumber(L, (lua_Number)clock() / (lua_Number)CLOCKS_PER_SEC);
    return 1;
}

static int ext_os_difftime(lua_State *L)
{
    lua_Number a = luaL_checknumber(L, 1);
    lua_Number b = luaL_optnumber(L, 2, 0);
    lua_pushnumber(L, a - b);
    return 1;
}

static int ext_get_config(lua_State *L)
{
    const char *key = luaL_checkstring(L, 1);
    ash_ext *e = ext_self(L);
    const char *v = NULL;
    if (e->cfg) {
        const ash_config *c = e->cfg;
        if (strcmp(key, "provider") == 0)      v = c->provider.value;
        else if (strcmp(key, "model") == 0)    v = c->model.value;
        else if (strcmp(key, "theme") == 0)    v = c->theme.value;
        else if (strcmp(key, "system") == 0)   v = c->system.value;
        else if (strcmp(key, "base_url") == 0) v = c->base_url.value;
        else if (strcmp(key, "thinking") == 0) v = ash_thinking_str(c->thinking);
    }
    if (v)
        lua_pushstring(L, v);
    else
        lua_pushnil(L);
    return 1;
}

static int ext_bind(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TFUNCTION);
    ash_ext *e = ext_self(L);

    const char *name_c = ext_arena_dup(&e->host, name, strlen(name));
    lua_pushvalue(L, 2);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    ext_keys_push(e, name_c, ref);
    return 0;
}

static int ext_register_tool(lua_State *L)
{
    ash_ext *e = ext_self(L);
    luaL_checktype(L, 1, LUA_TTABLE);

    lua_getfield(L, 1, "name");
    size_t nlen = 0;
    const char *name = luaL_checklstring(L, -1, &nlen);
    const char *name_c = ext_arena_dup(&e->host, name, nlen);
    lua_pop(L, 1);

    lua_getfield(L, 1, "description");
    const char *desc = luaL_optstring(L, -1, "");
    lua_getfield(L, 1, "schema");
    const char *schema = luaL_optstring(L, -1, "{\"type\":\"object\"}");
    const char *combined = ext_build_schema(&e->host, name_c, desc, schema);
    lua_pop(L, 2);

    lua_getfield(L, 1, "callback");
    luaL_checktype(L, -1, LUA_TFUNCTION);

    int slot = ext_slot_alloc();
    if (slot < 0)
        return luaL_error(L, "ash.register_tool: too many tools (max %d)",
                          EXT_MAX_TOOLS);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    g_slots[slot].tool.name = name_c;
    g_slots[slot].tool.schema = combined;
    g_slots[slot].tool.run = EXT_TRAMPS[slot];
    g_slots[slot].e = e;
    g_slots[slot].cb_ref = ref;
    g_slots[slot].in_use = 1;
    ext_tools_push(e, (size_t)slot);
    return 0;
}

static int ext_open_sandbox(lua_State *L)
{
    static const luaL_Reg libs[] = {
        { LUA_GNAME,      luaopen_base },
        { LUA_TABLIBNAME, luaopen_table },
        { LUA_STRLIBNAME, luaopen_string },
        { LUA_MATHLIBNAME, luaopen_math },
        { LUA_UTF8LIBNAME, luaopen_utf8 },
        { LUA_COLIBNAME,  luaopen_coroutine },
    };
    for (size_t i = 0; i < sizeof libs / sizeof libs[0]; i++) {
        luaL_requiref(L, libs[i].name, libs[i].func, 1);
        lua_pop(L, 1);
    }

    static const char *const banned[] = {
        "dofile", "loadfile", "load", "require", NULL
    };
    for (int i = 0; banned[i]; i++) {
        lua_pushnil(L);
        lua_setglobal(L, banned[i]);
    }

    lua_createtable(L, 0, 4);
    lua_pushcfunction(L, ext_os_time);
    lua_setfield(L, -2, "time");
    lua_pushcfunction(L, ext_os_clock);
    lua_setfield(L, -2, "clock");
    lua_pushcfunction(L, ext_os_difftime);
    lua_setfield(L, -2, "difftime");
    lua_pushcfunction(L, ext_os_getenv);
    lua_setfield(L, -2, "getenv");
    lua_setglobal(L, "os");

    lua_createtable(L, 0, 3);
    lua_pushcfunction(L, ext_get_config);
    lua_setfield(L, -2, "get_config");
    lua_pushcfunction(L, ext_bind);
    lua_setfield(L, -2, "bind");
    lua_pushcfunction(L, ext_register_tool);
    lua_setfield(L, -2, "register_tool");
    lua_setglobal(L, "ash");
    return 0;
}

ash_ext_limits ash_ext_limits_default(void)
{
    ash_ext_limits d;
    d.mem_budget = 16u * 1024u * 1024u;
    d.instr_budget = 100u * 1000u * 1000u;
    d.hook_count = 1000;
    return d;
}

static void ext_free(ash_ext *e)
{
    for (size_t i = 0; i < e->tool_count; i++) {
        ext_tool_slot *s = &g_slots[e->tools[i]];
        s->in_use = 0;
        s->e = NULL;
        s->cb_ref = 0;
    }
    if (e->L)
        lua_close(e->L);
    ash_arena_destroy(&e->alloc.arena);
    ash_arena host = e->host;
    ash_arena_destroy(&host);
}

ash_status ash_ext_create(ash_ext **out, const ash_ext_limits *lim,
                          const ash_config *cfg)
{
    *out = NULL;
    ash_ext_limits limits = lim ? *lim : ash_ext_limits_default();
    if (limits.instr_budget == 0 || limits.hook_count <= 0 ||
        limits.mem_budget == 0)
        return ash_fail(ASH_ERR_RANGE, "ext: invalid limits");

    ash_arena host;
    ASH_TRY(ash_arena_create(&host, "ext-host", 1u << 16));

    ash_ext *e = ash_new(&host, ash_ext);
    memset(e, 0, sizeof *e);
    e->host = host;
    e->limits = limits;
    e->cfg = cfg;
    e->alloc.budget = limits.mem_budget;

    ash_status st = ash_arena_create(&e->alloc.arena, "ext-lua", 1u << 18);
    if (st != ASH_OK) {
        ash_arena h = e->host;
        ash_arena_destroy(&h);
        return st;
    }

    e->L = lua_newstate(ext_alloc_fn, &e->alloc);
    if (e->L == NULL) {
        ext_free(e);
        return ash_fail(ASH_ERR_NOMEM, "ext: lua_newstate failed");
    }
    *(ash_ext **)lua_getextraspace(e->L) = e;

    lua_pushcfunction(e->L, ext_open_sandbox);
    if (lua_pcall(e->L, 0, 0, 0) != LUA_OK) {
        const char *msg = lua_tostring(e->L, -1);
        ash_status r = ash_fail(ASH_ERR_STATE, "ext init: %s",
                                msg ? msg : "unknown");
        ext_free(e);
        return r;
    }

    lua_sethook(e->L, ext_hook, LUA_MASKCOUNT, e->limits.hook_count);
    *out = e;
    return ASH_OK;
}

void ash_ext_destroy(ash_ext *e)
{
    if (e)
        ext_free(e);
}

ash_status ash_ext_eval(ash_ext *e, const char *src, size_t len,
                        const char *name)
{
    e->instr_used = 0;
    lua_settop(e->L, 0);

    if (luaL_loadbufferx(e->L, src, len, name ? name : "=chunk", "t") != LUA_OK) {
        const char *msg = lua_tostring(e->L, -1);
        ash_status r = ash_fail(ASH_ERR_PARSE, "lua load: %s",
                                msg ? msg : "syntax error");
        lua_settop(e->L, 0);
        return r;
    }

    int st = lua_pcall(e->L, 0, 0, 0);
    if (st != LUA_OK) {
        const char *msg = lua_tostring(e->L, -1);
        ash_status r = ash_fail(st == LUA_ERRMEM ? ASH_ERR_NOMEM : ASH_ERR_STATE,
                                "lua: %s", msg ? msg : "runtime error");
        lua_settop(e->L, 0);
        return r;
    }
    return ASH_OK;
}

size_t ash_ext_tool_count(const ash_ext *e)
{
    return e->tool_count;
}

ash_tool *ash_ext_tool_at(ash_ext *e, size_t i)
{
    if (i >= e->tool_count)
        return NULL;
    return &g_slots[e->tools[i]].tool;
}

size_t ash_ext_keybinding_count(const ash_ext *e)
{
    return e->key_count;
}

const char *ash_ext_keybinding_name(const ash_ext *e, size_t i)
{
    if (i >= e->key_count)
        return NULL;
    return e->keys[i].name;
}

ash_status ash_ext_keybinding_invoke(ash_ext *e, const char *name)
{
    for (size_t i = 0; i < e->key_count; i++) {
        if (strcmp(e->keys[i].name, name) == 0)
            return ext_invoke(e, e->keys[i].cb_ref, NULL, NULL, NULL);
    }
    return ash_fail(ASH_ERR_NOTFOUND, "ext: no keybinding '%s'", name);
}
