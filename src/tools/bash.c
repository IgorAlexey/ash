#include <string.h>

#include "ash/base/json.h"
#include "ash/base/slice.h"
#include "ash/tools/tools.h"

ash_status ash_bash_command(ash_arena *a, const char *input, size_t len,
                            const char **cmd)
{
    *cmd = NULL;
    if (input == NULL)
        return ash_fail(ASH_ERR_PARSE, "bash: input has no command");
    ash_json v;
    if (ash_json_parse(a, input, len, &v) != ASH_OK)
        return ash_fail(ASH_ERR_PARSE, "bash: input is not valid JSON");
    const ash_json *c = ash_json_get(&v, "command");
    ash_slice cs;
    if (c == NULL || ash_json_str(c, &cs) != ASH_OK)
        return ash_fail(ASH_ERR_PARSE, "bash: input has no command");
    char *buf = ash_array(a, char, cs.len + 1);
    memcpy(buf, cs.p, cs.len);
    buf[cs.len] = 0;
    *cmd = buf;
    return ASH_OK;
}
