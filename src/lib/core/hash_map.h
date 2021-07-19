//
// Created by weijing on 17-11-24.
//

#ifndef SKY_HASH_MAP_H
#define SKY_HASH_MAP_H

#include "types.h"
#include "string.h"
#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_HASH_DEFAULT_BUCKETS    64

typedef struct sky_hash_map_s sky_hash_map_t;

/**
 *
 * @param pool
 * @param bucket_size
 * @return
 */
sky_hash_map_t *sky_hash_map_bucket(sky_pool_t *pool, sky_u32_t bucket_size);

void *sky_hash_map_put(sky_hash_map_t *hash, const sky_uchar_t *key, sky_usize_t key_len, const void *data);

void *sky_hash_map_get(const sky_hash_map_t *hash, const sky_uchar_t *key, sky_usize_t key_len);

static sky_inline void *
sky_hash_map_put2(sky_hash_map_t *hash, const sky_str_t *key, const void *data) {
    return sky_hash_map_put(hash, key->data, key->len, data);
}

static sky_inline void *
sky_hash_map_get2(const sky_hash_map_t *hash, const sky_str_t *key) {
    return sky_hash_map_get(hash, key->data, key->len);
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HASH_MAP_H
