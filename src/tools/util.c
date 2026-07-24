#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ash/base/slice.h"
#include "internal.h"

int tools_arg_str(const ash_json *args, const char *key, ash_arena *a,
                  const char **out, size_t *len)
{
    *out = NULL;
    if (len)
        *len = 0;
    const ash_json *v = ash_json_get(args, key);
    ash_slice s;
    if (v == NULL || ash_json_str(v, &s) != ASH_OK)
        return 0;
    char *buf = ash_array(a, char, s.len + 1);
    memcpy(buf, s.p, s.len);
    buf[s.len] = 0;
    *out = buf;
    if (len)
        *len = s.len;
    return 1;
}

int tools_arg_int(const ash_json *args, const char *key, int64_t *out)
{
    const ash_json *v = ash_json_get(args, key);
    if (v == NULL || ash_json_int64(v, out) != ASH_OK)
        return 0;
    return 1;
}

void tools_result(ash_tool_result *res, ash_buf *b, int is_error)
{
    ash_buf_append_byte(b, 0);
    b->len--;
    res->content = (const char *)b->data;
    res->len = b->len;
    res->is_error = is_error;
}

void tools_error(ash_arena *a, ash_tool_result *res, const char *msg)
{
    ash_buf b;
    ash_buf_init(&b, a);
    ash_buf_append_cstr(&b, msg);
    tools_result(res, &b, 1);
}

void tools_append_size(ash_buf *b, size_t bytes)
{
    char tmp[32];
    int n;
    if (bytes < 1024)
        n = snprintf(tmp, sizeof tmp, "%zuB", bytes);
    else if (bytes < 1024u * 1024u)
        n = snprintf(tmp, sizeof tmp, "%.1fKB", (double)bytes / 1024.0);
    else
        n = snprintf(tmp, sizeof tmp, "%.1fMB", (double)bytes / (1024.0 * 1024.0));
    if (n > 0)
        ash_buf_append(b, tmp, (size_t)n);
}

ash_status tools_read_file(ash_arena *a, const char *path, ash_buf *out,
                           int *truncated)
{
    if (truncated)
        *truncated = 0;
    int fd;
    do {
        fd = open(path, O_RDONLY | O_CLOEXEC);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return ash_fail(ASH_ERR_IO, "%s", strerror(errno));

    struct stat st;
    if (fstat(fd, &st) == 0 && S_ISDIR(st.st_mode)) {
        close(fd);
        return ash_fail(ASH_ERR_IO, "is a directory");
    }

    ash_buf_init(out, a);
    uint8_t chunk[65536];
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return ash_fail(ASH_ERR_IO, "%s", strerror(errno));
        }
        if (n == 0)
            break;
        size_t room = TOOLS_FILE_CAP > out->len ? TOOLS_FILE_CAP - out->len : 0;
        size_t take = (size_t)n < room ? (size_t)n : room;
        if (take)
            ash_buf_append(out, chunk, take);
        if ((size_t)n > take) {
            if (truncated)
                *truncated = 1;
            break;
        }
    }
    close(fd);
    return ASH_OK;
}

static ash_status mkparents(const char *path)
{
    size_t n = strlen(path);
    char tmp[4096];
    for (size_t i = 1; i < n; i++) {
        if (path[i] != '/')
            continue;
        if (i >= sizeof tmp)
            return ash_fail(ASH_ERR_RANGE, "path too long");
        memcpy(tmp, path, i);
        tmp[i] = 0;
        if (mkdir(tmp, 0777) != 0 && errno != EEXIST)
            return ash_fail(ASH_ERR_IO, "%s: %s", tmp, strerror(errno));
    }
    return ASH_OK;
}

ash_status tools_write_file(const char *path, const void *data, size_t len)
{
    ASH_TRY(mkparents(path));
    int fd;
    do {
        fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    } while (fd < 0 && errno == EINTR);
    if (fd < 0)
        return ash_fail(ASH_ERR_IO, "%s", strerror(errno));

    const uint8_t *p = data;
    size_t off = 0;
    while (off < len) {
        ssize_t w = write(fd, p + off, len - off);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            close(fd);
            return ash_fail(ASH_ERR_IO, "%s", strerror(errno));
        }
        off += (size_t)w;
    }
    close(fd);
    return ASH_OK;
}
