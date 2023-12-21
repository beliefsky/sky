//
// Created by beliefsky on 2023/6/3.
//

#ifndef SKY_RBTREE_H
#define SKY_RBTREE_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct sky_rb_tree_s sky_rb_tree_t;
typedef struct sky_rb_node_s sky_rb_node_t;

struct sky_rb_node_s {
    sky_rb_node_t *parent;
    sky_rb_node_t *left;
    sky_rb_node_t *right;
    sky_bool_t color;
};

struct sky_rb_tree_s {
    sky_rb_node_t sentinel;
    sky_rb_node_t *root;
};

sky_rb_node_t *sky_rb_tree_prev(sky_rb_tree_t *tree, sky_rb_node_t *node);

sky_rb_node_t *sky_rb_tree_next(sky_rb_tree_t *tree, sky_rb_node_t *node);

sky_rb_node_t *sky_rb_tree_first(sky_rb_tree_t *tree);

sky_rb_node_t *sky_rb_tree_last(sky_rb_tree_t *tree);

void sky_rb_tree_link(sky_rb_tree_t *tree, sky_rb_node_t *node, sky_rb_node_t *parent);

void sky_rb_tree_del(sky_rb_tree_t *tree, sky_rb_node_t *node);

static sky_inline void
sky_rb_tree_init(sky_rb_tree_t *const tree) {
    tree->sentinel.color = false;
    tree->root = &tree->sentinel;
}

static sky_inline sky_bool_t
sky_rb_tree_is_empty(const sky_rb_tree_t *const tree) {
    return tree->root == &tree->sentinel;
}

/**

static void
rb_tree_insert(sky_rb_tree_t *tree, sky_rb_node_t *node) {
    if (sky_rb_tree_is_empty(tree)) {
        sky_rb_tree_link(tree, node, null);
        return;
    }

    sky_rb_node_t **p, *temp = tree->root;

    for (;;) {
        p = cmp(node, temp) < 0 ? &temp->left : &temp->right;
        if (*p == &tree->sentinel) {
            *p = node;
            sky_rb_tree_link(tree, node, temp);
            return;
        }
        temp = *p;
    }
}

sky_rb_node_t *
rb_tree_get(sky_rb_tree_t *tree, sky_u32_t key) {
    sky_rb_node_t *node = tree->root;
    sky_i32_t r;

    while (node != &tree->sentinel) {
        r = cmp(node, key);
        if (!r) {
          return node;
        }

        node = r > 0 ? node->left : node->right;
    }

    return null;
}

**/

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_RBTREE_H
