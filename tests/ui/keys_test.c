#include <string.h>

#include "ash/term/input.h"
#include "ash/ui/keys.h"
#include "ash_test.h"

enum { MAXCMD = 512, MAXTXT = 4096 };

typedef struct seq {
    ash_editcmd cmd[MAXCMD];
    int         n;
    char        text[MAXTXT];
    size_t      tlen;
} seq;

static void run(const char *in, size_t n, size_t chunk, seq *s)
{
    ash_input parser;
    ash_input_init(&parser);
    s->n = 0;
    s->tlen = 0;
    ash_input_event out[128];
    for (size_t off = 0; off < n; off += chunk) {
        size_t m = chunk < n - off ? chunk : n - off;
        size_t fed = 0;
        while (fed < m) {
            uint32_t consumed = 0, produced = 0;
            ASH_IGNORE(ash_input_feed(&parser, (const uint8_t *)in + off + fed,
                                      (uint32_t)(m - fed), out, 128,
                                      &consumed, &produced));
            for (uint32_t k = 0; k < produced; k++) {
                ash_key key = ash_key_map(&out[k]);
                if (s->n < MAXCMD)
                    s->cmd[s->n++] = key.cmd;
                if ((key.cmd == ASH_EC_INSERT || key.cmd == ASH_EC_PASTE_CHUNK) &&
                    key.len && s->tlen + key.len <= MAXTXT) {
                    memcpy(s->text + s->tlen, key.text, key.len);
                    s->tlen += key.len;
                }
            }
            if (consumed == 0 && produced == 0)
                break;
            fed += consumed;
        }
    }
}

static int only(const seq *s, ash_editcmd c)
{
    return s->n == 1 && s->cmd[0] == c;
}

static void test_text(void)
{
    seq s;
    run("abc", 3, 3, &s);
    ASH_CHECK(s.n == 1 && s.cmd[0] == ASH_EC_INSERT);
    ASH_CHECK(s.tlen == 3 && memcmp(s.text, "abc", 3) == 0);
}

static void test_enter_variants(void)
{
    seq s;
    run("\r", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_SUBMIT));
    run("\033[13;2u", 7, 7, &s);
    ASH_CHECK(only(&s, ASH_EC_NEWLINE));
    run("\033\r", 2, 2, &s);
    ASH_CHECK(only(&s, ASH_EC_NEWLINE));
    run("\033[13;5u", 7, 7, &s);
    ASH_CHECK(only(&s, ASH_EC_SUBMIT));
}

static void test_edit_keys(void)
{
    seq s;
    run("\177", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_BACKSPACE));
    run("\033\177", 2, 2, &s);
    ASH_CHECK(only(&s, ASH_EC_BACKSPACE_WORD));
    run("\033[3~", 4, 4, &s);
    ASH_CHECK(only(&s, ASH_EC_DELETE));
    run("\033[3;5~", 6, 6, &s);
    ASH_CHECK(only(&s, ASH_EC_DELETE_WORD));
}

static void test_motion(void)
{
    seq s;
    run("\033[D", 3, 3, &s);
    ASH_CHECK(only(&s, ASH_EC_LEFT));
    run("\033[C", 3, 3, &s);
    ASH_CHECK(only(&s, ASH_EC_RIGHT));
    run("\033[A", 3, 3, &s);
    ASH_CHECK(only(&s, ASH_EC_UP));
    run("\033[B", 3, 3, &s);
    ASH_CHECK(only(&s, ASH_EC_DOWN));
    run("\033[H", 3, 3, &s);
    ASH_CHECK(only(&s, ASH_EC_HOME));
    run("\033[F", 3, 3, &s);
    ASH_CHECK(only(&s, ASH_EC_END));
    run("\033[1;5D", 6, 6, &s);
    ASH_CHECK(only(&s, ASH_EC_WORD_LEFT));
    run("\033[1;5C", 6, 6, &s);
    ASH_CHECK(only(&s, ASH_EC_WORD_RIGHT));
    run("\033[1;3D", 6, 6, &s);
    ASH_CHECK(only(&s, ASH_EC_WORD_LEFT));
}

static void test_ctrl(void)
{
    seq s;
    run("\001", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_HOME));
    run("\005", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_END));
    run("\013", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_KILL_TO_END));
    run("\025", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_KILL_LINE));
    run("\027", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_BACKSPACE_WORD));
    run("\003", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_CANCEL));
    run("\004", 1, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_EOF));
}

static void test_paste(void)
{
    const char *p = "\033[200~hi\nthere\033[201~";
    size_t pl = strlen(p);
    seq whole, split;
    run(p, pl, pl, &whole);
    run(p, pl, 1, &split);
    for (int pass = 0; pass < 2; pass++) {
        seq *s = pass ? &split : &whole;
        int begins = 0, ends = 0, submits = 0, newlines = 0;
        for (int i = 0; i < s->n; i++) {
            begins += s->cmd[i] == ASH_EC_PASTE_BEGIN;
            ends += s->cmd[i] == ASH_EC_PASTE_END;
            submits += s->cmd[i] == ASH_EC_SUBMIT;
            newlines += s->cmd[i] == ASH_EC_NEWLINE;
        }
        ASH_CHECK(begins == 1 && ends == 1);
        ASH_CHECK(submits == 0 && newlines == 0);
        ASH_CHECK(s->tlen == 8 && memcmp(s->text, "hi\nthere", 8) == 0);
    }
}

static void chord(const char *in, ash_editcmd want)
{
    seq s;
    run(in, strlen(in), strlen(in), &s);
    ASH_CHECK(only(&s, want));
    run(in, strlen(in), 1, &s);
    ASH_CHECK(only(&s, want));
}

static void test_selection_chords(void)
{
    chord("\033[1;2C", ASH_EC_SELECT_RIGHT);
    chord("\033[1;2D", ASH_EC_SELECT_LEFT);
    chord("\033[1;6C", ASH_EC_SELECT_WORD_RIGHT);
    chord("\033[1;6D", ASH_EC_SELECT_WORD_LEFT);
    chord("\033[1;2A", ASH_EC_SELECT_UP);
    chord("\033[1;2B", ASH_EC_SELECT_DOWN);
    chord("\033[1;2H", ASH_EC_SELECT_HOME);
    chord("\033[1;2F", ASH_EC_SELECT_END);
    chord("\033[1;5H", ASH_EC_DOC_HOME);
    chord("\033[1;5F", ASH_EC_DOC_END);
    chord("\033[1;6H", ASH_EC_SELECT_DOC_HOME);
    chord("\033[1;6F", ASH_EC_SELECT_DOC_END);
}

static void test_clipboard_chords(void)
{
    chord("\033[99;6u", ASH_EC_COPY);
    chord("\033[27;6;99~", ASH_EC_COPY);
    chord("\033[27;6;67~", ASH_EC_COPY);
    chord("\033c", ASH_EC_COPY);
    chord("\033[2;5~", ASH_EC_COPY);
    chord("\033[120;6u", ASH_EC_CUT);
    chord("\033x", ASH_EC_CUT);
    chord("\033[3;2~", ASH_EC_CUT);
    chord("\033[118;6u", ASH_EC_PASTE);
    chord("\033v", ASH_EC_PASTE);
    chord("\033[2;2~", ASH_EC_PASTE);
}

static void test_undo_redo_chords(void)
{
    chord("\032", ASH_EC_UNDO);
    chord("\037", ASH_EC_UNDO);
    chord("\033[122;6u", ASH_EC_REDO);
    chord("\033[97;6u", ASH_EC_SELECT_ALL);
    chord("\033a", ASH_EC_SELECT_ALL);
}

static void test_word_delete_chords(void)
{
    chord("\033[127;5u", ASH_EC_BACKSPACE_WORD);
    chord("\033[27;5;127~", ASH_EC_BACKSPACE_WORD);
    chord("\010", ASH_EC_BACKSPACE);
    chord("\033d", ASH_EC_DELETE_WORD);
}

static void test_split_escape(void)
{
    seq s;
    run("\033[1;5D", 6, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_WORD_LEFT));
    run("\033[13;2u", 7, 1, &s);
    ASH_CHECK(only(&s, ASH_EC_NEWLINE));
}

int main(void)
{
    test_text();
    test_enter_variants();
    test_edit_keys();
    test_motion();
    test_ctrl();
    test_selection_chords();
    test_clipboard_chords();
    test_undo_redo_chords();
    test_word_delete_chords();
    test_paste();
    test_split_escape();
    return ash_test_done();
}
