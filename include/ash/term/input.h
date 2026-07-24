#ifndef ASH_TERM_INPUT_H
#define ASH_TERM_INPUT_H

#include <stddef.h>
#include <stdint.h>

#include "ash/base/api.h"
#include "ash/base/status.h"

typedef enum ash_ev_kind {
    ASH_EV_NONE = 0,
    ASH_EV_KEY,
    ASH_EV_TEXT,
    ASH_EV_PASTE_BEGIN,
    ASH_EV_PASTE_CHUNK,
    ASH_EV_PASTE_END,
    ASH_EV_FOCUS_IN,
    ASH_EV_FOCUS_OUT,
    ASH_EV_MOUSE
} ash_ev_kind;

typedef enum ash_mouse_action {
    ASH_MOUSE_PRESS = 0,
    ASH_MOUSE_RELEASE,
    ASH_MOUSE_DRAG,
    ASH_MOUSE_MOVE,
    ASH_MOUSE_WHEEL_UP,
    ASH_MOUSE_WHEEL_DOWN
} ash_mouse_action;

typedef enum ash_mouse_button {
    ASH_MB_LEFT   = 0,
    ASH_MB_MIDDLE = 1,
    ASH_MB_RIGHT  = 2,
    ASH_MB_NONE   = 3
} ash_mouse_button;

typedef enum ash_mods {
    ASH_MOD_SHIFT = 1u << 0,
    ASH_MOD_ALT   = 1u << 1,
    ASH_MOD_CTRL  = 1u << 2,
    ASH_MOD_SUPER = 1u << 3
} ash_mods;

enum {
    ASH_KEY_UP = 0x110000,
    ASH_KEY_DOWN,
    ASH_KEY_LEFT,
    ASH_KEY_RIGHT,
    ASH_KEY_HOME,
    ASH_KEY_END,
    ASH_KEY_PGUP,
    ASH_KEY_PGDN,
    ASH_KEY_INSERT,
    ASH_KEY_DELETE
};

typedef struct ash_input_event {
    ash_ev_kind kind;
    uint32_t    key;
    uint32_t    mods;
    const char *text;
    uint32_t    len;
    int16_t     mx;
    int16_t     my;
    uint8_t     mbutton;
    uint8_t     maction;
} ash_input_event;

typedef struct ash_input {
    uint8_t  state;
    uint8_t  paste_match;
    uint16_t plen;
    uint8_t  params[64];
} ash_input;

ASH_API void ash_input_init(ash_input *in);

ASH_API ASH_WUR ash_status ash_input_feed(ash_input *in,
                                          const uint8_t *buf, uint32_t n,
                                          ash_input_event *out, uint32_t cap,
                                          uint32_t *consumed, uint32_t *produced);

#endif
