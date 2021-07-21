//
// Created by weijing on 17-11-24.
//

#include "hash.h"
#include "crc32.h"
#include "log.h"

typedef struct hash_entry_s hash_entry_t;

typedef void *(*put_pt)(sky_hash_t *hash, sky_uchar_t *key, sky_usize_t key_len, void *data);

typedef void *(*get_pt)(const sky_hash_t *hash, const sky_uchar_t *key, sky_usize_t key_len);

struct hash_entry_s {
    sky_u32_t hash;
    sky_str_t key;
    void *data;
    hash_entry_t *next;
};

struct sky_hash_s {
    sky_pool_t *pool;
    put_pt put;
    get_pt get;
    union {
        struct {
            hash_entry_t **entry;
            sky_u32_t mask;
        } buckets;
    };
};

static void *map_buckets_put(sky_hash_t *hash, sky_uchar_t *key, sky_usize_t key_len, void *data);

void *map_buckets_get(const sky_hash_t *hash, const sky_uchar_t *key, sky_usize_t key_len);

sky_hash_t *
sky_hash_bucket(sky_pool_t *pool, sky_u32_t bucket_size) {
    if (sky_unlikely(!bucket_size)) {
        sky_log_error("bucket_size 必须大于0");
        return null;
    }
    if (sky_unlikely(sky_is_2_power(bucket_size))) {
        sky_log_error("bucket_size 必须为2的整数幂");
        return null;
    }

    sky_hash_t *hash = sky_palloc(pool, sizeof(sky_hash_t));
    hash->pool = pool;
    hash->put = map_buckets_put;
    hash->get = map_buckets_get;
    hash->buckets.entry = sky_pcalloc(pool, sizeof(hash_entry_t *) * bucket_size);
    hash->buckets.mask = bucket_size - 1;

    return hash;
}

void *
sky_hash_put(sky_hash_t *hash, sky_uchar_t *key, sky_usize_t key_len, void *data) {
    return hash->put(hash, key, key_len, data);
}

void *
sky_hash_get(const sky_hash_t *hash, const sky_uchar_t *key, sky_usize_t key_len) {
    return hash->get(hash, key, key_len);
}

static void *
map_buckets_put(sky_hash_t *hash, sky_uchar_t *key, sky_usize_t key_len, void *data) {
    sky_u32_t h;
    hash_entry_t *entry, **ptr;


    if (!key_len) {
        h = 0;
    } else {
        h = sky_crc32_init();
        h = sky_crc32c_update(h, key, key_len);
        h = sky_crc32_final(h);
    }
    ptr = hash->buckets.entry + (h & hash->buckets.mask);
    entry = *ptr;
    if (!entry) {
        *ptr = entry = sky_palloc(hash->pool, sizeof(hash_entry_t));
        entry->hash = h;
        entry->key.data = key;
        entry->key.len = key_len;
        entry->data = data;
        entry->next = null;

        return null;
    }

    for (;;) {
        if (entry->hash == h && sky_str_equals2(&entry->key, key, key_len)) {
            void *old_data = entry->data;
            entry->data = data;
            return old_data;
        }
        if (entry->next != null) {
            entry = entry->next;
            continue;
        }
        entry = entry->next = sky_palloc(hash->pool, sizeof(hash_entry_t));
        entry->hash = h;
        entry->key.data = key;
        entry->key.len = key_len;
        entry->data = data;
        entry->next = null;

        return null;
    }
}

void *
map_buckets_get(const sky_hash_t *hash, const sky_uchar_t *key, sky_usize_t key_len) {
    sky_u32_t h;
    hash_entry_t *entry, **ptr;

    if (!key_len) {
        h = 0;
    } else {
        h = sky_crc32_init();
        h = sky_crc32c_update(h, key, key_len);
        h = sky_crc32_final(h);
    }
    ptr = hash->buckets.entry + (h & hash->buckets.mask);
    entry = *ptr;
    if (!entry) {
        return null;
    }

    do {
        if (entry->hash == h && sky_str_equals2(&entry->key, key, key_len)) {
            return entry->data;
        }
    } while ((entry = entry->next) != null);

    return null;
}