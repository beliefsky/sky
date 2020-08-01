//
// Created by weijing on 2019/11/14.
//

#include "trie.h"
#include "string.h"

#define NODE_LEN 32
#define NODE_PTR 31

typedef struct sky_trie_node_s sky_trie_node_t;

struct sky_trie_node_s {
    sky_trie_node_t *next[NODE_LEN];
    sky_uchar_t *key;
    sky_size_t key_n;
    sky_uintptr_t value;
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


static sky_inline sky_size_t
str_cmp_index(sky_uchar_t *one, sky_uchar_t *two, sky_size_t min_len) {
    sky_size_t i;

    i = min_len;
    while (*one++ == *two++ && --i) {
    }

    return min_len - i;
}


void
sky_trie_put(sky_trie_t *trie, sky_str_t *key, sky_uintptr_t value) {
    sky_trie_node_t **k_node, *pre_node, *tmp;
    sky_uchar_t *tmp_key;
    sky_size_t len, index;

    pre_node = &trie->root;
    if (!key->len) {
        pre_node->value = value;
        return;
    }
    for (tmp_key = key->data, k_node = &pre_node->next[*tmp_key++ & NODE_PTR];
         *tmp_key;
         k_node = &pre_node->next[*tmp_key++ & NODE_PTR]) {
        if (!(tmp = *k_node)) {
            *k_node = tmp = sky_pcalloc(trie->pool, sizeof(sky_trie_node_t));
            tmp->key_n = key->len - (sky_size_t) (tmp_key - key->data);
            tmp->key = tmp_key;
            tmp->value = value;
            return;
        }
        len = key->len - (sky_size_t) (tmp_key - key->data);
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

                pre_node->next[*tmp->key++ & NODE_PTR] = tmp;
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
        pre_node->key = tmp->key;

        tmp->key_n -= index + 1;
        tmp->key += index;
        pre_node->next[*tmp->key++ & NODE_PTR] = tmp;

        tmp = sky_pcalloc(trie->pool, sizeof(sky_trie_node_t));
        tmp->key_n = len - index - 1;
        tmp->key = tmp_key + index;
        tmp->value = value;
        pre_node->next[*tmp->key++ & NODE_PTR] = tmp;
        return;
    }
}


sky_uintptr_t
sky_trie_find(sky_trie_t *trie, sky_str_t *key) {
    sky_trie_node_t *node;
    sky_uintptr_t previous_value;
    sky_uchar_t *tmp_key;
    sky_size_t len;

    if (!key->len) {
        return trie->root.value;
    }
    tmp_key = key->data;
    previous_value = 0;
    for (node = &trie->root; node && *tmp_key; node = node->next[*tmp_key++ & NODE_PTR]) {
        if (!node->key_n) {
            if (node->value) {
                previous_value = node->value;
            }
            continue;
        }
        len = key->len - (sky_size_t) (tmp_key - key->data);
        if (len < node->key_n) {
            return previous_value;
        }
        if (sky_strncmp(tmp_key, node->key, node->key_n) != 0) {
            return previous_value;
        }
        if (len == node->key_n) {
            return node->value;
        }
        tmp_key += node->key_n;

        if (node->value) {
            previous_value = node->value;
        }
    }
    return previous_value;
}


sky_uintptr_t sky_trie_contains(sky_trie_t *trie, sky_str_t *key) {
    sky_trie_node_t *node;
    sky_uchar_t *tmp_key;
    sky_size_t len;

    if (!key->len) {
        return trie->root.value;
    }
    tmp_key = key->data;
    for (node = &trie->root; node && *tmp_key; node = node->next[*tmp_key++ & NODE_PTR]) {
        if (!node->key_n) {
            continue;
        }
        len = key->len - (sky_size_t) (tmp_key - key->data);
        if (len < node->key_n) {
            return 0;
        }
        if (sky_strncmp(tmp_key, node->key, node->key_n) != 0) {
            return 0;
        }
        if (len == node->key_n) {
            return node->value;
        }
        tmp_key += node->key_n;
    }
    return 0;
}
