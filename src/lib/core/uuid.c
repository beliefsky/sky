//
// Created by weijing on 2019/12/30.
//

#include "uuid.h"
#include "random.h"
#include "string.h"

sky_bool_t
sky_uuid_generate_random(sky_uuid_t *uuid) {
    sky_uchar_t *bytes = uuid->bytes;

    if (sky_unlikely(!sky_random_bytes(bytes, 16))) {
        return false;
    }
    bytes[6] &= 0x0f;  /* clear version        */
    bytes[6] |= 0x40;  /* set to version 4     */
    bytes[8] &= 0x3f;  /* clear variant        */
    bytes[8] |= 0x80;  /* set to IETF variant  */
}

void
sky_uuid_to_str(sky_uuid_t *uuid, sky_uchar_t out[36]) {
    sky_uchar_t *bytes = uuid->bytes;
    sky_byte_to_hex(bytes, 4, out);
    bytes += 4;
    out += 8;
    *out++ = '-';
    for (int i = 0; i < 3; ++i) {
        sky_byte_to_hex(bytes, 2, out);
        bytes += 2;
        out += 4;
        *out++ = '-';
    }
    sky_byte_to_hex(bytes, 6, out);
    out += 12;
    *out = '\0';
}