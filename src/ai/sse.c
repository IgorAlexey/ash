#include <stddef.h>

#include "ash/ai/sse.h"
#include "ash/base/poison.h"

void ash_sse_init(ash_sse_parser *p, ash_arena *arena)
{
    ash_buf_init(&p->line, arena);
    ash_buf_init(&p->data, arena);
    ash_buf_init(&p->type, arena);
    p->prefix_len = 0;
    p->bom_done = false;
    p->saw_data = false;
    p->cr = false;
    p->dead = false;
}

static int field_is(const char *s, size_t n, const char *lit)
{
    size_t k = 0;
    while (k < n && lit[k] != '\0') {
        if (s[k] != lit[k])
            return 0;
        k++;
    }
    return k == n && lit[k] == '\0';
}

static ash_status dispatch_line(ash_sse_parser *p, ash_sse_emit emit, void *ud)
{
    size_t len = p->line.len;
    const char *s = (const char *)p->line.data;

    if (len == 0) {
        if (p->saw_data) {
            size_t dlen = p->data.len;
            if (dlen > 0 && p->data.data[dlen - 1] == '\n')
                dlen--;
            ash_slice type = p->type.len
                ? ash_slice_make((const char *)p->type.data, p->type.len)
                : ash_slice_from_cstr("message");
            ash_sse_event ev = {
                type,
                ash_slice_make((const char *)p->data.data, dlen),
            };
            emit(ud, &ev);
        }
        p->type.len = 0;
        p->data.len = 0;
        p->saw_data = false;
        return ASH_OK;
    }

    p->line.len = 0;

    if (s[0] == ':')
        return ASH_OK;

    size_t colon = len;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == ':') {
            colon = i;
            break;
        }
    }
    size_t vstart = colon < len ? colon + 1 : len;
    if (vstart < len && s[vstart] == ' ')
        vstart++;
    size_t vlen = len - vstart;

    if (field_is(s, colon, "event")) {
        p->type.len = 0;
        ash_buf_append(&p->type, s + vstart, vlen);
    } else if (field_is(s, colon, "data")) {
        if (p->data.len > (size_t)ASH_SSE_MAX_DATA - vlen - 1) {
            p->dead = true;
            return ash_fail(ASH_ERR_PROTOCOL, "SSE event data exceeds %u bytes",
                            ASH_SSE_MAX_DATA);
        }
        ash_buf_append(&p->data, s + vstart, vlen);
        ash_buf_append_byte(&p->data, '\n');
        p->saw_data = true;
    }
    return ASH_OK;
}

static ash_status feed_byte(ash_sse_parser *p, unsigned char c,
                           ash_sse_emit emit, void *ud)
{
    if (c == '\n') {
        if (p->cr) {
            p->cr = false;
            return ASH_OK;
        }
        return dispatch_line(p, emit, ud);
    }
    if (c == '\r') {
        p->cr = true;
        return dispatch_line(p, emit, ud);
    }
    p->cr = false;
    if (p->line.len >= ASH_SSE_MAX_LINE) {
        p->dead = true;
        return ash_fail(ASH_ERR_PROTOCOL, "SSE line exceeds %u bytes",
                        ASH_SSE_MAX_LINE);
    }
    ash_buf_append_byte(&p->line, c);
    return ASH_OK;
}

ash_status ash_sse_feed(ash_sse_parser *p, const void *bytes, size_t n,
                        ash_sse_emit emit, void *ud)
{
    if (p->dead)
        return ASH_ERR_PROTOCOL;

    const unsigned char *b = bytes;
    size_t i = 0;

    if (!p->bom_done) {
        static const unsigned char bom[3] = { 0xef, 0xbb, 0xbf };
        while (i < n && !p->bom_done) {
            unsigned char c = b[i];
            if (c != bom[p->prefix_len]) {
                for (uint8_t k = 0; k < p->prefix_len; k++)
                    ASH_TRY(feed_byte(p, p->prefix[k], emit, ud));
                p->prefix_len = 0;
                p->bom_done = true;
                break;
            }
            p->prefix[p->prefix_len++] = c;
            i++;
            if (p->prefix_len == 3) {
                p->prefix_len = 0;
                p->bom_done = true;
            }
        }
        if (!p->bom_done)
            return ASH_OK;
    }

    for (; i < n; i++)
        ASH_TRY(feed_byte(p, b[i], emit, ud));
    return ASH_OK;
}
