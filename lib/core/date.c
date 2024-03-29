//
// Created by beliefsky on 18-8-3.
//

#include <core/date.h>
#include <core/memory.h>
#include "number/number_common.h"


sky_api sky_u8_t
sky_time_to_str(const sky_u32_t secs, sky_uchar_t *const out) {
    if (sky_unlikely(secs > 86400)) {
        return 0;
    }
    // divide by 3600 to calculate hours
    const sky_u64_t hours = (secs * 0x91A3) >> 27;
    sky_u64_t xrem = secs - (hours * 3600);

    // divide by 60 to calculate minutes
    const sky_u64_t mins = (xrem * 0x889) >> 17;
    xrem = xrem - (mins * 60);

    // position hours, minutes, and seconds in one var
    sky_u64_t timeBuffer = hours + (mins << 24) + (xrem << 48);

    // convert to decimal representation
    xrem = ((timeBuffer * 103) >> 9) & 0x001E00001E00001E;
    timeBuffer += xrem * 3;

    // move high nibbles into low mibble position in current byte
    // move lower nibble into left-side byte
    timeBuffer = ((timeBuffer & 0x00F00000F00000F0) >> 4) |
                 ((timeBuffer & 0x000F00000F00000F) << 8);

    // bitwise-OR in colons and convert numbers into ASCII number characters
    timeBuffer |= 0x30303A30303A3030;

    // copy to buffer
    *(sky_u64_t *) out = timeBuffer;
    out[8] = '\0';

    return 8;
}


sky_api sky_bool_t
sky_rfc_str_to_date(const sky_str_t *const in, time_t *const out) {
    struct tm tm;

    if (sky_unlikely(in->len != 29)) {
        return false;
    }
    sky_uchar_t *value = in->data;

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


    if (sky_unlikely(!sky_str_len_to_i32(value, 2, &tm.tm_mday) || tm.tm_mday < 1 || tm.tm_mday > 31)) {
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
    if (sky_unlikely(!sky_str_len_to_i32(value, 4, &tm.tm_year) || tm.tm_year < 0 || tm.tm_year > 9999)) {
        return false;
    }
    tm.tm_year -= 1900;
    value += 5;

    if (sky_unlikely(!sky_str_len_to_i32(value, 2, &tm.tm_hour) || tm.tm_hour < 0 || tm.tm_hour > 24)) {
        return false;
    }
    value += 3;
    if (sky_unlikely(!sky_str_len_to_i32(value, 2, &tm.tm_min) || tm.tm_min < 0 || tm.tm_min > 60)) {
        return false;
    }
    value += 3;
    if (sky_unlikely(!sky_str_len_to_i32(value, 2, &tm.tm_sec) || tm.tm_sec < 0 || tm.tm_sec > 60)) {
        return false;
    }
    value += 3;
    if (sky_unlikely(!sky_str2_cmp(value, 'G', 'M') || value[2] != 'T')) {
        return false;
    }

    tm.tm_isdst = -1;
    *out = timegm(&tm);

    return true;

}


sky_api sky_u8_t
sky_date_to_rfc_str(time_t time, sky_uchar_t *src) {
    static const sky_char_t week_days[] = "Sun,Mon,Tue,Wed,Thu,Fri,Sat,";
    static const sky_char_t months[] = "Jan Feb Mar Apr May Jun Jul Aug Sep Oct Nov Dec ";
    struct tm tm;

    const sky_u32_t day_of_time = (sky_u32_t) (time % 86400);
    if (sky_unlikely(!gmtime_r(&time, &tm))) {
        return 0;
    }
    sky_memcpy4(src, week_days + (tm.tm_wday << 2));
    src += 4;
    *(src++) = ' ';
    if (tm.tm_mday < 10) {
        *(src++) = '0';
        *(src++) = num_to_uchar(tm.tm_mday);
    } else {
        src += sky_u8_to_str((sky_u8_t) tm.tm_mday, src);
    }
    *(src++) = ' ';
    sky_memcpy4(src, months + (tm.tm_mon << 2));
    src += 4;

    src += sky_i32_to_str(tm.tm_year + 1900, src);
    *(src++) = ' ';

    src += sky_time_to_str(day_of_time, src);
    sky_memcpy4(src, " GMT");
    src += 4;
    *src = '\0';

    return 29;
}
