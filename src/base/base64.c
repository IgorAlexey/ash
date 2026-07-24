#include "ash/base/base64.h"
#include "ash/base/poison.h"

static const char B64URL[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t ash_base64url_encode(const uint8_t *data, size_t len, char *out,
                            size_t out_cap)
{
    size_t rem = len % 3;
    size_t need = len / 3 * 4 + (rem ? rem + 1 : 0);
    if (out == NULL || out_cap <= need)
        return 0;

    size_t o = 0, i = 0;
    for (; i + 3 <= len; i += 3) {
        uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8 |
                     (uint32_t)data[i + 2];
        out[o++] = B64URL[v >> 18 & 63];
        out[o++] = B64URL[v >> 12 & 63];
        out[o++] = B64URL[v >> 6 & 63];
        out[o++] = B64URL[v & 63];
    }
    if (rem == 1) {
        uint32_t v = (uint32_t)data[i] << 16;
        out[o++] = B64URL[v >> 18 & 63];
        out[o++] = B64URL[v >> 12 & 63];
    } else if (rem == 2) {
        uint32_t v = (uint32_t)data[i] << 16 | (uint32_t)data[i + 1] << 8;
        out[o++] = B64URL[v >> 18 & 63];
        out[o++] = B64URL[v >> 12 & 63];
        out[o++] = B64URL[v >> 6 & 63];
    }
    out[o] = '\0';
    return o;
}
