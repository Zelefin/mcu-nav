#ifndef NAV_GNSS_H
#define NAV_GNSS_H

#include "nav/nav_types.h"

#ifdef __cplusplus
extern "C" {
#endif

bool nav_gnss_sample_is_usable(const nav_gnss_sample_t *sample);

#ifdef __cplusplus
}
#endif

#endif
