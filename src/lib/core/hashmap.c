//
// Created by beliefsky on 2022/2/22.
//

#include "hashmap.h"
#include "memory.h"


typedef struct {
    sky_u64_t hash: 48;
    sky_u16_t dib: 16;
    void *data;
} bucket_t;

struct sky_hashmap_s {
    sky_hashmap_hash_pt hash;
    sky_hashmap_equals_pt equals;
    sky_usize_t count;
    sky_usize_t cap;
    sky_usize_t bucket_num;
    sky_usize_t mask;
    sky_usize_t grow_at;
    sky_usize_t shrink_at;
    void *hash_secret;
    bucket_t *buckets;
};

#define get_hash(_map, _item) \
    ((_map)->hash(_item, (_map)->hash_secret) << 16 >> 16)

static sky_usize_t two_power_next(sky_usize_t n);

static sky_hashmap_t *hashmap_create(sky_hashmap_hash_pt hash, sky_hashmap_equals_pt equals,
                                     void *hash_secret, sky_usize_t cap);

static void buckets_resize(sky_hashmap_t *map, sky_usize_t bucket_num);

sky_hashmap_t *
sky_hashmap_create(sky_hashmap_hash_pt hash, sky_hashmap_equals_pt equals, void *hash_secret) {
    return hashmap_create(hash, equals, hash_secret, 16);
}

sky_hashmap_t *
sky_hashmap_create_with_cap(sky_hashmap_hash_pt hash, sky_hashmap_equals_pt equals, void *hash_secret,
                            sky_usize_t cap) {
    if (cap < 16) {
        cap = 16;
    } else if (!sky_is_2_power(cap)) {
        cap = two_power_next(cap);
    }
    return hashmap_create(hash, equals, hash_secret, cap);
}

sky_usize_t
sky_hashmap_count(const sky_hashmap_t *map) {
    return map->count;
}

void *
sky_hashmap_put(sky_hashmap_t *map, void *item) {
    if (sky_unlikely(map->count == map->grow_at)) {
        buckets_resize(map, map->bucket_num << 1);
    }
    sky_u64_t hash = get_hash(map, item);
    sky_u16_t dib = 1;
    sky_usize_t i = hash & map->mask;

    bucket_t *bucket;
    for (;;) {
        bucket = map->buckets + i;
        if (bucket->dib == SKY_U16(0)) {
            bucket->hash = hash;
            bucket->dib = dib;
            bucket->data = item;

            ++map->count;
            return null;
        }

        if (hash == bucket->hash && map->equals(item, bucket->data)) {
            void *data = bucket->data;

            bucket->data = item;

            return data;
        }
        if (bucket->dib < dib) {
            void *tmp = bucket->data;

            sky_swap(hash, bucket->hash);
            sky_swap(dib, bucket->dib);
            bucket->data = item;
            item = tmp;
        }
        i = (i + 1) & map->mask;
        ++dib;
    }
}

void *
sky_hashmap_get(sky_hashmap_t *map, const void *item) {
    const sky_u64_t hash = get_hash(map, item);
    sky_usize_t i = hash & map->mask;

    bucket_t *bucket;
    for (;;) {
        bucket = map->buckets + i;
        if (!bucket->dib) {
            return null;
        }
        if (bucket->hash == hash && map->equals(item, bucket->data)) {
            return bucket->data;
        }
        i = (i + 1) & map->mask;
    }
}

void *
sky_hashmap_del(sky_hashmap_t *map, const void *item) {
    const sky_u64_t hash = get_hash(map, item);
    sky_usize_t i = hash & map->mask;

    bucket_t *bucket, *prev;
    for (;;) {
        bucket = map->buckets + i;
        if (!bucket->dib) {
            return null;
        }

        if (bucket->hash == hash && map->equals(item, bucket->data)) {
            void *data = bucket->data;
            bucket->dib = 0;
            for (;;) {
                prev = bucket;
                i = (i + 1) & map->mask;
                bucket = map->buckets + i;
                if (bucket->dib <= SKY_U16(1)) {
                    prev->dib = 0;
                    break;
                }
                prev->hash = bucket->hash;
                prev->dib = bucket->dib;
                prev->data = bucket->data;

                --(prev->dib);
            }
            --map->count;
            if (map->count <= map->shrink_at && map->bucket_num > map->cap) {
                buckets_resize(map, map->bucket_num >> 1);
            }
            return data;
        }
        i = (i + 1) & map->mask;
    }
}

sky_bool_t
sky_hashmap_scan(sky_hashmap_t *map, sky_hashmap_iter_pt iter, void *user_data) {
    if (map->count == 0) {
        return true;
    }
    bucket_t *bucket = map->buckets;
    for (sky_usize_t i = map->bucket_num; i > 0; --i, ++bucket) {
        if (bucket->dib) {
            if (!iter(bucket->data, user_data)) {
                return false;
            }
        }
    }
    return true;
}

void
sky_hashmap_clean(sky_hashmap_t *map, sky_hashmap_free_pt free, sky_bool_t recap) {
    if (free && map->count != 0) {
        bucket_t *bucket = map->buckets;
        for (sky_usize_t i = map->bucket_num; i > 0; --i, ++bucket) {
            if (bucket->dib) {
                bucket->dib = 0;
                free(bucket->data);
            }
        }
        if (recap) {
            map->cap = map->bucket_num;
        } else if (map->cap != map->bucket_num) {
            bucket_t *tmp = sky_realloc(map->buckets, sizeof(bucket_t) * map->cap);
            if (sky_unlikely(tmp != map->buckets)) {
                sky_memzero(tmp, sizeof(bucket_t) * map->cap);
            }
            map->bucket_num = map->cap;
            map->buckets = tmp;
            map->mask = map->bucket_num - 1;
            map->grow_at = (map->bucket_num * 3) >> 2;
            map->shrink_at = map->bucket_num >> 3;
        }
    } else {
        if (recap) {
            map->cap = map->bucket_num;
            sky_memzero(map->buckets, sizeof(bucket_t) * map->bucket_num);
        } else if (map->cap != map->bucket_num) {
            map->buckets = sky_realloc(map->buckets, sizeof(bucket_t) * map->cap);
            sky_memzero(map->buckets, sizeof(bucket_t) * map->cap);

            map->bucket_num = map->cap;
            map->mask = map->bucket_num - 1;
            map->grow_at = (map->bucket_num * 3) >> 2;
            map->shrink_at = map->bucket_num >> 3;
        }
    }
    map->count = 0;
}

void sky_hashmap_destroy(sky_hashmap_t *map, sky_hashmap_free_pt free) {
    if (free && map->count != 0) {
        bucket_t *bucket = map->buckets;
        for (sky_usize_t i = map->bucket_num; i > 0; --i, ++bucket) {
            if (bucket->dib) {
                free(bucket->data);
            }
        }
    }
    sky_free(map->buckets);
    map->buckets = null;
    sky_free(map);
}

static sky_inline sky_usize_t
two_power_next(sky_usize_t n) {
#if SKY_USIZE_MAX == SKY_U64_MAX
    sky_i32_t l = 64 - __builtin_clzll(n);
#else
    sky_i32_t l = 32 - __builtin_clz(n);
#endif
    return SKY_USIZE(1) << l;
}

static sky_inline sky_hashmap_t *
hashmap_create(sky_hashmap_hash_pt hash, sky_hashmap_equals_pt equals, void *hash_secret, sky_usize_t cap) {
    sky_hashmap_t *map = sky_malloc(sizeof(sky_hashmap_t));
    map->hash = hash;
    map->equals = equals;
    map->count = 0;
    map->cap = cap;
    map->bucket_num = cap;
    map->mask = map->bucket_num - 1;
    map->grow_at = (map->bucket_num * 3) >> 2;
    map->shrink_at = map->bucket_num >> 3;
    map->hash_secret = hash_secret;
    map->buckets = sky_malloc(sizeof(bucket_t) * map->bucket_num);
    sky_memzero(map->buckets, sizeof(bucket_t) * map->bucket_num);

    return map;
}

static void
buckets_resize(sky_hashmap_t *map, sky_usize_t bucket_num) {
    bucket_t *new_buckets = sky_malloc(sizeof(bucket_t) * bucket_num);
    sky_memzero(new_buckets, sizeof(bucket_t) * bucket_num);
    sky_u64_t mask = bucket_num - 1;

    bucket_t *entry = map->buckets;
    for (sky_usize_t i = map->bucket_num; i > 0; --i, ++entry) {
        if (!entry->dib) {
            continue;
        }
        entry->dib = SKY_U16(1);
        sky_usize_t j = entry->hash & mask;
        for (;;) {
            bucket_t *bucket = new_buckets + j;
            if (bucket->dib == SKY_U16(0)) {
                bucket->hash = entry->hash;
                bucket->dib = entry->dib;
                bucket->data = entry->data;
                break;
            }
            if (bucket->dib < entry->dib) {
                const bucket_t tmp = {
                        .hash = bucket->hash,
                        .dib = bucket->dib,
                        .data = bucket->data
                };

                bucket->hash = entry->hash;
                bucket->dib = entry->dib;
                bucket->data = entry->data;

                entry->hash = tmp.hash;
                entry->dib = tmp.dib;
                entry->data = tmp.data;
            }
            j = (j + 1) & mask;
            ++entry->dib;
        }
    }

    sky_free(map->buckets);
    map->buckets = new_buckets;

    map->bucket_num = bucket_num;
    map->mask = mask;
    map->grow_at = (bucket_num * 3) >> 2;
    map->shrink_at = bucket_num >> 3;
}