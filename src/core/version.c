#include "ash/core/version.h"
#include "ash/ai/version.h"
#include "ash/base/poison.h"

const char *ash_core_version(void)
{
    return "0.0.0";
}

const char *ash_core_ai_version(void)
{
    return ash_ai_version();
}
