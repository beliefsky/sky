//
// Created by edz on 2022/2/22.
//

#include "topic_tree.h"
#include "memory.h"
#include "hashmap.h"
#include "crc32.h"

typedef struct topic_node_s topic_node_t;

struct topic_node_s {
    sky_str_t key;
    sky_usize_t num;
    sky_usize_t client_n;
    sky_topic_tree_t *tree;
    topic_node_t *parent;
    sky_hashmap_t *map;
    void *client;
};

struct sky_topic_tree_s {
    topic_node_t node;
    sky_topic_tree_sub_pt sub;
    sky_topic_tree_unsub_pt unsub;
    sky_topic_tree_destroy_pt destroy;
};

static sky_u64_t topic_hash(const void *a, void *secret);

static sky_bool_t topic_equals(const void *a, const void *b);

static void topic_tree_scan(sky_hashmap_t *map, sky_uchar_t *key, sky_usize_t len,
                            sky_topic_tree_iter_pt iter, void *user_data);

static topic_node_t *topic_node_sub(topic_node_t *parent, sky_uchar_t *key, sky_usize_t len);

static topic_node_t *topic_node_get(sky_hashmap_t *map, sky_uchar_t *key, sky_usize_t len);

static void topic_node_clean(topic_node_t *node, void *user_data);

static void topic_node_destroy(topic_node_t *node, void *user_data);


sky_topic_tree_t *
sky_topic_tree_create(sky_topic_tree_sub_pt sub,
                      sky_topic_tree_unsub_pt unsub,
                      sky_topic_tree_destroy_pt destroy
) {
    sky_topic_tree_t *tree = sky_malloc(sizeof(sky_topic_tree_t));
    tree->node.map = sky_hashmap_create(topic_hash, topic_equals, null);
    tree->node.tree = tree;
    tree->node.parent = null;
    tree->sub = sub;
    tree->unsub = unsub;
    tree->destroy = destroy;

    return tree;
}

void
sky_topic_tree_sub(sky_topic_tree_t *tree, const sky_str_t *topic, void *user_data) {
    sky_uchar_t *p = topic->data;
    sky_usize_t len = topic->len;

    topic_node_t *node = &tree->node;
    sky_isize_t index;
    for (;;) {
        index = sky_str_len_index_char(p, len, '/');
        if (index == -1) {
            node = topic_node_sub(node, p, len);
            if (sky_likely(null != tree->sub)) {
                // 如果执行返回为 false, 将不增加计数
                if (sky_unlikely(!tree->sub(&node->client, user_data))) {
                    do {
                        --node->num;
                        node = node->parent;
                    } while (null != node);
                    return;
                }
            }
            ++node->client_n;

            return;
        }
        node = topic_node_sub(node, p, (sky_usize_t) index);
        len -= (sky_usize_t) ++index;
        p += index;
    }
}

void
sky_topic_tree_unsub(sky_topic_tree_t *tree, const sky_str_t *topic, void *user_data) {
    sky_uchar_t *p = topic->data;
    sky_usize_t len = topic->len;

    topic_node_t *node = &tree->node;
    sky_isize_t index;
    for (;;) {
        index = sky_str_len_index_char(p, len, '/');
        if (index == -1) {
            node = topic_node_get(node->map, p, len);
            if (null != node) {
                topic_node_clean(node, user_data);
            }
            return;
        }
        node = topic_node_get(node->map, p, (sky_usize_t) index);
        if (!node) {
            return;
        }
        len -= (sky_usize_t) ++index;
        p += index;
    }
}

void
sky_topic_tree_scan(sky_topic_tree_t *tree, const sky_str_t *topic,
                    sky_topic_tree_iter_pt iter, void *user_data) {
    topic_tree_scan(tree->node.map, topic->data, topic->len, iter, user_data);
}

void
sky_topic_tree_destroy(sky_topic_tree_t *tree) {
    sky_hashmap_destroy(tree->node.map, (sky_hashmap_free_pt) topic_node_destroy, null);
    tree->node.map = null;
    sky_free(tree);
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

static void
topic_tree_scan(sky_hashmap_t *map, sky_uchar_t *key, sky_usize_t len,
                sky_topic_tree_iter_pt iter, void *user_data) {
    topic_node_t *node1 = topic_node_get(map, sky_str_line("#"));
    if (null != node1 && node1->client_n != 0) {
        iter(node1->client, user_data);
    }
    topic_node_t *node2 = topic_node_get(map, sky_str_line("+"));

    sky_isize_t index = sky_str_len_index_char(key, len, '/');
    if (index == -1) {
        if (null != node2 && 0 != node2->client_n) {
            // 执行 node 2
            iter(node2->client, user_data);
        }
        topic_node_t *node3 = topic_node_get(map, key, len);
        if (null != node3 && 0 != node3->client_n) {
            // 执行 node 3
            iter(node3->client, user_data);
        }
    } else {
        topic_node_t *node3 = topic_node_get(map, key, (sky_usize_t) index);
        len -= (sky_usize_t) ++index;
        key += index;

        if (null != node2) {
            topic_tree_scan(node2->map, key, len, iter, user_data);
        }
        if (null != node3) {
            topic_tree_scan(node3->map, key, len, iter, user_data);
        }
    }

}

static sky_inline topic_node_t *
topic_node_sub(topic_node_t *parent, sky_uchar_t *key, sky_usize_t len) {
    const topic_node_t tmp = {
            .key.data = key,
            .key.len = len
    };
    topic_node_t *node = sky_hashmap_get(parent->map, &tmp);
    if (!node) {
        node = sky_malloc(sizeof(topic_node_t) + len);
        node->key.data = (sky_uchar_t *) (node + 1);
        node->key.len = len;
        sky_memcpy(node->key.data, key, len);
        node->num = 0;
        node->client_n = 0;
        node->tree = parent->tree;
        node->parent = parent;
        node->map = sky_hashmap_create(topic_hash, topic_equals, null);
        node->client = null;

        sky_hashmap_put(parent->map, node);
    }
    ++node->num;

    return node;
}


static sky_inline topic_node_t *
topic_node_get(sky_hashmap_t *map, sky_uchar_t *key, sky_usize_t len) {
    const topic_node_t tmp = {
            .key.data = key,
            .key.len = len
    };
    return sky_hashmap_get(map, &tmp);
}


static void
topic_node_clean(topic_node_t *node, void *user_data) {
    sky_topic_tree_t *tree = node->tree;
    topic_node_t *parent = node->parent;

    --node->num;
    if (0 != node->num) {
        if (node->client_n > 0) {
            --node->client_n;
            if (sky_likely(null != tree->unsub)) {
                tree->unsub(&node->client, user_data);
            }
        }
        return;
    }
    if (node->client_n > 0) {
        if (sky_likely(null != tree->unsub)) {
            tree->unsub(&node->client, user_data);
        }
        if (sky_likely(null != tree->destroy)) {
            tree->destroy(node->client);
        }
    }
    sky_hashmap_destroy(node->map, null, null);
    node->map = null;

    sky_hashmap_del(parent->map, node);
    sky_free(node);

    do {
        node = parent;
        if (0 != (--node->num)) {
            break;
        }
        parent = node->parent;
        sky_hashmap_del(parent->map, node);
        sky_free(node);
    } while (null != parent);

    while (null != (node = node->parent)) {
        --node->num;
    }
}

static void
topic_node_destroy(topic_node_t *node, void *user_data) {
    sky_hashmap_destroy(node->map, (sky_hashmap_free_pt) topic_node_destroy, user_data);
    node->map = null;

    const sky_topic_tree_destroy_pt destroy = node->tree->destroy;
    if (sky_likely(0 != node->client_n && null != destroy)) {
        destroy(node->client);
    }

    sky_free(node);
}
