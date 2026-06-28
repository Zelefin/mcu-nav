#include "nav/nav_gnss.h"

#include <stddef.h>

bool nav_gnss_sample_is_usable(const nav_gnss_sample_t *sample)
{
    return sample != NULL && sample->valid && sample->fix_type >= NAV_GNSS_FIX_3D;
}
