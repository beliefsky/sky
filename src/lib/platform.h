//
// Created by edz on 2021/11/4.
//

#ifndef SKY_PLATFORM_H
#define SKY_PLATFORM_H

#include "core/types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_platform_s sky_platform_t;

typedef void (*sky_platform_pt)(sky_u32_t index);

typedef struct {
    sky_u32_t thread_size;
    sky_platform_pt run;
    void *data;
} sky_platform_conf_t;


sky_platform_t *sky_platform_create(const sky_platform_conf_t *conf);

void sky_platform_wait(sky_platform_t *platform);

void sky_platform_destroy(sky_platform_t *platform);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_PLATFORM_H
