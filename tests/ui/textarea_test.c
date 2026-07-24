#include <string.h>

#include "ash/base/arena.h"
#include "ash/ui/textarea.h"
#include "ash_test.h"

static int text_is(const ash_textarea *ta, const char *s)
{
    size_t n = strlen(s);
    return ta->len == n && memcmp(ta->data, s, n) == 0;
}

static void apply(ash_textarea *ta, ash_editcmd c)
{
    ash_key k = { c, NULL, 0 };
    ash_textarea_apply(ta, k);
}

static void ins(ash_textarea *ta, const char *s)
{
    ash_textarea_insert(ta, s, strlen(s));
}

static void test_insert_cursor(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "hello");
    ASH_CHECK(text_is(&ta, "hello"));
    ASH_CHECK(ash_textarea_cursor(&ta) == 5);
    ASH_CHECK(ash_textarea_at_end(&ta));

    apply(&ta, ASH_EC_LEFT);
    apply(&ta, ASH_EC_LEFT);
    ASH_CHECK(ash_textarea_cursor(&ta) == 3);
    ins(&ta, "XX");
    ASH_CHECK(text_is(&ta, "helXXlo"));
    ASH_CHECK(ash_textarea_cursor(&ta) == 5);

    apply(&ta, ASH_EC_HOME);
    ASH_CHECK(ash_textarea_cursor(&ta) == 0);
    apply(&ta, ASH_EC_END);
    ASH_CHECK(ash_textarea_cursor(&ta) == 7);

    ash_arena_destroy(&a);
}

static void test_backspace_delete(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "hello");
    int w = ash_textarea_backspace(&ta);
    ASH_CHECK(w == 1 && text_is(&ta, "hell"));

    ash_textarea_clear(&ta);
    ins(&ta, "a\xc3\xa9");
    w = ash_textarea_backspace(&ta);
    ASH_CHECK(w == 1 && text_is(&ta, "a"));

    ash_textarea_clear(&ta);
    ins(&ta, "hello");
    apply(&ta, ASH_EC_DOC_HOME);
    apply(&ta, ASH_EC_DELETE);
    ASH_CHECK(text_is(&ta, "ello"));

    ash_arena_destroy(&a);
}

static void test_word_ops(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "foo bar baz");
    apply(&ta, ASH_EC_WORD_LEFT);
    ASH_CHECK(ash_textarea_cursor(&ta) == 8);
    apply(&ta, ASH_EC_WORD_LEFT);
    ASH_CHECK(ash_textarea_cursor(&ta) == 4);

    apply(&ta, ASH_EC_DOC_END);
    apply(&ta, ASH_EC_BACKSPACE_WORD);
    ASH_CHECK(text_is(&ta, "foo bar "));

    apply(&ta, ASH_EC_DOC_HOME);
    apply(&ta, ASH_EC_DELETE_WORD);
    ASH_CHECK(text_is(&ta, " bar "));

    ash_arena_destroy(&a);
}

static void test_kill(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "one two\nthree");
    apply(&ta, ASH_EC_DOC_HOME);
    apply(&ta, ASH_EC_RIGHT);
    apply(&ta, ASH_EC_RIGHT);
    apply(&ta, ASH_EC_RIGHT);
    apply(&ta, ASH_EC_KILL_TO_END);
    ASH_CHECK(text_is(&ta, "one\nthree"));

    apply(&ta, ASH_EC_KILL_LINE);
    ASH_CHECK(text_is(&ta, "\nthree"));

    ash_arena_destroy(&a);
}

static void test_newline_paste_no_submit(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "a");
    apply(&ta, ASH_EC_NEWLINE);
    ins(&ta, "b");
    ASH_CHECK(text_is(&ta, "a\nb"));

    ash_key chunk = { ASH_EC_PASTE_CHUNK, "x\ny", 3 };
    ash_textarea_apply(&ta, chunk);
    ASH_CHECK(text_is(&ta, "a\nbx\ny"));

    ash_arena_destroy(&a);
}

static int sel_is(const ash_textarea *ta, ash_arena *a, const char *s)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_textarea_selection(ta, &b);
    size_t n = strlen(s);
    return b.len == n && (n == 0 || memcmp(b.data, s, n) == 0);
}

static void test_selection(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "hello world");
    apply(&ta, ASH_EC_DOC_HOME);
    ASH_CHECK(!ash_textarea_has_selection(&ta));

    apply(&ta, ASH_EC_SELECT_RIGHT);
    apply(&ta, ASH_EC_SELECT_RIGHT);
    apply(&ta, ASH_EC_SELECT_RIGHT);
    ASH_CHECK(ash_textarea_has_selection(&ta));
    ASH_CHECK(sel_is(&ta, &a, "hel"));
    ASH_CHECK(ash_textarea_cursor(&ta) == 3);

    apply(&ta, ASH_EC_LEFT);
    ASH_CHECK(!ash_textarea_has_selection(&ta));
    ASH_CHECK(ash_textarea_cursor(&ta) == 0);

    apply(&ta, ASH_EC_SELECT_WORD_RIGHT);
    ASH_CHECK(sel_is(&ta, &a, "hello"));
    apply(&ta, ASH_EC_RIGHT);
    ASH_CHECK(ash_textarea_cursor(&ta) == 5);

    apply(&ta, ASH_EC_SELECT_ALL);
    ASH_CHECK(sel_is(&ta, &a, "hello world"));

    ins(&ta, "x");
    ASH_CHECK(text_is(&ta, "x"));
    ASH_CHECK(!ash_textarea_has_selection(&ta));

    ins(&ta, "yz");
    apply(&ta, ASH_EC_SELECT_HOME);
    ASH_CHECK(sel_is(&ta, &a, "xyz"));
    apply(&ta, ASH_EC_BACKSPACE);
    ASH_CHECK(text_is(&ta, ""));

    ash_arena_destroy(&a);
}

static void test_undo_redo(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "one");
    apply(&ta, ASH_EC_UNDO);
    ASH_CHECK(text_is(&ta, ""));
    apply(&ta, ASH_EC_REDO);
    ASH_CHECK(text_is(&ta, "one"));

    apply(&ta, ASH_EC_DOC_HOME);
    ins(&ta, "X");
    ASH_CHECK(text_is(&ta, "Xone"));
    apply(&ta, ASH_EC_UNDO);
    ASH_CHECK(text_is(&ta, "one"));
    apply(&ta, ASH_EC_UNDO);
    ASH_CHECK(text_is(&ta, ""));
    apply(&ta, ASH_EC_REDO);
    ASH_CHECK(text_is(&ta, "one"));
    apply(&ta, ASH_EC_REDO);
    ASH_CHECK(text_is(&ta, "Xone"));

    apply(&ta, ASH_EC_UNDO);
    ASH_CHECK(text_is(&ta, "one"));
    ins(&ta, "Y");
    ASH_CHECK(text_is(&ta, "oneY"));
    apply(&ta, ASH_EC_REDO);
    ASH_CHECK(text_is(&ta, "oneY"));

    ash_arena_destroy(&a);
}

static void test_cut_selection(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "hello world");
    apply(&ta, ASH_EC_DOC_HOME);
    apply(&ta, ASH_EC_SELECT_WORD_RIGHT);
    ASH_CHECK(sel_is(&ta, &a, "hello"));

    ash_textarea_delete_selection(&ta);
    ASH_CHECK(text_is(&ta, " world"));
    ASH_CHECK(!ash_textarea_has_selection(&ta));

    apply(&ta, ASH_EC_UNDO);
    ASH_CHECK(text_is(&ta, "hello world"));

    ash_arena_destroy(&a);
}

static void test_render_selection(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 20, 0);

    ins(&ta, "hello");
    apply(&ta, ASH_EC_DOC_HOME);
    apply(&ta, ASH_EC_SELECT_RIGHT);
    apply(&ta, ASH_EC_SELECT_RIGHT);
    apply(&ta, ASH_EC_SELECT_RIGHT);

    ash_style def = { ASH_RGBA_DEFAULT, ASH_RGBA_DEFAULT, 0 };
    ash_fb fb;
    ash_fb_init(&fb, &a, def);
    ash_fb_begin(&fb, 20, 3);
    ash_rect r = { 0, 0, 20, 1 };
    ash_textarea_render(&ta, &fb, r, def);

    const ash_cell *cells = fb.buffers[fb.frame];
    ash_style sty = ash_selection_style();
    for (int x = 0; x < 3; x++) {
        ASH_CHECK(cells[x].bg == sty.bg && cells[x].fg == sty.fg);
        ASH_CHECK(!(cells[x].attr & ASH_ATTR_REVERSE));
    }
    ASH_CHECK(cells[3].bg != sty.bg);
    ASH_CHECK(cells[4].bg != sty.bg);

    ash_arena_destroy(&a);
}

static void test_wrap_rows(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;

    ash_textarea_init(&ta, &a, 80, 0);
    ins(&ta, "a\nb\nc");
    ASH_CHECK(ash_textarea_rows(&ta) == 3);

    ash_textarea_init(&ta, &a, 5, 0);
    ins(&ta, "aaaaaaa");
    ASH_CHECK(ash_textarea_rows(&ta) == 2);
    int r = 0, c = 0;
    ash_textarea_cursor_rc(&ta, &r, &c);
    ASH_CHECK(r == 1 && c == 2);

    ash_textarea_init(&ta, &a, 10, 0);
    ins(&ta, "hello world foo");
    ASH_CHECK(ash_textarea_rows(&ta) == 2);

    ash_arena_destroy(&a);
}

static void test_updown(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "ta", 1u << 16) == ASH_OK);
    ash_textarea ta;
    ash_textarea_init(&ta, &a, 80, 0);

    ins(&ta, "abc\ndef");
    apply(&ta, ASH_EC_DOC_HOME);
    apply(&ta, ASH_EC_DOWN);
    apply(&ta, ASH_EC_RIGHT);
    ASH_CHECK(ash_textarea_cursor(&ta) == 5);
    int r = 0, c = 0;
    ash_textarea_cursor_rc(&ta, &r, &c);
    ASH_CHECK(r == 1 && c == 1);

    apply(&ta, ASH_EC_UP);
    ash_textarea_cursor_rc(&ta, &r, &c);
    ASH_CHECK(r == 0 && c == 1);
    ASH_CHECK(ash_textarea_cursor(&ta) == 1);

    ash_arena_destroy(&a);
}

#define DASH "\xe2\x94\x80"
#define UP   "\xe2\x86\x91"
#define DOWN "\xe2\x86\x93"

static int rule_is(ash_arena *a, ash_input_rule r, int below, const char *want)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_input_rule_text(r, below, &b);
    size_t n = strlen(want);
    return b.len == n && memcmp(b.data, want, n) == 0;
}

static int rule_cols(ash_arena *a, ash_input_rule r, int below)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_input_rule_text(r, below, &b);
    int cols = 0;
    size_t i = 0;
    while (i < b.len) {
        uint32_t cp = 0;
        size_t adv = ash_utf8_decode(b.data + i, b.len - i, &cp);
        if (adv == 0)
            adv = 1;
        int w = ash_char_width(cp);
        if (w < 0)
            w = 0;
        cols += w;
        i += adv;
    }
    return cols;
}

static void test_input_rule(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "rule", 1u << 16) == ASH_OK);

    ash_input_rule none = { 0, 3, 3, 10 };
    ASH_CHECK(ash_input_rule_hidden(none, 0) == 0);
    ASH_CHECK(ash_input_rule_hidden(none, 1) == 0);
    ASH_CHECK(rule_is(&a, none, 0,
                      DASH DASH DASH DASH DASH DASH DASH DASH DASH DASH));
    ASH_CHECK(rule_is(&a, none, 1,
                      DASH DASH DASH DASH DASH DASH DASH DASH DASH DASH));

    ash_input_rule down = { 2, 3, 8, 20 };
    ASH_CHECK(ash_input_rule_hidden(down, 0) == 2);
    ASH_CHECK(rule_is(&a, down, 0,
                      DASH DASH DASH " " UP " 2 more "
                      DASH DASH DASH DASH DASH DASH DASH));

    ash_input_rule below = { 0, 3, 8, 20 };
    ASH_CHECK(ash_input_rule_hidden(below, 1) == 5);
    ASH_CHECK(rule_is(&a, below, 1,
                      DASH DASH DASH " " DOWN " 5 more "
                      DASH DASH DASH DASH DASH DASH DASH));

    ash_input_rule both = { 2, 3, 10, 20 };
    ASH_CHECK(ash_input_rule_hidden(both, 0) == 2);
    ASH_CHECK(ash_input_rule_hidden(both, 1) == 5);
    ASH_CHECK(rule_is(&a, both, 0,
                      DASH DASH DASH " " UP " 2 more "
                      DASH DASH DASH DASH DASH DASH DASH));
    ASH_CHECK(rule_is(&a, both, 1,
                      DASH DASH DASH " " DOWN " 5 more "
                      DASH DASH DASH DASH DASH DASH DASH));

    ash_input_rule tiny = { 4, 3, 40, 5 };
    ASH_CHECK(rule_is(&a, tiny, 0, DASH DASH DASH " " UP));
    tiny.width = 3;
    ASH_CHECK(rule_is(&a, tiny, 0, DASH DASH DASH));
    tiny.width = 1;
    ASH_CHECK(rule_is(&a, tiny, 0, DASH));
    tiny.width = 0;
    ASH_CHECK(rule_is(&a, tiny, 0, ""));

    int widths[] = { 1, 2, 7, 13, 40, 41 };
    for (size_t i = 0; i < sizeof widths / sizeof widths[0]; i++) {
        ash_input_rule pr = { 0, 3, 3, widths[i] };
        ash_input_rule sr = { 7, 3, 99, widths[i] };
        ASH_CHECK(rule_cols(&a, pr, 0) == widths[i]);
        ASH_CHECK(rule_cols(&a, pr, 1) == widths[i]);
        ASH_CHECK(rule_cols(&a, sr, 0) == widths[i]);
        ASH_CHECK(rule_cols(&a, sr, 1) == widths[i]);
    }

    ash_arena_destroy(&a);
}

static void test_history(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "h", 1u << 16) == ASH_OK);
    ash_history h;
    ash_history_init(&h, &a, 8);

    ash_history_push(&h, "one", 3);
    ash_history_push(&h, "two", 3);
    ash_history_push(&h, "three", 5);

    size_t len = 0;
    const char *p = ash_history_prev(&h, &len);
    ASH_CHECK(p && len == 5 && memcmp(p, "three", 5) == 0);
    p = ash_history_prev(&h, &len);
    ASH_CHECK(p && len == 3 && memcmp(p, "two", 3) == 0);
    p = ash_history_prev(&h, &len);
    ASH_CHECK(p && len == 3 && memcmp(p, "one", 3) == 0);
    p = ash_history_prev(&h, &len);
    ASH_CHECK(p == NULL);

    p = ash_history_next(&h, &len);
    ASH_CHECK(p && memcmp(p, "two", 3) == 0);
    p = ash_history_next(&h, &len);
    ASH_CHECK(p && memcmp(p, "three", 5) == 0);
    p = ash_history_next(&h, &len);
    ASH_CHECK(p == NULL);

    ash_arena_destroy(&a);
}

static void test_history_ring_evict(void)
{
    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "h", 1u << 16) == ASH_OK);
    ash_history h;
    ash_history_init(&h, &a, 2);

    ash_history_push(&h, "a", 1);
    ash_history_push(&h, "b", 1);
    ash_history_push(&h, "c", 1);

    size_t len = 0;
    const char *p = ash_history_prev(&h, &len);
    ASH_CHECK(p && memcmp(p, "c", 1) == 0);
    p = ash_history_prev(&h, &len);
    ASH_CHECK(p && memcmp(p, "b", 1) == 0);
    p = ash_history_prev(&h, &len);
    ASH_CHECK(p == NULL);

    ash_arena_destroy(&a);
}

int main(void)
{
    test_insert_cursor();
    test_backspace_delete();
    test_word_ops();
    test_kill();
    test_newline_paste_no_submit();
    test_selection();
    test_undo_redo();
    test_cut_selection();
    test_render_selection();
    test_wrap_rows();
    test_updown();
    test_input_rule();
    test_history();
    test_history_ring_evict();
    return ash_test_done();
}
