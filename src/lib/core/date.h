//
// Created by weijing on 18-8-3.
//

#ifndef SKY_SKY_DATE_H
#define SKY_SKY_DATE_H

#include <time.h>
#include "string.h"

sky_bool_t sky_rfc_str_to_date(sky_str_t *in, time_t *out);
sky_uint8_t sky_date_to_rfc_str(time_t time, sky_uchar_t *src);
#endif //SKY_SKY_DATE_H
