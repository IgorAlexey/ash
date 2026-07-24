#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ash/core/session.h"
#include "ash/base/arena.h"
#include "ash_test.h"

static long fsize(const char *path)
{
    struct stat s;
    return stat(path, &s) == 0 ? (long)s.st_size : -1;
}

static size_t mk_record(unsigned char *b, uint16_t type, uint16_t flags,
                        uint64_t seq, uint64_t prev,
                        const unsigned char *payload, uint32_t plen)
{
    b[0] = 0x54;
    b[1] = 0x41;
    b[2] = 0x55;
    b[3] = 0x52;
    b[4] = (unsigned char)(type & 0xff);
    b[5] = (unsigned char)(type >> 8);
    b[6] = (unsigned char)(flags & 0xff);
    b[7] = (unsigned char)(flags >> 8);
    for (int i = 0; i < 8; i++)
        b[8 + i] = (unsigned char)(seq >> (i * 8));
    for (int i = 0; i < 8; i++)
        b[16 + i] = (unsigned char)(prev >> (i * 8));
    for (int i = 0; i < 4; i++)
        b[24 + i] = (unsigned char)(plen >> (i * 8));
    uint32_t crc = ash_crc32c_pair(b, 28, payload, plen);
    for (int i = 0; i < 4; i++)
        b[28 + i] = (unsigned char)(crc >> (i * 8));
    if (plen)
        memcpy(b + 32, payload, plen);
    return 32u + plen;
}

int main(void)
{
    char path[] = "/tmp/ash_sess_XXXXXX";
    int tfd = mkstemp(path);
    ASH_CHECK(tfd >= 0);
    close(tfd);

    ash_arena a;
    ASH_CHECK(ash_arena_create(&a, "sess", 1u << 16) == ASH_OK);

    ash_log g;
    ASH_CHECK(ash_log_open(&g, &a, path) == ASH_OK);
    ASH_CHECK(ash_log_tip_seq(&g) == 0);
    long sz0 = fsize(path);

    ASH_CHECK(ash_log_append_turn(&g, "turn0", 5) == ASH_OK);
    ASH_CHECK(ash_log_append_turn(&g, "turn1", 5) == ASH_OK);
    ASH_CHECK(ash_log_append_turn(&g, "turn2", 5) == ASH_OK);
    ASH_CHECK(ash_log_tip_seq(&g) == 3);
    long sz1 = fsize(path);
    ASH_CHECK(sz1 > sz0);

    ASH_CHECK(ash_log_append_turn(&g, NULL, 5) == ASH_ERR_RANGE);
    ASH_CHECK(ash_log_append_turn(&g, "x", ASH_MAX_PAYLOAD + 1) == ASH_ERR_RANGE);
    ASH_CHECK(ash_log_tip_seq(&g) == 3);
    ASH_CHECK(fsize(path) == sz1);

    ASH_CHECK(ash_log_undo(&g) == ASH_OK);
    ASH_CHECK(ash_log_tip_seq(&g) == 2);
    ASH_CHECK(ash_log_undo(&g) == ASH_OK);
    ASH_CHECK(ash_log_tip_seq(&g) == 1);
    long sz2 = fsize(path);
    ASH_CHECK(sz2 >= sz1);

    ASH_CHECK(ash_log_redo(&g) == ASH_OK);
    ASH_CHECK(ash_log_tip_seq(&g) == 2);

    ASH_CHECK(ash_log_append_turn(&g, "turn3", 5) == ASH_OK);
    uint64_t forked = ash_log_tip_seq(&g);
    ASH_CHECK(ash_log_redo(&g) == ASH_OK);
    ASH_CHECK(ash_log_tip_seq(&g) == forked);

    ASH_CHECK(ash_log_compact(&g, 1, 3, "folded", 6) == ASH_OK);
    long sz4 = fsize(path);
    ASH_CHECK(sz4 > sz2);
    uint64_t tip_after = ash_log_tip_seq(&g);
    ash_log_close(&g);

    ash_log g2;
    ASH_CHECK(ash_log_open(&g2, &a, path) == ASH_OK);
    ASH_CHECK(ash_log_tip_seq(&g2) == tip_after);
    ASH_CHECK(fsize(path) == sz4);
    ash_log_close(&g2);

    static const unsigned char sub_header[4] = { 0x54, 0x41, 0x55, 0x52 };
    static const unsigned char mid_header[10] = { 0x54, 0x41, 0x55, 0x52, 1, 0, 0, 0, 0, 0 };
    static const unsigned char mid_payload[40] = {
        0x54, 0x41, 0x55, 0x52,
        1, 0, 0, 0,
        9, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0,
        0, 0x10, 0, 0,
        0, 0, 0, 0,
        1, 2, 3, 4, 5, 6, 7, 8
    };
    struct torn { const unsigned char *b; size_t n; };
    const struct torn torns[3] = {
        { sub_header, sizeof sub_header },
        { mid_header, sizeof mid_header },
        { mid_payload, sizeof mid_payload }
    };
    for (size_t ti = 0; ti < 3; ti++) {
        int fd = open(path, O_WRONLY | O_APPEND);
        ASH_CHECK(fd >= 0);
        ASH_CHECK(write(fd, torns[ti].b, torns[ti].n) == (ssize_t)torns[ti].n);
        close(fd);
        ASH_CHECK(fsize(path) == sz4 + (long)torns[ti].n);

        ash_log gt;
        ASH_CHECK(ash_log_open(&gt, &a, path) == ASH_OK);
        ASH_CHECK(fsize(path) == sz4);
        ASH_CHECK(ash_log_tip_seq(&gt) == tip_after);
        ash_log_close(&gt);
    }

    char fp[] = "/tmp/ash_forge_XXXXXX";
    int ff = mkstemp(fp);
    ASH_CHECK(ff >= 0);
    unsigned char rec[128];
    size_t roff = 0;
    unsigned char ver[4] = { 1, 0, 0, 0 };
    roff += mk_record(rec + roff, 0, 0, 0, ~(uint64_t)0, ver, 4);
    unsigned char tgt[8] = { 0xff, 0xff, 0xff, 0xff, 0, 0, 0, 0 };
    roff += mk_record(rec + roff, 3, 0, 1, ~(uint64_t)0, tgt, 8);
    ASH_CHECK(write(ff, rec, roff) == (ssize_t)roff);
    close(ff);

    ash_log gf;
    ASH_CHECK(ash_log_open(&gf, &a, fp) == ASH_OK);
    ASH_CHECK(ash_log_tip_seq(&gf) < 2);
    ASH_CHECK(ash_log_undo(&gf) == ASH_OK);
    ash_log_close(&gf);
    unlink(fp);

    ash_arena_destroy(&a);
    unlink(path);
    return ash_test_done();
}
