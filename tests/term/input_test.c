#include <stdio.h>
#include <string.h>

#include "ash/term/input.h"
#include "ash_test.h"

enum { MAXEV = 4096, MAXBYTES = 16384 };

typedef struct col {
    ash_ev_kind kind[MAXEV];
    uint32_t    key[MAXEV];
    uint32_t    mods[MAXEV];
    int16_t     mx[MAXEV];
    int16_t     my[MAXEV];
    uint8_t     mbutton[MAXEV];
    uint8_t     maction[MAXEV];
    int         n;
    char        text[MAXBYTES];
    size_t      tlen;
} col;

static void run(const char *in, uint32_t n, uint32_t chunk, col *c)
{
    ash_input parser;
    ash_input_init(&parser);
    c->n = 0;
    c->tlen = 0;
    ash_input_event out[512];
    for (uint32_t off = 0; off < n; off += chunk) {
        uint32_t m = chunk < n - off ? chunk : n - off;
        uint32_t fed = 0;
        while (fed < m) {
            uint32_t consumed = 0, produced = 0;
            ASH_IGNORE(ash_input_feed(&parser, (const uint8_t *)in + off + fed, m - fed,
                                      out, 512, &consumed, &produced));
            for (uint32_t k = 0; k < produced; k++) {
                if (c->n < MAXEV) {
                    c->kind[c->n] = out[k].kind;
                    c->key[c->n] = out[k].key;
                    c->mods[c->n] = out[k].mods;
                    c->mx[c->n] = out[k].mx;
                    c->my[c->n] = out[k].my;
                    c->mbutton[c->n] = out[k].mbutton;
                    c->maction[c->n] = out[k].maction;
                    c->n++;
                }
                if ((out[k].kind == ASH_EV_TEXT || out[k].kind == ASH_EV_PASTE_CHUNK) &&
                    out[k].len && c->tlen + out[k].len <= MAXBYTES) {
                    memcpy(c->text + c->tlen, out[k].text, out[k].len);
                    c->tlen += out[k].len;
                }
            }
            if (consumed == 0 && produced == 0)
                break;
            fed += consumed;
        }
    }
}

int main(void)
{
    char paste[4096];
    size_t pl = 0;
    memcpy(paste, "\033[200~", 6);
    pl = 6;
    size_t cstart = pl;
    for (int i = 0; i < 40; i++)
        pl += (size_t)snprintf(paste + pl, sizeof paste - pl, "line%d\n", i);
    size_t cend = pl;
    memcpy(paste + pl, "\033[201~", 6);
    pl += 6;

    col c;
    run(paste, (uint32_t)pl, (uint32_t)pl, &c);
    int begins = 0, ends = 0, keys = 0, nl = 0;
    for (int k = 0; k < c.n; k++) {
        begins += c.kind[k] == ASH_EV_PASTE_BEGIN;
        ends += c.kind[k] == ASH_EV_PASTE_END;
        keys += c.kind[k] == ASH_EV_KEY;
    }
    for (size_t k = 0; k < c.tlen; k++)
        nl += c.text[k] == '\n';
    ASH_CHECK(begins == 1);
    ASH_CHECK(ends == 1);
    ASH_CHECK(keys == 0);
    ASH_CHECK(nl == 40);
    ASH_CHECK(c.tlen == cend - cstart);
    ASH_CHECK(memcmp(c.text, paste + cstart, c.tlen) == 0);

    col e;
    run("\033[13u", 5, 5, &e);
    ASH_CHECK(e.n == 1 && e.kind[0] == ASH_EV_KEY && e.key[0] == 13 && e.mods[0] == 0);
    run("\033[13;2u", 7, 7, &e);
    ASH_CHECK(e.n == 1 && e.key[0] == 13 && e.mods[0] == ASH_MOD_SHIFT);
    run("\033[13;5u", 7, 7, &e);
    ASH_CHECK(e.n == 1 && e.key[0] == 13 && e.mods[0] == ASH_MOD_CTRL);

    col split;
    run("\033[13;2u", 7, 1, &split);
    ASH_CHECK(split.n == 1 && split.key[0] == 13 && split.mods[0] == ASH_MOD_SHIFT);

    col cr;
    run("\r", 1, 1, &cr);
    ASH_CHECK(cr.n == 1 && cr.key[0] == 13 && cr.mods[0] == 0);

    const char *fmk = "\033[200~has \033[201 bad\033[201~";
    const char *expect = "has \033[201 bad";
    col fw, fb;
    run(fmk, 25, 25, &fw);
    run(fmk, 25, 1, &fb);
    for (int pass = 0; pass < 2; pass++) {
        col *f = pass ? &fb : &fw;
        int b1 = 0, e1 = 0, k1 = 0;
        for (int k = 0; k < f->n; k++) {
            b1 += f->kind[k] == ASH_EV_PASTE_BEGIN;
            e1 += f->kind[k] == ASH_EV_PASTE_END;
            k1 += f->kind[k] == ASH_EV_KEY;
        }
        ASH_CHECK(b1 == 1 && e1 == 1 && k1 == 0);
        ASH_CHECK(f->tlen == 13);
        ASH_CHECK(memcmp(f->text, expect, 13) == 0);
    }

    col m;
    run("\033[<0;12;5M", 10, 10, &m);
    ASH_CHECK(m.n == 1 && m.kind[0] == ASH_EV_MOUSE);
    ASH_CHECK(m.mbutton[0] == ASH_MB_LEFT && m.maction[0] == ASH_MOUSE_PRESS);
    ASH_CHECK(m.mx[0] == 11 && m.my[0] == 4 && m.mods[0] == 0);

    run("\033[<0;12;5m", 10, 10, &m);
    ASH_CHECK(m.n == 1 && m.maction[0] == ASH_MOUSE_RELEASE && m.mbutton[0] == ASH_MB_LEFT);

    run("\033[<2;1;1M", 9, 9, &m);
    ASH_CHECK(m.n == 1 && m.mbutton[0] == ASH_MB_RIGHT && m.mx[0] == 0 && m.my[0] == 0);

    run("\033[<32;7;9M", 10, 10, &m);
    ASH_CHECK(m.n == 1 && m.maction[0] == ASH_MOUSE_DRAG && m.mbutton[0] == ASH_MB_LEFT);
    ASH_CHECK(m.mx[0] == 6 && m.my[0] == 8);

    run("\033[<64;3;4M", 10, 10, &m);
    ASH_CHECK(m.n == 1 && m.maction[0] == ASH_MOUSE_WHEEL_UP && m.mbutton[0] == ASH_MB_NONE);

    run("\033[<65;3;4M", 10, 10, &m);
    ASH_CHECK(m.n == 1 && m.maction[0] == ASH_MOUSE_WHEEL_DOWN);

    run("\033[<20;40;10M", 12, 10, &m);
    ASH_CHECK(m.n == 1 && m.mbutton[0] == ASH_MB_LEFT && (m.mods[0] & ASH_MOD_CTRL));
    ASH_CHECK(m.mx[0] == 39 && m.my[0] == 9);

    col msplit;
    run("\033[<0;12;5M", 10, 1, &msplit);
    ASH_CHECK(msplit.n == 1 && msplit.kind[0] == ASH_EV_MOUSE);
    ASH_CHECK(msplit.mx[0] == 11 && msplit.my[0] == 4 &&
              msplit.maction[0] == ASH_MOUSE_PRESS);

    col csu;
    run("\033[99;6u", 7, 7, &csu);
    ASH_CHECK(csu.n == 1 && csu.kind[0] == ASH_EV_KEY && csu.key[0] == 'c');
    ASH_CHECK((csu.mods[0] & ASH_MOD_CTRL) && (csu.mods[0] & ASH_MOD_SHIFT));

    run("\033[99;6u", 7, 1, &csu);
    ASH_CHECK(csu.n == 1 && csu.key[0] == 'c' &&
              (csu.mods[0] & ASH_MOD_CTRL) && (csu.mods[0] & ASH_MOD_SHIFT));

    col mok;
    run("\033[27;6;99~", 10, 10, &mok);
    ASH_CHECK(mok.n == 1 && mok.kind[0] == ASH_EV_KEY && mok.key[0] == 99);
    ASH_CHECK((mok.mods[0] & ASH_MOD_CTRL) && (mok.mods[0] & ASH_MOD_SHIFT));

    run("\033[27;6;99~", 10, 1, &mok);
    ASH_CHECK(mok.n == 1 && mok.key[0] == 99 &&
              (mok.mods[0] & ASH_MOD_CTRL) && (mok.mods[0] & ASH_MOD_SHIFT));

    col esc;
    run("\033[27u", 5, 5, &esc);
    ASH_CHECK(esc.n == 1 && esc.kind[0] == ASH_EV_KEY && esc.key[0] == 27);

    col ldrag;
    run("\033[<32;10;3M", 11, 11, &ldrag);
    ASH_CHECK(ldrag.n == 1 && ldrag.maction[0] == ASH_MOUSE_DRAG &&
              ldrag.mbutton[0] == ASH_MB_LEFT && ldrag.mx[0] == 9 && ldrag.my[0] == 2);

    col pg;
    run("\033[5~\033[6~", 8, 8, &pg);
    ASH_CHECK(pg.n == 2);
    ASH_CHECK(pg.kind[0] == ASH_EV_KEY && pg.key[0] == ASH_KEY_PGUP && pg.mods[0] == 0);
    ASH_CHECK(pg.kind[1] == ASH_EV_KEY && pg.key[1] == ASH_KEY_PGDN && pg.mods[1] == 0);

    col pg1;
    run("\033[5~\033[6~", 8, 1, &pg1);
    ASH_CHECK(pg1.n == 2);
    ASH_CHECK(pg1.kind[0] == ASH_EV_KEY && pg1.key[0] == ASH_KEY_PGUP);
    ASH_CHECK(pg1.kind[1] == ASH_EV_KEY && pg1.key[1] == ASH_KEY_PGDN);

    col pgm;
    run("\033[5;2~", 6, 6, &pgm);
    ASH_CHECK(pgm.n == 1 && pgm.kind[0] == ASH_EV_KEY && pgm.key[0] == ASH_KEY_PGUP &&
              (pgm.mods[0] & ASH_MOD_SHIFT));

    col t;
    run("ab\003c", 4, 4, &t);
    ASH_CHECK(t.n == 3);
    ASH_CHECK(t.kind[0] == ASH_EV_TEXT);
    ASH_CHECK(t.kind[1] == ASH_EV_KEY && t.key[1] == 'c' && t.mods[1] == ASH_MOD_CTRL);
    ASH_CHECK(t.kind[2] == ASH_EV_TEXT);
    ASH_CHECK(t.tlen == 3 && memcmp(t.text, "abc", 3) == 0);

    return ash_test_done();
}
