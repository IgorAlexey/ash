#include <assert.h>

#include "ash/term/input.h"
#include "ash/base/poison.h"

enum { ST_GROUND, ST_ESC, ST_CSI, ST_SS3, ST_PASTE };

static const char PASTE_MARK[] = "\033[201~";

void ash_input_init(ash_input *in)
{
    in->state = ST_GROUND;
    in->paste_match = 0;
    in->plen = 0;
}

static int is_text(uint8_t b)
{
    return b >= 0x20 && b != 0x7f;
}

static void put(ash_input_event *out, uint32_t *p, uint32_t cap, ash_input_event ev)
{
    assert(*p < cap);
    (void)cap;
    out[(*p)++] = ev;
}

static void key(ash_input_event *out, uint32_t *p, uint32_t cap,
                uint32_t k, uint32_t mods)
{
    ash_input_event ev = { ASH_EV_KEY, k, mods, NULL, 0, 0, 0, 0, 0 };
    put(out, p, cap, ev);
}

static void span(ash_input_event *out, uint32_t *p, uint32_t cap, ash_ev_kind kind,
                 const uint8_t *s, uint32_t len)
{
    ash_input_event ev = { kind, 0, 0, (const char *)s, len, 0, 0, 0, 0 };
    put(out, p, cap, ev);
}

static int16_t clamp_coord(uint32_t v)
{
    if (v == 0)
        return 0;
    v -= 1;
    return v > 0x7fff ? (int16_t)0x7fff : (int16_t)v;
}

static void mouse(ash_input_event *out, uint32_t *p, uint32_t cap,
                  uint32_t cb, uint32_t cx, uint32_t cy, int press)
{
    uint32_t mods = 0;
    if (cb & 4u)
        mods |= ASH_MOD_SHIFT;
    if (cb & 8u)
        mods |= ASH_MOD_ALT;
    if (cb & 16u)
        mods |= ASH_MOD_CTRL;

    uint32_t low = cb & 3u;
    uint8_t button;
    uint8_t action;
    if (cb & 64u) {
        button = ASH_MB_NONE;
        action = low == 0 ? ASH_MOUSE_WHEEL_UP : ASH_MOUSE_WHEEL_DOWN;
    } else if (cb & 32u) {
        button = (uint8_t)low;
        action = low == 3u ? ASH_MOUSE_MOVE : ASH_MOUSE_DRAG;
    } else {
        button = (uint8_t)low;
        action = press ? ASH_MOUSE_PRESS : ASH_MOUSE_RELEASE;
    }

    ash_input_event ev = { ASH_EV_MOUSE, 0, mods, NULL, 0,
                           clamp_coord(cx), clamp_coord(cy), button, action };
    put(out, p, cap, ev);
}

static uint32_t csi_param(const uint8_t *p, uint16_t plen, int index)
{
    int field = 0;
    uint32_t val = 0;
    int seen = 0;
    int sub = 0;
    for (uint16_t k = 0; k <= plen; k++) {
        uint8_t c = k < plen ? p[k] : (uint8_t)';';
        if (c == ';') {
            if (field == index)
                return seen ? val : 0;
            field++;
            val = 0;
            seen = 0;
            sub = 0;
            continue;
        }
        if (c == ':') {
            sub = 1;
            continue;
        }
        if (!sub && c >= '0' && c <= '9' && field == index) {
            val = val * 10u + (uint32_t)(c - '0');
            seen = 1;
        }
    }
    return 0;
}

static uint32_t csi_mods(const uint8_t *params, uint16_t plen)
{
    uint32_t f = csi_param(params, plen, 1);
    return f > 0 ? f - 1u : 0u;
}

static void csi_final(ash_input *in, uint8_t f, ash_input_event *out,
                      uint32_t *p, uint32_t cap)
{
    const uint8_t *ps = in->params;
    uint16_t pl = in->plen;
    in->state = ST_GROUND;

    if ((f == 'M' || f == 'm') && pl > 0 && ps[0] == '<') {
        mouse(out, p, cap, csi_param(ps, pl, 0), csi_param(ps, pl, 1),
              csi_param(ps, pl, 2), f == 'M');
        return;
    }
    if (f == 'u') {
        key(out, p, cap, csi_param(ps, pl, 0), csi_mods(ps, pl));
        return;
    }
    if (f == '~') {
        uint32_t code = csi_param(ps, pl, 0);
        if (code == 200) {
            span(out, p, cap, ASH_EV_PASTE_BEGIN, NULL, 0);
            in->state = ST_PASTE;
            in->paste_match = 0;
            return;
        }
        if (code == 201)
            return;
        if (code == 27) {
            key(out, p, cap, csi_param(ps, pl, 2), csi_mods(ps, pl));
            return;
        }
        uint32_t map = 0;
        switch (code) {
        case 1: case 7: map = ASH_KEY_HOME; break;
        case 2:         map = ASH_KEY_INSERT; break;
        case 3:         map = ASH_KEY_DELETE; break;
        case 4: case 8: map = ASH_KEY_END; break;
        case 5:         map = ASH_KEY_PGUP; break;
        case 6:         map = ASH_KEY_PGDN; break;
        default: return;
        }
        key(out, p, cap, map, csi_mods(ps, pl));
        return;
    }
    uint32_t mods = csi_mods(ps, pl);
    switch (f) {
    case 'A': key(out, p, cap, ASH_KEY_UP, mods); return;
    case 'B': key(out, p, cap, ASH_KEY_DOWN, mods); return;
    case 'C': key(out, p, cap, ASH_KEY_RIGHT, mods); return;
    case 'D': key(out, p, cap, ASH_KEY_LEFT, mods); return;
    case 'H': key(out, p, cap, ASH_KEY_HOME, mods); return;
    case 'F': key(out, p, cap, ASH_KEY_END, mods); return;
    case 'I': span(out, p, cap, ASH_EV_FOCUS_IN, NULL, 0); return;
    case 'O': span(out, p, cap, ASH_EV_FOCUS_OUT, NULL, 0); return;
    default: return;
    }
}

static void ground_control(ash_input_event *out, uint32_t *p, uint32_t cap, uint8_t b)
{
    if (b == 0x0d)
        key(out, p, cap, 13, 0);
    else if (b == 0x09)
        key(out, p, cap, 9, 0);
    else if (b == 0x7f)
        key(out, p, cap, 127, 0);
    else if (b == 0x00)
        key(out, p, cap, ' ', ASH_MOD_CTRL);
    else if (b <= 0x1a)
        key(out, p, cap, (uint32_t)('a' + (b - 1)), ASH_MOD_CTRL);
    else
        key(out, p, cap, (uint32_t)(b | 0x40), ASH_MOD_CTRL);
}

ash_status ash_input_feed(ash_input *in, const uint8_t *buf, uint32_t n,
                          ash_input_event *out, uint32_t cap,
                          uint32_t *consumed, uint32_t *produced)
{
    if (consumed == NULL || produced == NULL)
        return ash_fail(ASH_ERR_RANGE, "ash_input_feed: null consumed/produced");
    *consumed = 0;
    *produced = 0;
    if (in == NULL || out == NULL || cap < 2 || (buf == NULL && n > 0))
        return ash_fail(ASH_ERR_RANGE, "ash_input_feed: out null, cap<2, or buf null");

    uint32_t i = 0;
    uint32_t p = 0;

    while (i < n) {
        if (p + 2 > cap)
            break;
        uint8_t b = buf[i];

        switch (in->state) {
        case ST_GROUND:
            if (b == 0x1b) {
                in->state = ST_ESC;
                i++;
                break;
            }
            if (is_text(b)) {
                uint32_t start = i;
                while (i < n && is_text(buf[i]))
                    i++;
                span(out, &p, cap, ASH_EV_TEXT, buf + start, i - start);
                break;
            }
            ground_control(out, &p, cap, b);
            i++;
            break;

        case ST_ESC:
            if (b == '[') {
                in->plen = 0;
                in->state = ST_CSI;
                i++;
                break;
            }
            if (b == 'O') {
                in->state = ST_SS3;
                i++;
                break;
            }
            key(out, &p, cap, (uint32_t)b, ASH_MOD_ALT);
            in->state = ST_GROUND;
            i++;
            break;

        case ST_CSI:
            if ((b >= 0x30 && b <= 0x3f) || (b >= 0x20 && b <= 0x2f)) {
                if (in->plen < sizeof in->params)
                    in->params[in->plen++] = b;
                else
                    in->state = ST_GROUND;
                i++;
                break;
            }
            if (b >= 0x40 && b <= 0x7e) {
                csi_final(in, b, out, &p, cap);
                i++;
                break;
            }
            in->state = ST_GROUND;
            i++;
            break;

        case ST_SS3:
            switch (b) {
            case 'A': key(out, &p, cap, ASH_KEY_UP, 0); break;
            case 'B': key(out, &p, cap, ASH_KEY_DOWN, 0); break;
            case 'C': key(out, &p, cap, ASH_KEY_RIGHT, 0); break;
            case 'D': key(out, &p, cap, ASH_KEY_LEFT, 0); break;
            case 'H': key(out, &p, cap, ASH_KEY_HOME, 0); break;
            case 'F': key(out, &p, cap, ASH_KEY_END, 0); break;
            default: break;
            }
            in->state = ST_GROUND;
            i++;
            break;

        case ST_PASTE:
            if (b == (uint8_t)PASTE_MARK[in->paste_match]) {
                in->paste_match++;
                i++;
                if (in->paste_match == 6) {
                    span(out, &p, cap, ASH_EV_PASTE_END, NULL, 0);
                    in->paste_match = 0;
                    in->state = ST_GROUND;
                }
                break;
            }
            if (in->paste_match > 0) {
                span(out, &p, cap, ASH_EV_PASTE_CHUNK,
                     (const uint8_t *)PASTE_MARK, in->paste_match);
                in->paste_match = 0;
                break;
            }
            {
                uint32_t start = i;
                while (i < n && buf[i] != 0x1b)
                    i++;
                span(out, &p, cap, ASH_EV_PASTE_CHUNK, buf + start, i - start);
            }
            break;

        default:
            in->state = ST_GROUND;
            i++;
            break;
        }
    }

    *consumed = i;
    *produced = p;
    return ASH_OK;
}
