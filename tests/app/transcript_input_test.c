#include <string.h>

#include "ash/term/input.h"
#include "ash_test.h"

static int decode_one(const char *bytes, uint32_t n, ash_input_event *ev)
{
    ash_input in;
    ash_input_init(&in);
    ash_input_event evs[8];
    uint32_t consumed = 0, produced = 0;
    if (ash_input_feed(&in, (const uint8_t *)bytes, n, evs, 8, &consumed,
                       &produced) != ASH_OK)
        return 0;
    if (produced != 1)
        return 0;
    *ev = evs[0];
    return 1;
}

int main(void)
{
    ash_input_event ev;
    ASH_CHECK(decode_one("\x0f", 1, &ev));
    ASH_CHECK(ev.kind == ASH_EV_KEY);
    ASH_CHECK(ev.key == 'o');
    ASH_CHECK((ev.mods & ASH_MOD_CTRL) != 0);

    ASH_CHECK(decode_one("\x1b[111;5u", 8, &ev));
    ASH_CHECK(ev.kind == ASH_EV_KEY);
    ASH_CHECK(ev.key == 'o');
    ASH_CHECK((ev.mods & ASH_MOD_CTRL) != 0);

    return ash_test_done();
}
