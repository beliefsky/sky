//
// Created by beliefsky on 2022/2/22.
//

#include "hashmap.h"
#include "memory.h"

typedef struct {
    sky_u64_t hash: 48;
    sky_u64_t dib: 16;
    void *data;
} bucket_t;

struct sky_hashmap_s {
    bucket_t spare;
    bucket_t e_data;
    sky_hashmap_hash_pt hash;
    sky_hashmap_cmp_pt cmp;
    sky_usize_t cap;
    sky_usize_t bucket_num;
    sky_usize_t mask;
    bucket_t *buckets;
};

sky_hashmap_t *
sky_hashmap_create(sky_hashmap_hash_pt hash, sky_hashmap_cmp_pt cmp) {
    sky_hashmap_t *map = sky_malloc(sizeof(sky_hashmap_t));
    map->spare.hash = 0;
    map->spare.dib = 0;
    map->spare.data = null;
    map->e_data.hash = 0;
    map->e_data.dib = 0;
    map->e_data.data = null;
    map->hash = hash;
    map->cmp = cmp;
    map->cap = 16;
    map->bucket_num = map->cap;
    map->mask = map->bucket_num - 1;
    map->buckets = sky_malloc(sizeof(bucket_t) * map->bucket_num);

    return map;
}

void *
sky_hashmap_put(sky_hashmap_t *map, void *item) {

    return null;
}

void *
sky_hashmap_get(sky_hashmap_t *map, void *item) {

    return null;
}

void *
sky_hashmap_del(sky_hashmap_t *map, void *item) {

    return null;
}

void
sky_hashmap_destroy(sky_hashmap_t *map) {
    sky_free(map->buckets);
    sky_free(map);
}