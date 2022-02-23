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
typedef sky_u64_t (*sky_hashmap_hash_pt)(const void *item);
typedef sky_bool_t (*sky_hashmap_equals_pt)(const void *a, const void *b);

sky_hashmap_t *sky_hashmap_create(sky_hashmap_hash_pt hash, sky_hashmap_equals_pt equals);

sky_hashmap_t *sky_hashmap_create_with_cap(sky_hashmap_hash_pt hash, sky_hashmap_equals_pt equals, sky_usize_t cap);

sky_usize_t sky_hashmap_count(sky_hashmap_t *map);

void *sky_hashmap_put(sky_hashmap_t *map, void *item);

void *sky_hashmap_get(sky_hashmap_t *map, void *item);

void *sky_hashmap_del(sky_hashmap_t *map, void *item);

void sky_hashmap_destroy(sky_hashmap_t *map);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HASHMAP_H
