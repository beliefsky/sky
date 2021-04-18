//
// Created by weijing on 2019/12/30.
//

#include "uuid.h"
#include <stdlib.h>
#include <stdio.h>

void sky_uuid_generate_random(sky_uchar_t buf[37]) {
    const sky_char_t *c = "89ab";
    sky_u32_t b;
    sky_char_t *p;

    p = (sky_char_t *) buf;
    for(sky_u8_t i = 0; i < 16; ++i) {
        b = rand() % 255;
        switch (i) {
            case 6:
                sprintf(p, "4%x", b % 15);
                break;
            case 8:
                sprintf(p, "%c%x", c[rand() & 3], b % 15);
                break;
            default:
                sprintf(p, "%02x", b);
                break;
        }
        p += 2;
        switch (i) {
            case 3:
            case 5:
            case 7:
            case 9:
                *p++ = '-';
                break;
        }
    }
    *p = '\0';
}