#include <assert.h>
#include <string.h>

#include "nav/nav_state_machine.h"

int main(void)
{
    assert(strcmp(nav_mode_to_string(NAV_MODE_BOOT), "BOOT") == 0);
    assert(strcmp(nav_mode_to_string(NAV_MODE_GNSS_OK), "GNSS_OK") == 0);
    assert(strcmp(nav_mode_to_string(NAV_MODE_DEMO_FORCED_DENIED), "DEMO_FORCED_DENIED") == 0);
    assert(nav_state_machine_select_mode(true, false, 0u) == NAV_MODE_GNSS_OK);
    assert(nav_state_machine_select_mode(false, false, 3u) == NAV_MODE_RADIO_NAV_OK);
    assert(nav_state_machine_select_mode(false, false, 2u) == NAV_MODE_NO_NAV_SOLUTION);
    assert(nav_state_machine_select_mode(true, true, 3u) == NAV_MODE_DEMO_FORCED_DENIED);
    return 0;
}
