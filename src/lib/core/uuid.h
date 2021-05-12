//
// Created by weijing on 2019/12/30.
//

#ifndef SKY_UUID_H
#define SKY_UUID_H


#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_uchar_t bytes[16];
} sky_uuid_t;

sky_bool_t sky_uuid_generate_random(sky_uuid_t *uuid);

void sky_uuid_to_str(sky_uuid_t *uuid, sky_uchar_t out[36]);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_UUID_H
