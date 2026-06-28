#include "nav/nav_state_machine.h"

const char *nav_mode_to_string(nav_mode_t mode)
{
    switch (mode) {
    case NAV_MODE_BOOT:
        return "BOOT";
    case NAV_MODE_GNSS_ACQUIRE:
        return "GNSS_ACQUIRE";
    case NAV_MODE_GNSS_OK:
        return "GNSS_OK";
    case NAV_MODE_GNSS_SUSPECT:
        return "GNSS_SUSPECT";
    case NAV_MODE_GPS_DENIED:
        return "GPS_DENIED";
    case NAV_MODE_RADIO_NAV_OK:
        return "RADIO_NAV_OK";
    case NAV_MODE_RADIO_NAV_DEGRADED:
        return "RADIO_NAV_DEGRADED";
    case NAV_MODE_NO_NAV_SOLUTION:
        return "NO_NAV_SOLUTION";
    case NAV_MODE_DEMO_FORCED_DENIED:
        return "DEMO_FORCED_DENIED";
    default:
        return "UNKNOWN";
    }
}

nav_mode_t nav_state_machine_select_mode(bool local_gnss_valid, bool forced_denied, uint8_t usable_anchor_count)
{
    if (forced_denied) {
        return usable_anchor_count >= NAV_TRILAT_ANCHOR_COUNT ? NAV_MODE_DEMO_FORCED_DENIED : NAV_MODE_GPS_DENIED;
    }
    if (local_gnss_valid) {
        return NAV_MODE_GNSS_OK;
    }
    if (usable_anchor_count >= NAV_TRILAT_ANCHOR_COUNT) {
        return NAV_MODE_RADIO_NAV_OK;
    }
    return NAV_MODE_NO_NAV_SOLUTION;
}
