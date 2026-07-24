#ifndef ASH_APP_BANG_H
#define ASH_APP_BANG_H

#include <stddef.h>

int ash_bang_split(const char *line, size_t len, const char **cmd,
                   size_t *cmd_len);

#endif
