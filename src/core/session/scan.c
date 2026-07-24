#include <string.h>

#include "ash/core/session.h"
#include "ash/base/poison.h"

static uint32_t crc32c_step(uint32_t crc, const uint8_t *b, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        crc ^= (uint32_t)b[i];
        for (int k = 0; k < 8; k++)
            crc = (crc >> 1) ^ (0x82f63b78u & (uint32_t)(-(int32_t)(crc & 1u)));
    }
    return crc;
}

uint32_t ash_crc32c_pair(const void *a, size_t na, const void *b, size_t nb)
{
    uint32_t crc = 0xffffffffu;
    crc = crc32c_step(crc, a, na);
    crc = crc32c_step(crc, b, nb);
    return crc ^ 0xffffffffu;
}

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | (uint16_t)((uint16_t)p[1] << 8));
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static uint64_t rd64(const uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 0; i < 8; i++)
        v |= (uint64_t)p[i] << (8 * i);
    return v;
}

static uint32_t rec_crc(const uint8_t *rec, uint32_t plen)
{
    return ash_crc32c_pair(rec, ASH_HDR_SIZE - 4, rec + ASH_HDR_SIZE, plen);
}

ash_status ash_log_scan(const uint8_t *buf, size_t len, ash_span *out, size_t cap,
                        size_t *n_out, size_t *trunc_out)
{
    if (n_out == NULL || trunc_out == NULL || out == NULL || (buf == NULL && len > 0))
        return ash_fail(ASH_ERR_RANGE, "ash_log_scan: bad arguments");

    size_t off = 0;
    size_t n = 0;
    uint64_t expect = 0;

    while (len - off >= ASH_HDR_SIZE) {
        const uint8_t *r = buf + off;
        if (rd32(r) != ASH_REC_MAGIC)
            break;
        uint16_t type = rd16(r + 4);
        uint16_t flags = rd16(r + 6);
        uint64_t seq = rd64(r + 8);
        uint64_t prev = rd64(r + 16);
        uint32_t plen = rd32(r + 24);
        uint32_t crc = rd32(r + 28);

        if (plen > ASH_MAX_PAYLOAD)
            break;
        if (len - off - ASH_HDR_SIZE < plen)
            break;
        if (rec_crc(r, plen) != crc)
            break;
        if (seq != expect)
            break;
        if (!(prev == ASH_SEQ_NONE || prev < seq))
            break;
        if (type > ASH_REC_HEAD)
            break;
        if (expect == 0 && type != ASH_REC_HEADER)
            break;

        if (n == cap)
            return ash_fail(ASH_ERR_NOSPACE, "ash_log_scan: span buffer full");

        out[n].off = off;
        out[n].payload_off = off + ASH_HDR_SIZE;
        out[n].payload_len = plen;
        out[n].type = type;
        out[n].flags = flags;
        out[n].seq = seq;
        out[n].prev = prev;
        n++;
        off += ASH_HDR_SIZE + plen;
        expect++;
    }

    *n_out = n;
    *trunc_out = off;
    return ASH_OK;
}
