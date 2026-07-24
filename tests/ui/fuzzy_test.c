#include <string.h>

#include "ash/base/arena.h"
#include "ash/ui/fuzzy.h"
#include "ash_test.h"

static ash_arena a;

static int32_t score(const char *h, const char *n, ash_fuzzy_match *m)
{
    ash_arena_mark mk = ash_arena_mark_get(&a);
    int32_t s = ash_fuzzy_score(&a, h, strlen(h), n, strlen(n), 1, m);
    ash_arena_rewind(&a, mk);
    return s;
}

int main(void)
{
    ASH_CHECK(ash_arena_create(&a, "fuzzy-test", 1u << 20) == ASH_OK);
    ash_fuzzy_match m;

    ASH_CHECK(score("readme", "", &m) == 0 && m.npos == 0);
    ASH_CHECK(score("", "x", &m) == 0);
    ASH_CHECK(score("ab", "abc", &m) == 0);

    ASH_CHECK(score("readme", "readme", &m) > 0);
    ASH_CHECK(m.npos == 6);
    for (int i = 0; i < 6; i++)
        ASH_CHECK(m.positions[i] == i);

    ASH_CHECK(score("README", "readme", &m) > 0);
    ASH_CHECK(m.npos == 6);

    ASH_CHECK(score("far", "fb", &m) == 0);
    ASH_CHECK(score("foobar", "fb", &m) > 0);

    int32_t s = score("src/ui/tui.c", "tui", &m);
    ASH_CHECK(s > 0 && m.npos == 3);
    const char *h = "src/ui/tui.c";
    ASH_CHECK(h[m.positions[0]] == 't' && h[m.positions[1]] == 'u' &&
              h[m.positions[2]] == 'i');

    int32_t contig = score("application", "app", &m);
    int32_t split = score("a_p_p_z", "app", &m);
    ASH_CHECK(contig > 0 && split > 0 && contig > split);

    int32_t sep_hit = score("foo/bar", "b", &m);
    int32_t mid_hit = score("foobar", "b", &m);
    ASH_CHECK(sep_hit > mid_hit);

    ash_fuzzy_match ma, mb;
    int32_t prefix = score("tui.c", "tui", &ma);
    int32_t suffix = score("zztui", "tui", &mb);
    ASH_CHECK(prefix > 0 && suffix > 0 && prefix > suffix);

    ash_arena_destroy(&a);
    return ash_test_done();
}
