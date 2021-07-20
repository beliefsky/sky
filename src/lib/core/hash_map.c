//
// Created by weijing on 17-11-24.
//

#include "hash_map.h"

struct sky_hash_map_s {
    sky_pool_t *pool;
};

sky_hash_map_t *
sky_hash_map_bucket(sky_pool_t *pool, sky_u32_t bucket_size) {
    sky_hash_map_t *hash = sky_palloc(pool, sizeof(sky_hash_map_t));
    hash->pool = pool;

    return hash;
}

void *
sky_hash_map_put(sky_hash_map_t *hash, const sky_uchar_t *key, sky_usize_t key_len, const void *data) {
    return null;
}

void *
sky_hash_map_get(const sky_hash_map_t *hash, const sky_uchar_t *key, sky_usize_t key_len) {
    return null;
}