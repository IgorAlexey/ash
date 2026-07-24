#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <unistd.h>

#include "ash/core/session.h"
#include "ash/base/poison.h"

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xffu);
    p[1] = (uint8_t)((v >> 8) & 0xffu);
}

static void wr32(uint8_t *p, uint32_t v)
{
    for (int i = 0; i < 4; i++)
        p[i] = (uint8_t)((v >> (i * 8)) & 0xffu);
}

static void wr64(uint8_t *p, uint64_t v)
{
    for (int i = 0; i < 8; i++)
        p[i] = (uint8_t)((v >> (i * 8)) & 0xffu);
}

static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (i * 8);
    return v;
}

static void ensure_spans(ash_log *g, size_t need)
{
    if (g->cap >= need)
        return;
    size_t nc = g->cap ? g->cap * 2 : 16;
    while (nc < need)
        nc *= 2;
    ash_span *ns = ash_array(g->arena, ash_span, nc);
    if (g->n)
        memcpy(ns, g->rec, g->n * sizeof(ash_span));
    g->rec = ns;
    g->cap = nc;
}

static void ensure_redo(ash_log *g, size_t need)
{
    if (g->redo_cap >= need)
        return;
    size_t nc = g->redo_cap ? g->redo_cap * 2 : 16;
    while (nc < need)
        nc *= 2;
    uint64_t *ns = ash_array(g->arena, uint64_t, nc);
    if (g->redo_n)
        memcpy(ns, g->redo, g->redo_n * sizeof(uint64_t));
    g->redo = ns;
    g->redo_cap = nc;
}

static ash_status append_record(ash_log *g, uint16_t type, uint16_t flags, uint64_t prev,
                                const void *payload, uint32_t plen, uint64_t *seq_out)
{
    if (plen && payload == NULL)
        return ash_fail(ASH_ERR_RANGE, "session log: null payload with len > 0");
    if (plen > ASH_MAX_PAYLOAD)
        return ash_fail(ASH_ERR_RANGE, "session log: payload exceeds ASH_MAX_PAYLOAD");

    uint8_t hdr[ASH_HDR_SIZE];
    wr32(hdr + 0, ASH_REC_MAGIC);
    wr16(hdr + 4, type);
    wr16(hdr + 6, flags);
    wr64(hdr + 8, g->next_seq);
    wr64(hdr + 16, prev);
    wr32(hdr + 24, plen);
    uint32_t crc = ash_crc32c_pair(hdr, ASH_HDR_SIZE - 4, payload, plen);
    wr32(hdr + 28, crc);

    struct iovec iov[2];
    iov[0].iov_base = hdr;
    iov[0].iov_len = ASH_HDR_SIZE;
    iov[1].iov_base = (void *)(uintptr_t)payload;
    iov[1].iov_len = plen;
    ssize_t want = (ssize_t)(ASH_HDR_SIZE + plen);
    ssize_t w = writev(g->fd, iov, plen ? 2 : 1);
    if (w != want)
        return ash_fail(ASH_ERR_IO, "session log: short write");
    if (fdatasync(g->fd) != 0)
        return ash_fail(ASH_ERR_IO, "session log: fdatasync failed");

    size_t off = g->image.len;
    ash_buf_append(&g->image, hdr, ASH_HDR_SIZE);
    if (plen)
        ash_buf_append(&g->image, payload, plen);
    ensure_spans(g, g->n + 1);
    g->rec[g->n] = (ash_span){ off, off + ASH_HDR_SIZE, plen, type, flags, g->next_seq, prev };
    if (seq_out)
        *seq_out = g->next_seq;
    g->n++;
    g->next_seq++;
    return ASH_OK;
}

static void replay(ash_log *g)
{
    g->tip = 0;
    g->redo_n = 0;
    for (size_t i = 0; i < g->n; i++) {
        ash_span *r = &g->rec[i];
        if (r->type == ASH_REC_HEAD) {
            uint64_t target = r->payload_len >= 8 ? rd64(g->image.data + r->payload_off) : 0;
            if (target >= (uint64_t)g->n)
                target = 0;
            if (r->flags == ASH_HEAD_UNDO) {
                ensure_redo(g, g->redo_n + 1);
                g->redo[g->redo_n++] = g->tip;
            } else if (g->redo_n > 0) {
                g->redo_n--;
            }
            g->tip = target;
        } else {
            g->tip = r->seq;
            g->redo_n = 0;
        }
    }
}

ash_status ash_log_open(ash_log *g, ash_arena *arena, const char *path)
{
    memset(g, 0, sizeof *g);
    g->arena = arena;
    g->fd = -1;
    ash_buf_init(&g->image, arena);

    int fd = open(path, O_RDWR | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0)
        return ash_fail(ASH_ERR_IO, "session log: open %s failed", path);
    g->fd = fd;

    struct stat st;
    if (fstat(fd, &st) != 0) {
        close(fd);
        g->fd = -1;
        return ash_fail(ASH_ERR_IO, "session log: fstat failed");
    }

    if (st.st_size > 0) {
        size_t sz = (size_t)st.st_size;
        ash_buf_reserve(&g->image, sz);
        size_t got = 0;
        while (got < sz) {
            ssize_t r = pread(fd, g->image.data + got, sz - got, (off_t)got);
            if (r < 0) {
                if (errno == EINTR)
                    continue;
                close(fd);
                g->fd = -1;
                return ash_fail(ASH_ERR_IO, "session log: read failed");
            }
            if (r == 0)
                break;
            got += (size_t)r;
        }
        g->image.len = got;
    }

    size_t est = g->image.len / ASH_HDR_SIZE + 1;
    ensure_spans(g, est);
    size_t n = 0, trunc = 0;
    ash_status ss = ash_log_scan(g->image.data, g->image.len, g->rec, g->cap, &n, &trunc);
    if (ss != ASH_OK) {
        close(fd);
        g->fd = -1;
        return ss;
    }
    g->n = n;
    g->next_seq = n;

    if (trunc < g->image.len) {
        if (ftruncate(fd, (off_t)trunc) != 0) {
            close(fd);
            g->fd = -1;
            return ash_fail(ASH_ERR_IO, "session log: truncate torn tail failed");
        }
        g->image.len = trunc;
    }

    if (g->n == 0) {
        uint8_t ver[4];
        wr32(ver, 1);
        ASH_TRY(append_record(g, ASH_REC_HEADER, 0, ASH_SEQ_NONE, ver, 4, NULL));
    }

    replay(g);
    return ASH_OK;
}

ash_status ash_log_append_turn(ash_log *g, const void *payload, uint32_t len)
{
    uint64_t seq = 0;
    ASH_TRY(append_record(g, ASH_REC_TURN, 0, g->tip, payload, len, &seq));
    g->tip = seq;
    g->redo_n = 0;
    return ASH_OK;
}

ash_status ash_log_compact(ash_log *g, uint64_t from, uint64_t to,
                           const char *summary, uint32_t slen)
{
    if (slen > ASH_MAX_PAYLOAD - 20u)
        return ash_fail(ASH_ERR_RANGE, "session log: compact summary too large");
    size_t plen = 20u + slen;
    uint8_t *pl = ash_array(g->arena, uint8_t, plen);
    wr64(pl + 0, from);
    wr64(pl + 8, to);
    wr32(pl + 16, slen);
    if (slen)
        memcpy(pl + 20, summary, slen);
    uint64_t seq = 0;
    ASH_TRY(append_record(g, ASH_REC_MARK, 0, g->tip, pl, (uint32_t)plen, &seq));
    g->tip = seq;
    g->redo_n = 0;
    return ASH_OK;
}

ash_status ash_log_clear(ash_log *g)
{
    return ash_log_compact(g, 0, g->next_seq, "", 0);
}

ash_status ash_log_undo(ash_log *g)
{
    ash_span *r = &g->rec[g->tip];
    if (r->prev == ASH_SEQ_NONE)
        return ASH_OK;
    uint64_t from = g->tip;
    uint64_t target = r->prev;
    uint8_t tgt[8];
    wr64(tgt, target);
    ASH_TRY(append_record(g, ASH_REC_HEAD, ASH_HEAD_UNDO, ASH_SEQ_NONE, tgt, 8, NULL));
    ensure_redo(g, g->redo_n + 1);
    g->redo[g->redo_n++] = from;
    g->tip = target;
    return ASH_OK;
}

ash_status ash_log_redo(ash_log *g)
{
    if (g->redo_n == 0)
        return ASH_OK;
    uint64_t target = g->redo[g->redo_n - 1];
    uint8_t tgt[8];
    wr64(tgt, target);
    ASH_TRY(append_record(g, ASH_REC_HEAD, ASH_HEAD_REDO, ASH_SEQ_NONE, tgt, 8, NULL));
    g->redo_n--;
    g->tip = target;
    return ASH_OK;
}

uint64_t ash_log_tip_seq(const ash_log *g)
{
    return g->tip;
}

void ash_log_close(ash_log *g)
{
    if (g->fd >= 0)
        close(g->fd);
    g->fd = -1;
}
