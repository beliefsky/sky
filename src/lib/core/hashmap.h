//
// Created by beliefsky on 2022/2/22.
//

#ifndef SKY_HASHMAP_H
#define SKY_HASHMAP_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_hashmap_s sky_hashmap_t;
typedef sky_u64_t (*sky_hashmap_hash_pt)(void *item);
typedef sky_i32_t (*sky_hashmap_cmp_pt)(void *a, void *b);

sky_hashmap_t *sky_hashmap_create(sky_hashmap_hash_pt hash, sky_hashmap_cmp_pt cmp);

void *sky_hashmap_put(sky_hashmap_t *map, void *item);

void *sky_hashmap_get(sky_hashmap_t *map, void *item);

void *sky_hashmap_del(sky_hashmap_t *map, void *item);

void sky_hashmap_destroy(sky_hashmap_t *map);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HASHMAP_H
