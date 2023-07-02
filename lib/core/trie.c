//
// Created by weijing on 2019/11/14.
//

#include <core/trie.h>

typedef struct sky_trie_node_s sky_trie_node_t;

struct sky_trie_node_s {
    sky_trie_node_t *next[256];
    sky_uchar_t *key;
    sky_usize_t key_n;
    void *value;
};

struct sky_trie_s {
    sky_trie_node_t root;
    sky_pool_t *pool;
};


sky_trie_t *
sky_trie_create(sky_pool_t *pool) {
    sky_trie_t *trie;

    trie = sky_pcalloc(pool, sizeof(sky_trie_t));
    trie->pool = pool;

    return trie;
}


static sky_inline sky_usize_t
str_cmp_index(const sky_uchar_t *one, const sky_uchar_t *two, sky_usize_t min_len) {
    sky_usize_t i;

    i = min_len;
    while (i && *one++ == *two++) {
        --i;
    }

    return min_len - i;
}


void
sky_trie_put(sky_trie_t *trie, const sky_str_t *key, void *value) {
    sky_trie_node_t **k_node, *pre_node, *tmp;
    sky_uchar_t *tmp_key;
    sky_usize_t len, index;

    pre_node = &trie->root;
    if (!key || !key->len) {
        pre_node->value = value;
        return;
    }
    tmp_key = key->data;

    for (;;) {
        k_node = &pre_node->next[*tmp_key++];
        tmp = *k_node;

        if (!tmp) {
            *k_node = tmp = sky_pcalloc(trie->pool, sizeof(sky_trie_node_t));
            tmp->key_n = key->len - (sky_usize_t) (tmp_key - key->data);
            tmp->key = tmp_key;
            tmp->value = value;

            return;
        }
        len = key->len - (sky_usize_t) (tmp_key - key->data);
        if (len == tmp->key_n) {
            if (!len) {
                tmp->value = value;
                return;
            }
            index = str_cmp_index(tmp->key, tmp_key, len);
            if (index == len) {
                tmp->value = value;
                return;
            }
        } else if (len < tmp->key_n) {
            index = str_cmp_index(tmp->key, tmp_key, len);
            if (index == len) {
                *k_node = pre_node = sky_pcalloc(trie->pool, sizeof(sky_trie_node_t));
                pre_node->key_n = len;
                pre_node->key = tmp_key;
                pre_node->value = value;

                tmp->key_n -= len + 1;
                tmp->key += len;
                pre_node->next[*tmp->key++] = tmp;
                return;
            }
        } else {
            index = str_cmp_index(tmp->key, tmp_key, tmp->key_n);
            if (index == tmp->key_n) {
                tmp_key += index;
                pre_node = tmp;
                continue;
            }
        }
        *k_node = pre_node = sky_pcalloc(trie->pool, sizeof(sky_trie_node_t));
        pre_node->key_n = index;
        pre_node->key = tmp_key;

        tmp->key_n -= index + 1;
        tmp->key += index;
        pre_node->next[*tmp->key++] = tmp;

        tmp = sky_pcalloc(trie->pool, sizeof(sky_trie_node_t));
        tmp->key_n = len - index - 1;
        tmp->key = tmp_key + index;
        tmp->value = value;
        pre_node->next[*tmp->key++] = tmp;
        return;

    }
}


void *
sky_trie_find(const sky_trie_t *trie, const sky_str_t *key) {
    const sky_trie_node_t *node, *prev_node;
    const sky_uchar_t *tmp_key;
    sky_usize_t len;

    node = &trie->root;
    if (!key || !key->len) {
        return node->value;
    }
    tmp_key = key->data;
    prev_node = node;
    for (;;) {
        node = node->next[*tmp_key++];
        if (!node) {
            break;
        }
        len = key->len - (sky_usize_t) (tmp_key - key->data);

        if (len == node->key_n) {
            if (!len || sky_str_len_unsafe_equals(tmp_key, node->key, len)) {
                if (node->value) {
                    prev_node = node;
                }
            }
            break;
        }
        if (len < node->key_n || !sky_str_len_unsafe_starts_with(tmp_key, node->key, node->key_n)) {
            break;
        }
        tmp_key += node->key_n;

        if (node->value) {
            prev_node = node;
        }
    }

    return prev_node->value;
}


void *
sky_trie_contains(const sky_trie_t *trie, const sky_str_t *key) {
    const sky_trie_node_t *node;
    const sky_uchar_t *tmp_key;
    sky_usize_t len;

    node = &trie->root;
    if (!key || !key->len) {
        return node->value;
    }
    tmp_key = key->data;

    for (;;) {
        node = node->next[*tmp_key++];
        if (!node) {
            return null;
        }
        len = key->len - (sky_usize_t) (tmp_key - key->data);

        if (len == node->key_n) {
            if (!len || sky_str_len_unsafe_equals(tmp_key, node->key, len)) {
                return node->value;
            }
            return null;
        }
        if (len < node->key_n || !sky_str_len_unsafe_starts_with(tmp_key, node->key, node->key_n)) {
            return null;
        }
        tmp_key += node->key_n;
    }
}
