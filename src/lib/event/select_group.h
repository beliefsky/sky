//
// Created by beliefsky on 2023/3/24.
//

#ifndef SKY_SELECT_GROUP_H
#define SKY_SELECT_GROUP_H

#include "selector.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_selector_group_s sky_selector_group_t;

typedef void (*sky_selector_group_cb_pt)(sky_selector_t *selector, void *data);

struct sky_selector_group_s {
    sky_u32_t n;
    sky_selector_t *s;
};

sky_selector_group_t *sky_selector_group_create(sky_u32_t n);

void sky_selector_group_run(sky_selector_group_t *g);

void sky_selector_group_destroy(sky_selector_group_t *g);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_SELECT_GROUP_H
