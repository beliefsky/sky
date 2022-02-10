//
// Created by weijing on 17-11-12.
//

#include "radix_tree.h"

static sky_radix_node_t *sky_radix_alloc(sky_radix_tree_t *tree);

sky_radix_tree_t*
sky_radix_tree_create(sky_pool_t *pool, sky_isize_t preallocate) {
    sky_u32_t key, mask, inc;
    sky_radix_tree_t *tree;

    tree = sky_palloc(pool, sizeof(sky_radix_tree_t));
    if (!tree) {
        return null;
    }
    tree->pool = pool;
    tree->free = null;
    tree->start = null;
    tree->size = 0;
    tree->root = sky_radix_alloc(tree);
    if (!tree->root) {
        return null;
    }
    tree->root->right = null;
    tree->root->left = null;
    tree->root->parent = null;
    tree->root->value = SKY_RADIX_NO_VALUE;
    if (preallocate == 0) {
        return tree;
    }
    /*
     * Preallocation of first nodes : 0, 1, 00, 01, 10, 11, 000, 001, etc.
     * increases TLB hits even if for first lookup iterations.
     * On 32-bit platforms the 7 preallocated bits takes continuous 4K,
     * 8 - 8K, 9 - 16K, etc.  On 64-bit platforms the 6 preallocated bits
     * takes continuous 4K, 7 - 8K, 8 - 16K, etc.  There is no sense to
     * to preallocate more than one page, because further preallocation
     * distributes the only bit per page.  Instead, a random insertion
     * may distribute several bits per page.
     *
     * Thus, by default we preallocate maximum
     *     6 bits on amd64 (64-bit platform and 4K pages)
     *     7 bits on i386 (32-bit platform and 4K pages)
     *     7 bits on sparc64 in 64-bit mode (8K pages)
     *     8 bits on sparc64 in 32-bit mode (8K pages)
     */
    if (preallocate == -0x1) {
        switch (SKY_PAGESIZE / sizeof(sky_radix_node_t)) {
            /* amd64 */
            case 0x80://128
                preallocate = 0x6;
                break;
                /* i386, sparc64 */
            case 0x100://256
                preallocate = 0x7;
                break;
                /* sparc64 in 32-bit mode */
            default:
                preallocate = 0x8;
        }
    }
    mask = 0;
    inc = 0x80000000;
    while (preallocate--) {
        key = 0;
        mask >>= 0x1;
        mask |= 0x80000000;
        do {
            if (!sky_radix32tree_insert(tree, key, mask, SKY_RADIX_NO_VALUE)) {
                return null;
            }
            key += inc;
        } while (key);
        inc >>= 1;
    }
    return tree;
}

sky_bool_t
sky_radix32tree_insert(sky_radix_tree_t *tree, sky_u32_t key, sky_u32_t mask, sky_usize_t value) {
    sky_u32_t bit;
    sky_radix_node_t *node, *next;

    bit = 0x80000000;
    node = tree->root;
    next = tree->root;
    while (bit & mask) {
        if (key & bit) {
            next = node->right;
        } else {
            next = node->left;
        }
        if (!next) {
            break;
        }
        bit >>= 0x1;
        node = next;
    }
    if (next) {
        if (node->value != SKY_RADIX_NO_VALUE) {
            return false; // -1预留
        }
        node->value = value;
        return true;
    }
    while (bit & mask) {
        next = sky_radix_alloc(tree);
        if (!next) {
            return false;
        }
        next->right = null;
        next->left = null;
        next->parent = node;
        next->value = SKY_RADIX_NO_VALUE;
        if (key & bit) {
            node->right = next;
        } else {
            node->left = next;
        }
        bit >>= 0x1;
        node = next;
    }
    node->value = value;
    return true;
}

sky_bool_t
sky_radix32tree_delete(sky_radix_tree_t *tree, sky_u32_t key, sky_u32_t mask) {
    sky_u32_t bit;
    sky_radix_node_t *node;

    bit = 0x80000000;
    node = tree->root;
    while (node && (bit & mask)) {
        if (key & bit) {
            node = node->right;
        } else {
            node = node->left;
        }
        bit >>= 0x1;
    }
    if (!node) {
        return false;
    }
    if (node->right || node->left) {
        if (node->value != SKY_RADIX_NO_VALUE) {
            node->value = SKY_RADIX_NO_VALUE;
            return true;
        }
        return false;
    }
    for (;;) {
        if (node->parent->right == node) {
            node->parent->right = null;
        } else {
            node->parent->left = null;
        }
        node->right = tree->free;
        tree->free = node;
        node = node->parent;
        if (node->right || node->left) {
            break;
        }
        if (node->value != SKY_RADIX_NO_VALUE) {
            break;
        }
        if (!node->parent) {
            break;
        }
    }
    return true;
}

sky_usize_t
sky_radix32tree_find(sky_radix_tree_t *tree, sky_u32_t key) {
    sky_u32_t bit;
    sky_usize_t value;
    sky_radix_node_t *node;

    bit = 0x80000000;
    value = SKY_RADIX_NO_VALUE;
    node = tree->root;
    while (node) {
        if (node->value != SKY_RADIX_NO_VALUE) {
            value = node->value;
        }
        if (key & bit) {
            node = node->right;
        } else {
            node = node->left;
        }
        bit >>= 0x1;
    }
    return value;
}

sky_bool_t
sky_radix128tree_insert(sky_radix_tree_t *tree, sky_uchar_t *key, sky_uchar_t *mask, sky_usize_t value) {
    sky_uchar_t bit;
    sky_usize_t i;
    sky_radix_node_t *node, *next;
    i = 0;
    bit = 0x80;
    node = tree->root;
    next = tree->root;
    while (bit & mask[i]) {
        if (key[i] & bit) {
            next = node->right;
        } else {
            next = node->left;
        }
        if (!next) {
            break;
        }
        bit >>= 0x1;
        node = next;
        if (bit == 0) {
            if (++i == 0x10) {
                break;
            }
            bit = 0x80;
        }
    }
    if (next) {
        if (node->value != SKY_RADIX_NO_VALUE) {
            return false; //-1
        }
        node->value = value;
        return true;
    }
    while (bit & mask[i]) {
        next = sky_radix_alloc(tree);
        if (!next) {
            return false;
        }
        next->right = null;
        next->left = null;
        next->parent = node;
        next->value = SKY_RADIX_NO_VALUE;
        if (key[i] & bit) {
            node->right = next;
        } else {
            node->left = next;
        }
        bit >>= 0x1;
        node = next;
        if (bit == 0) {
            if (++i == 0x10) {
                break;
            }
            bit = 0x80;
        }
    }
    node->value = value;
    return true;
}

sky_bool_t
sky_radix128tree_delete(sky_radix_tree_t *tree, sky_uchar_t *key, sky_uchar_t *mask) {
    sky_uchar_t bit;
    sky_usize_t i;
    sky_radix_node_t *node;
    i = 0;
    bit = 0x80;
    node = tree->root;
    while (node && (bit & mask[i])) {
        if (key[i] & bit) {
            node = node->right;
        } else {
            node = node->left;
        }
        bit >>= 0x1;
        if (bit == 0) {
            if (++i == 0x10) {
                break;
            }
            bit = 0x80;
        }
    }
    if (!node) {
        return false;
    }
    if (node->right || node->left) {
        if (node->value != SKY_RADIX_NO_VALUE) {
            node->value = SKY_RADIX_NO_VALUE;
            return true;
        }
        return false;
    }
    for (;;) {
        if (node->parent->right == node) {
            node->parent->right = null;
        } else {
            node->parent->left = null;
        }
        node->right = tree->free;
        tree->free = node;
        node = node->parent;
        if (node->right || node->left) {
            break;
        }
        if (node->value != SKY_RADIX_NO_VALUE) {
            break;
        }
        if (!node->parent) {
            break;
        }
    }
    return true;
}

sky_usize_t
sky_radix128tree_find(sky_radix_tree_t *tree, sky_uchar_t *key) {
    sky_uchar_t bit;
    sky_usize_t value;
    sky_usize_t i;
    sky_radix_node_t *node;
    i = 0;
    bit = 0x80;
    value = SKY_RADIX_NO_VALUE;
    node = tree->root;
    while (node) {
        if (node->value != SKY_RADIX_NO_VALUE) {
            value = node->value;
        }
        if (key[i] & bit) {
            node = node->right;
        } else {
            node = node->left;
        }
        bit >>= 0x1;
        if (bit == 0) {
            i++;
            bit = 0x80;
        }
    }
    return value;
}

static sky_radix_node_t*
sky_radix_alloc(sky_radix_tree_t *tree) {
    sky_radix_node_t *p;

    if (tree->free) {
        p = tree->free;
        tree->free = tree->free->right;
        return p;
    }
    if (tree->size < sizeof(sky_radix_node_t)) {
        tree->start = sky_pmemalign(tree->pool, SKY_PAGESIZE, SKY_PAGESIZE);
        if (!tree->start) {
            return null;
        }
        tree->size = SKY_PAGESIZE;
    }
    p = (sky_radix_node_t *) tree->start;
    tree->start += sizeof(sky_radix_node_t);
    tree->size -= sizeof(sky_radix_node_t);
    return p;
}
