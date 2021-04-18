//
// Created by weijing on 18-8-3.
//

#ifndef SKY_SKY_DATE_H
#define SKY_SKY_DATE_H

#include <time.h>
#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * 用于时分秒
 * @param secs 秒 secs <= 86400
 * @param out 输出的字符串
 * @return 字符长度 一直为8, 0代表 secs > 86400
 */
sky_uint8_t sky_time_to_str(sky_uint32_t secs, sky_uchar_t* out);

sky_bool_t sky_rfc_str_to_date(sky_str_t* in, time_t* out);

sky_uint8_t sky_date_to_rfc_str(time_t time, sky_uchar_t* src);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_SKY_DATE_H
