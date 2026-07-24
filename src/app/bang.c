#include "ash/app/bang.h"

int ash_bang_split(const char *line, size_t len, const char **cmd,
                   size_t *cmd_len)
{
    size_t i = 0;
    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;
    if (i >= len || line[i] != '!')
        return 0;
    i++;
    while (i < len && (line[i] == ' ' || line[i] == '\t'))
        i++;
    size_t end = len;
    while (end > i && (line[end - 1] == ' ' || line[end - 1] == '\t' ||
                       line[end - 1] == '\n' || line[end - 1] == '\r'))
        end--;
    *cmd = line + i;
    *cmd_len = end - i;
    return 1;
}
