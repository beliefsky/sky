//
// Created by weijing on 2019/12/30.
//

#include "uuid.h"
#include "random.h"
#include "base16.h"

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

    return true;
}

void
sky_uuid_to_str(sky_uuid_t *uuid, sky_uchar_t out[36]) {
    sky_uchar_t *bytes = uuid->bytes;
    out += sky_base16_encode(out, bytes, 4);
    bytes += 4;
    *out++ = '-';
    for (int i = 0; i < 3; ++i) {
        out += sky_base16_encode(out, bytes, 2);
        bytes += 2;
        *out++ = '-';
    }
    out += sky_base16_encode(out, bytes, 6);
    *out = '\0';
}