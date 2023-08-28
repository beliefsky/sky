//
// Created by beliefsky on 2023/4/30.
//

#include "dns_cache.h"
#include "../../core/crc32.h"


void
sky_dns_cache_init(sky_dns_cache_t *cache) {
    sky_rb_tree_init(&cache->host_tree);
}

sky_dns_host_t *
sky_dns_cache_host_get(sky_dns_cache_t *cache, const sky_str_t *name) {
    sky_rb_tree_t *tree = &cache->host_tree;
    sky_rb_node_t *node = tree->root;
    sky_dns_host_t *entry;
    sky_u32_t hash = sky_crc32_init();
    hash = sky_crc32_update(hash, name->data, name->len);
    hash = sky_crc32_final(hash);

    sky_i32_t r;

    while (node != &tree->sentinel) {
        entry = sky_type_convert(node, sky_dns_host_t, node);
        if (entry->hash == hash) {
            r = sky_str_cmp(&entry->host, name);
            if (!r) {
                return entry;
            }
            node = r > 0 ? node->left : node->right;

        } else {
            node = entry->hash > hash ? node->left : node->right;
        }
    }

    return null;
}

void
sky_dns_cache_host_add(sky_dns_cache_t *cache, sky_dns_host_t *entry) {
    sky_rb_tree_t *tree = &cache->host_tree;

    if (sky_rb_tree_is_empty(tree)) {
        sky_rb_tree_link(tree, &entry->node, null);
        return;
    }

    sky_rb_node_t **p, *temp = tree->root;
    sky_dns_host_t *host;
    sky_i32_t r;

    for (;;) {
        host = sky_type_convert(temp, sky_dns_host_t, node);
        if (entry->hash == host->hash) {
            r = sky_str_cmp(&entry->host, &host->host);
            p = r < 0 ? &temp->left : &temp->right;
        } else {
            p = entry->hash < host->hash ? &temp->left : &temp->right;
        }
        if (*p == &tree->sentinel) {
            *p = &entry->node;
            sky_rb_tree_link(tree, &entry->node, temp);
            return;
        }
        temp = *p;
    }
}

void
dns_cache_host_add(sky_dns_cache_t *cache, sky_rb_node_t *node) {
    sky_rb_tree_t *tree = &cache->host_tree;

    if (sky_rb_tree_is_empty(tree)) {
        sky_rb_tree_link(tree, node, null);
        return;
    }

    sky_rb_node_t **p, *temp = tree->root;
    sky_dns_host_t *host, *entry = sky_type_convert(node, sky_dns_host_t, node);
    sky_i32_t r;

    for (;;) {
        host = sky_type_convert(temp, sky_dns_host_t, node);
        if (entry->hash == host->hash) {
            r = sky_str_cmp(&entry->host, &host->host);
            p = r < 0 ? &temp->left : &temp->right;
        } else {
            p = entry->hash < host->hash ? &temp->left : &temp->right;
        }
        if (*p == &tree->sentinel) {
            *p = node;
            sky_rb_tree_link(tree, node, temp);
            return;
        }
        temp = *p;
    }
}

void
sky_dns_cache_host_remove(sky_dns_cache_t *cache, sky_dns_host_t *host) {
    sky_rb_tree_del(&cache->host_tree, &host->node);
}