//
// Created by weijing on 18-8-3.
//

#include "date.h"
#include "memory.h"
#include "number.h"

sky_bool_t
sky_rfc_str_to_date(sky_str_t *in, time_t *out) {
    struct tm tm;
    sky_uchar_t *value;
    sky_str_t tmp;

    if (sky_unlikely(in->len != 29)) {
        return false;
    }
    value = in->data;

    switch (sky_str4_switch(value)) {
        case sky_str4_num('S', 'u', 'n', ','):
            tm.tm_wday = 0;
            break;
        case sky_str4_num('M', 'o', 'n', ','):
            tm.tm_wday = 1;
            break;
        case sky_str4_num('T', 'u', 'e', ','):
            tm.tm_wday = 2;
            break;
        case sky_str4_num('W', 'e', 'd', ','):
            tm.tm_wday = 3;
            break;
        case sky_str4_num('T', 'h', 'u', ','):
            tm.tm_wday = 4;
            break;
        case sky_str4_num('F', 'r', 'i', ','):
            tm.tm_wday = 5;
            break;
        case sky_str4_num('S', 'a', 't', ','):
            tm.tm_wday = 6;
            break;
        default:
            return false;
    }
    value += 5;

    tmp.data = value;
    tmp.len = 2;
    sky_str_to_uint8(&tmp, (sky_uint8_t *) &tm.tm_mday);
    if (tm.tm_mday < 1 || tm.tm_mday > 31) {
        return false;
    }
    value += 3;
    switch (sky_str4_switch(value)) {
        case sky_str4_num('J', 'a', 'n', ' '):
            tm.tm_mon = 0;
            break;
        case sky_str4_num('F', 'e', 'b', ' '):
            tm.tm_mon = 1;
            break;
        case sky_str4_num('M', 'a', 'r', ' '):
            tm.tm_mon = 2;
            break;
        case sky_str4_num('A', 'p', 'r', ' '):
            tm.tm_mon = 3;
            break;
        case sky_str4_num('M', 'a', 'y', ' '):
            tm.tm_mon = 4;
            break;
        case sky_str4_num('J', 'u', 'n', ' '):
            tm.tm_mon = 5;
            break;
        case sky_str4_num('J', 'u', 'l', ' '):
            tm.tm_mon = 6;
            break;
        case sky_str4_num('A', 'u', 'g', ' '):
            tm.tm_mon = 7;
            break;
        case sky_str4_num('S', 'e', 'p', ' '):
            tm.tm_mon = 8;
            break;
        case sky_str4_num('O', 'c', 't', ' '):
            tm.tm_mon = 9;
            break;
        case sky_str4_num('N', 'o', 'v', ' '):
            tm.tm_mon = 10;
            break;
        case sky_str4_num('D', 'e', 'c', ' '):
            tm.tm_mon = 11;
            break;
        default:
            return false;
    }
    value += 4;

    tmp.data = value;
    tmp.len = 4;
    sky_str_to_uint16(&tmp, (sky_uint16_t *) &tm.tm_year);
    if (tm.tm_year < 0 || tm.tm_year > 9999) {
        return false;
    }
    tm.tm_year -= 1900;
    value += 5;

    tmp.data = value;
    tmp.len = 2;
    sky_str_to_uint8(&tmp, (sky_uint8_t *) &tm.tm_hour);
    if (tm.tm_hour < 0 || tm.tm_hour > 24) {
        return false;
    }
    value += 3;

    tmp.data = value;
    sky_str_to_uint8(&tmp, (sky_uint8_t *) &tm.tm_min);
    if (tm.tm_min < 0 || tm.tm_min > 60) {
        return false;
    }
    value += 3;

    tmp.data = value;
    sky_str_to_uint8(&tmp, (sky_uint8_t *) &tm.tm_sec);
    if (tm.tm_sec < 0 || tm.tm_sec > 60) {
        return false;
    }
    value += 3;
    if (sky_unlikely(!sky_str3_cmp(value, 'G', 'M', 'T'))) {
        return false;
    }

    tm.tm_isdst = -1;
    *out = timegm(&tm);

    return true;

}


sky_uint8_t
sky_date_to_rfc_str(time_t time, sky_uchar_t *src) {
    static const sky_char_t *week_days = "Sun,Mon,Tue,Wed,Thu,Fri,Sat,";
    static const sky_char_t *months = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec ";
    struct tm tm;


    if (sky_unlikely(!gmtime_r(&time, &tm))) {
        return 0;
    }
    sky_memcpy(src, week_days + (tm.tm_wday << 2), 4);
    src += 4;
    *(src++) = ' ';
    if (tm.tm_mday < 10) {
        *(src++) = '0';
        *(src++) = sky_num_to_uchar(tm.tm_mday);
    } else {
        src += sky_uint8_to_str((sky_uint8_t) tm.tm_mday, src);
    }
    *(src++) = ' ';
    sky_memcpy(src, months + (tm.tm_mon << 2), 4);
    src += 4;

    src += sky_uint16_to_str((sky_uint16_t) tm.tm_year + 1900, src);
    *(src++) = ' ';
    if (tm.tm_hour < 10) {
        *(src++) = '0';
        *(src++) = sky_num_to_uchar(tm.tm_hour);
    } else {
        src += sky_uint8_to_str((sky_uint8_t) tm.tm_hour, src);
    }
    *(src++) = ':';
    if (tm.tm_min < 10) {
        *(src++) = '0';
        *(src++) = sky_num_to_uchar(tm.tm_min);
    } else {
        src += sky_uint8_to_str((sky_uint8_t) tm.tm_min, src);
    }
    *(src++) = ':';
    if (tm.tm_sec < 10) {
        *(src++) = '0';
        *(src++) = sky_num_to_uchar(tm.tm_sec);
    } else {
        src += sky_uint8_to_str((sky_uint8_t) tm.tm_sec, src);
    }
    sky_memcpy(src, " GMT\0", 5);

    return 29;
}
