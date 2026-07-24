#ifndef ASH_UI_KEYS_H
#define ASH_UI_KEYS_H

#include <stdint.h>

#include "ash/base/api.h"
#include "ash/term/input.h"

typedef enum ash_editcmd {
    ASH_EC_NONE = 0,
    ASH_EC_INSERT,
    ASH_EC_NEWLINE,
    ASH_EC_SUBMIT,
    ASH_EC_BACKSPACE,
    ASH_EC_DELETE,
    ASH_EC_BACKSPACE_WORD,
    ASH_EC_DELETE_WORD,
    ASH_EC_KILL_TO_END,
    ASH_EC_KILL_LINE,
    ASH_EC_LEFT,
    ASH_EC_RIGHT,
    ASH_EC_UP,
    ASH_EC_DOWN,
    ASH_EC_WORD_LEFT,
    ASH_EC_WORD_RIGHT,
    ASH_EC_HOME,
    ASH_EC_END,
    ASH_EC_DOC_HOME,
    ASH_EC_DOC_END,
    ASH_EC_SELECT_LEFT,
    ASH_EC_SELECT_RIGHT,
    ASH_EC_SELECT_WORD_LEFT,
    ASH_EC_SELECT_WORD_RIGHT,
    ASH_EC_SELECT_UP,
    ASH_EC_SELECT_DOWN,
    ASH_EC_SELECT_HOME,
    ASH_EC_SELECT_END,
    ASH_EC_SELECT_DOC_HOME,
    ASH_EC_SELECT_DOC_END,
    ASH_EC_SELECT_ALL,
    ASH_EC_UNDO,
    ASH_EC_REDO,
    ASH_EC_COPY,
    ASH_EC_CUT,
    ASH_EC_PASTE,
    ASH_EC_PASTE_BEGIN,
    ASH_EC_PASTE_CHUNK,
    ASH_EC_PASTE_END,
    ASH_EC_CANCEL,
    ASH_EC_EOF
} ash_editcmd;

typedef struct ash_key {
    ash_editcmd cmd;
    const char *text;
    uint32_t    len;
} ash_key;

ASH_API ash_key ash_key_map(const ash_input_event *ev);

#endif
