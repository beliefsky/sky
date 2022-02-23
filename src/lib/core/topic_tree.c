//
// Created by edz on 2022/2/22.
//

#include "topic_tree.h"
#include "memory.h"
#include "hashmap.h"
#include "crc32.h"
#include "log.h"

typedef struct {
    sky_str_t key;
    sky_hashmap_t *map;
    sky_usize_t num;
    void *client;
} topic_node_t;

struct sky_topic_tree_s {
    sky_hashmap_t *map;
};

static sky_u64_t topic_hash(const void *a, void *secret);

static sky_bool_t topic_equals(const void *a, const void *b);

static topic_node_t *upsert_topic_node(sky_hashmap_t *map, sky_uchar_t *key, sky_usize_t len);


sky_topic_tree_t *
sky_topic_tree_create() {
    sky_topic_tree_t *tree = sky_malloc(sizeof(sky_topic_tree_t));
    tree->map = sky_hashmap_create(topic_hash, topic_equals, null);

    return tree;
}

void
sky_topic_tree_sub(sky_topic_tree_t *tree, const sky_str_t *topic, void *client) {
    sky_uchar_t *p = topic->data;
    sky_usize_t len = topic->len;
    sky_hashmap_t *map = tree->map;

    topic_node_t *node;
    sky_isize_t index;
    for (;;) {
        index = sky_str_len_index_char(p, len, '/');
        if (index == -1) {
            break;
        }
        node = upsert_topic_node(map, p, (sky_usize_t) index);
        map = node->map;
        len -= (sky_usize_t) ++index;
        p += index;
    }

    node = upsert_topic_node(map, p, len);
}


static sky_u64_t
topic_hash(const void *a, void *secret) {
    (void) secret;
    const topic_node_t *node = a;

    sky_u32_t crc = sky_crc32_init();
    crc = sky_crc32c_update(crc, node->key.data, node->key.len);

    return sky_crc32_final(crc);
}

static sky_bool_t
topic_equals(const void *a, const void *b) {
    const topic_node_t *na = a;
    const topic_node_t *nb = b;

    return sky_str_equals(&na->key, &nb->key);
}

static topic_node_t *
upsert_topic_node(sky_hashmap_t *map, sky_uchar_t *key, sky_usize_t len) {
    const topic_node_t tmp = {
            .key.data = key,
            .key.len = len
    };
    topic_node_t *node = sky_hashmap_get(map, &tmp);
    if (!node) {
        node = sky_malloc(sizeof(topic_node_t) + len);
        node->key.data = (sky_uchar_t *) (node + 1);
        node->key.len = len;
        sky_memcpy(node->key.data, key, len);
        node->map = sky_hashmap_create(topic_hash, topic_equals, null);

        sky_hashmap_put(map, node);
    }
    ++node->num;

    return node;
}
