#ifndef NAV_STATE_MACHINE_H
#define NAV_STATE_MACHINE_H

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

const char *nav_mode_to_string(nav_mode_t mode);
nav_mode_t nav_state_machine_select_mode(bool local_gnss_valid, bool forced_denied, uint8_t usable_anchor_count);

#ifdef __cplusplus
}
#endif

#endif
