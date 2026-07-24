#include "ash/ui/keys.h"

static ash_key cmd(ash_editcmd c)
{
    ash_key k = { c, NULL, 0 };
    return k;
}

static ash_key text(const ash_input_event *ev, ash_editcmd c)
{
    ash_key k = { c, ev->text, ev->len };
    return k;
}

static ash_key map_key(uint32_t key, uint32_t mods)
{
    int shift = (mods & ASH_MOD_SHIFT) != 0;
    int ctrl = (mods & ASH_MOD_CTRL) != 0;
    int alt = (mods & ASH_MOD_ALT) != 0;
    int wordmod = ctrl || alt;

    switch (key) {
    case 13:
        return cmd(shift || alt ? ASH_EC_NEWLINE : ASH_EC_SUBMIT);
    case 8:
    case 127:
        return cmd(wordmod ? ASH_EC_BACKSPACE_WORD : ASH_EC_BACKSPACE);
    case ASH_KEY_DELETE:
        if (wordmod)
            return cmd(ASH_EC_DELETE_WORD);
        return cmd(shift ? ASH_EC_CUT : ASH_EC_DELETE);
    case ASH_KEY_INSERT:
        if (ctrl)
            return cmd(ASH_EC_COPY);
        return cmd(shift ? ASH_EC_PASTE : ASH_EC_NONE);
    case ASH_KEY_LEFT:
        if (wordmod)
            return cmd(shift ? ASH_EC_SELECT_WORD_LEFT : ASH_EC_WORD_LEFT);
        return cmd(shift ? ASH_EC_SELECT_LEFT : ASH_EC_LEFT);
    case ASH_KEY_RIGHT:
        if (wordmod)
            return cmd(shift ? ASH_EC_SELECT_WORD_RIGHT : ASH_EC_WORD_RIGHT);
        return cmd(shift ? ASH_EC_SELECT_RIGHT : ASH_EC_RIGHT);
    case ASH_KEY_UP:
        return cmd(shift ? ASH_EC_SELECT_UP : ASH_EC_UP);
    case ASH_KEY_DOWN:
        return cmd(shift ? ASH_EC_SELECT_DOWN : ASH_EC_DOWN);
    case ASH_KEY_HOME:
        if (ctrl)
            return cmd(shift ? ASH_EC_SELECT_DOC_HOME : ASH_EC_DOC_HOME);
        return cmd(shift ? ASH_EC_SELECT_HOME : ASH_EC_HOME);
    case ASH_KEY_END:
        if (ctrl)
            return cmd(shift ? ASH_EC_SELECT_DOC_END : ASH_EC_DOC_END);
        return cmd(shift ? ASH_EC_SELECT_END : ASH_EC_END);
    default:
        break;
    }

    uint32_t lk = key >= 'A' && key <= 'Z' ? key + 32 : key;

    if (ctrl && shift) {
        switch (lk) {
        case 'c': return cmd(ASH_EC_COPY);
        case 'x': return cmd(ASH_EC_CUT);
        case 'v': return cmd(ASH_EC_PASTE);
        case 'z': return cmd(ASH_EC_REDO);
        case 'a': return cmd(ASH_EC_SELECT_ALL);
        default: break;
        }
    }

    if (ctrl) {
        switch (lk) {
        case 'a': return cmd(ASH_EC_HOME);
        case 'e': return cmd(ASH_EC_END);
        case 'b': return cmd(ASH_EC_LEFT);
        case 'f': return cmd(ASH_EC_RIGHT);
        case 'h': return cmd(ASH_EC_BACKSPACE);
        case 'k': return cmd(ASH_EC_KILL_TO_END);
        case 'u': return cmd(ASH_EC_KILL_LINE);
        case 'w': return cmd(ASH_EC_BACKSPACE_WORD);
        case 'c': return cmd(ASH_EC_CANCEL);
        case 'd': return cmd(ASH_EC_EOF);
        case 'z': return cmd(ASH_EC_UNDO);
        default: break;
        }
        if (key == '_')
            return cmd(ASH_EC_UNDO);
    }

    if (alt) {
        switch (lk) {
        case 'd': return cmd(ASH_EC_DELETE_WORD);
        case 'c': return cmd(ASH_EC_COPY);
        case 'x': return cmd(ASH_EC_CUT);
        case 'v': return cmd(ASH_EC_PASTE);
        case 'a': return cmd(ASH_EC_SELECT_ALL);
        default: break;
        }
    }

    return cmd(ASH_EC_NONE);
}

ash_key ash_key_map(const ash_input_event *ev)
{
    switch (ev->kind) {
    case ASH_EV_TEXT:
        return text(ev, ASH_EC_INSERT);
    case ASH_EV_PASTE_BEGIN:
        return cmd(ASH_EC_PASTE_BEGIN);
    case ASH_EV_PASTE_CHUNK:
        return text(ev, ASH_EC_PASTE_CHUNK);
    case ASH_EV_PASTE_END:
        return cmd(ASH_EC_PASTE_END);
    case ASH_EV_KEY:
        return map_key(ev->key, ev->mods);
    case ASH_EV_NONE:
    case ASH_EV_FOCUS_IN:
    case ASH_EV_FOCUS_OUT:
    case ASH_EV_MOUSE:
        break;
    }
    return cmd(ASH_EC_NONE);
}
