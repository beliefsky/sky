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

typedef sky_u64_t (*sky_hashmap_hash_pt)(const void *item, void *u_data);

typedef sky_bool_t (*sky_hashmap_equals_pt)(const void *a, const void *b);

typedef sky_bool_t (*sky_hashmap_iter_pt)(void *item, void *user_data);

typedef void (*sky_hashmap_free_pt)(void *item);


sky_hashmap_t *sky_hashmap_create(sky_hashmap_hash_pt hash, sky_hashmap_equals_pt equals, void *hash_secret);

sky_hashmap_t *sky_hashmap_create_with_cap(sky_hashmap_hash_pt hash, sky_hashmap_equals_pt equals,
                                           void *hash_secret, sky_usize_t cap);

sky_usize_t sky_hashmap_count(const sky_hashmap_t *map);

void *sky_hashmap_put(sky_hashmap_t *map, void *item);

void *sky_hashmap_put_with_hash(sky_hashmap_t *map, sky_u64_t hash, void *item);

sky_u64_t sky_hashmap_get_hash(const sky_hashmap_t *map, const void *item);

void *sky_hashmap_get(const sky_hashmap_t *map, const void *item);

void *sky_hashmap_get_with_hash(const sky_hashmap_t *map, sky_u64_t hash, const void *item);

void *sky_hashmap_del(sky_hashmap_t *map, const void *item);

void *sky_hashmap_del_with_hash(sky_hashmap_t *map, sky_u64_t hash, const void *item);

sky_bool_t sky_hashmap_scan(sky_hashmap_t *map, sky_hashmap_iter_pt iter, void *user_data);

void sky_hashmap_clean(sky_hashmap_t *map, sky_hashmap_free_pt free, sky_bool_t recap);

void sky_hashmap_destroy(sky_hashmap_t *map, sky_hashmap_free_pt free);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_HASHMAP_H
