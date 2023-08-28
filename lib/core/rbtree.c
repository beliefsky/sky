//
// Created by beliefsky on 2023/6/3.
//

#include <core/rbtree.h>

#define sky_rbt_red(_node)               ((_node)->color = true)
#define sky_rbt_black(_node)             ((_node)->color = false)
#define sky_rbt_is_red(_node)            ((_node)->color)
#define sky_rbt_is_black(_node)          (!sky_rbt_is_red(_node))
#define sky_rbt_copy_color(_n1, _n2)      ((_n1)->color = (_n2)->color)

static sky_rb_node_t *sky_rb_tree_min(const sky_rb_tree_t *tree, sky_rb_node_t *node);

static sky_rb_node_t *sky_rb_tree_max(const sky_rb_tree_t *tree, sky_rb_node_t *node);

static void rb_tree_left_rotate(sky_rb_node_t **root, const sky_rb_node_t *sentinel, sky_rb_node_t *node);

static void rb_tree_right_rotate(sky_rb_node_t **root, const sky_rb_node_t *sentinel, sky_rb_node_t *node);

sky_api sky_rb_node_t *
sky_rb_tree_prev(sky_rb_tree_t *const tree, sky_rb_node_t *node) {
    if (node->left != &tree->sentinel) {
        return sky_rb_tree_max(tree, node->left);
    }
    sky_rb_node_t *const root = tree->root;
    sky_rb_node_t *parent;
    for (;;) {
        parent = node->parent;
        if (node == root) {
            return null;
        }
        if (node == parent->right) {
            return parent;
        }
        node = parent;
    }
}

sky_api sky_rb_node_t *
sky_rb_tree_next(sky_rb_tree_t *const tree, sky_rb_node_t *node) {
    if (node->right != &tree->sentinel) {
        return sky_rb_tree_min(tree, node->right);
    }
    sky_rb_node_t *const root = tree->root;
    sky_rb_node_t *parent;
    for (;;) {
        parent = node->parent;
        if (node == root) {
            return null;
        }
        if (node == parent->left) {
            return parent;
        }
        node = parent;
    }
}

sky_api sky_rb_node_t *
sky_rb_tree_first(sky_rb_tree_t *const tree) {
    if (sky_rb_tree_is_empty(tree)) {
        return null;
    }
    return sky_rb_tree_min(tree, tree->root);
}

sky_api sky_rb_node_t *
sky_rb_tree_last(sky_rb_tree_t *tree) {
    if (sky_rb_tree_is_empty(tree)) {
        return null;
    }
    return sky_rb_tree_max(tree, tree->root);
}

sky_api void
sky_rb_tree_link(sky_rb_tree_t *const tree, sky_rb_node_t * node, sky_rb_node_t *const parent) {
    node->parent = parent;
    node->left = &tree->sentinel;
    node->right = &tree->sentinel;
    if (!parent) {
        sky_rbt_black(node);
        tree->root = node;
        return;
    }
    sky_rbt_red(node);

    sky_rb_node_t *temp;
    while (node != tree->root && sky_rbt_is_red(node->parent)) {
        if (node->parent == node->parent->parent->left) {
            temp = node->parent->parent->right;
            if (sky_rbt_is_red(temp)) {
                sky_rbt_black(node->parent);
                sky_rbt_black(temp);
                sky_rbt_red(node->parent->parent);
                node = node->parent->parent;
            } else {
                if (node == node->parent->right) {
                    node = node->parent;
                    rb_tree_left_rotate(&tree->root, &tree->sentinel, node);
                }
                sky_rbt_black(node->parent);
                sky_rbt_red(node->parent->parent);
                rb_tree_right_rotate(&tree->root, &tree->sentinel, node->parent->parent);
            }
        } else {
            temp = node->parent->parent->left;
            if (sky_rbt_is_red(temp)) {
                sky_rbt_black(node->parent);
                sky_rbt_black(temp);
                sky_rbt_red(node->parent->parent);
                node = node->parent->parent;
            } else {
                if (node == node->parent->left) {
                    node = node->parent;
                    rb_tree_right_rotate(&tree->root, &tree->sentinel, node);
                }
                sky_rbt_black(node->parent);
                sky_rbt_red(node->parent->parent);
                rb_tree_left_rotate(&tree->root, &tree->sentinel, node->parent->parent);
            }
        }
    }
    sky_rbt_black(tree->root);
}


sky_api void
sky_rb_tree_del(sky_rb_tree_t *const tree, sky_rb_node_t *const node) {
    /* a binary tree delete */
    sky_rb_node_t **const root = &tree->root;
    sky_rb_node_t *sentinel = &tree->sentinel;

    sky_rb_node_t *temp, *subst;
    if (node->left == sentinel) {
        temp = node->right;
        subst = node;
    } else if (node->right == sentinel) {
        temp = node->left;
        subst = node;
    } else {
        subst = sky_rb_tree_min(tree, node->right);
        temp = subst->right;
    }
    if (subst == *root) {
        *root = temp;
        sky_rbt_black(temp);
        node->left = null;
        node->right = null;
        node->parent = null;
        return;
    }
    const sky_bool_t red = sky_rbt_is_red(subst);
    if (subst == subst->parent->left) {
        subst->parent->left = temp;
    } else {
        subst->parent->right = temp;
    }
    if (subst == node) {
        temp->parent = subst->parent;
    } else {
        temp->parent = subst->parent == node ? subst : subst->parent;

        subst->left = node->left;
        subst->right = node->right;
        subst->parent = node->parent;
        sky_rbt_copy_color(subst, node);
        if (node == *root) {
            *root = subst;
        } else {
            if (node == node->parent->left) {
                node->parent->left = subst;
            } else {
                node->parent->right = subst;
            }
        }
        if (subst->left != sentinel) {
            subst->left->parent = subst;
        }
        if (subst->right != sentinel) {
            subst->right->parent = subst;
        }
    }
    node->left = null;
    node->right = null;
    node->parent = null;
    if (red) {
        return;
    }
    /* a delete fixup */
    sky_rb_node_t *w;
    while (temp != *root && sky_rbt_is_black(temp)) {
        if (temp == temp->parent->left) {
            w = temp->parent->right;
            if (sky_rbt_is_red(w)) {
                sky_rbt_black(w);
                sky_rbt_red(temp->parent);
                rb_tree_left_rotate(root, sentinel, temp->parent);
                w = temp->parent->right;
            }
            if (sky_rbt_is_black(w->left) && sky_rbt_is_black(w->right)) {
                sky_rbt_red(w);
                temp = temp->parent;
            } else {
                if (sky_rbt_is_black(w->right)) {
                    sky_rbt_black(w->left);
                    sky_rbt_red(w);
                    rb_tree_right_rotate(root, sentinel, w);
                    w = temp->parent->right;
                }
                sky_rbt_copy_color(w, temp->parent);
                sky_rbt_black(temp->parent);
                sky_rbt_black(w->right);
                rb_tree_left_rotate(root, sentinel, temp->parent);
                temp = *root;
            }
        } else {
            w = temp->parent->left;
            if (sky_rbt_is_red(w)) {
                sky_rbt_black(w);
                sky_rbt_red(temp->parent);
                rb_tree_right_rotate(root, sentinel, temp->parent);
                w = temp->parent->left;
            }
            if (sky_rbt_is_black(w->left) && sky_rbt_is_black(w->right)) {
                sky_rbt_red(w);
                temp = temp->parent;
            } else {
                if (sky_rbt_is_black(w->left)) {
                    sky_rbt_black(w->right);
                    sky_rbt_red(w);
                    rb_tree_left_rotate(root, sentinel, w);
                    w = temp->parent->left;
                }
                sky_rbt_copy_color(w, temp->parent);
                sky_rbt_black(temp->parent);
                sky_rbt_black(w->left);
                rb_tree_right_rotate(root, sentinel, temp->parent);
                temp = *root;
            }
        }
    }
    sky_rbt_black(temp);
}


//=============================================================

static sky_inline sky_rb_node_t *
sky_rb_tree_min(const sky_rb_tree_t *const tree, sky_rb_node_t *node) {
    while (node->left != &tree->sentinel) {
        node = node->left;
    }
    return node;
}

static sky_inline sky_rb_node_t *
sky_rb_tree_max(const sky_rb_tree_t *const tree, sky_rb_node_t *node) {
    while (node->right != &tree->sentinel) {
        node = node->right;
    }
    return node;
}


static sky_inline void
rb_tree_left_rotate(
        sky_rb_node_t **const root,
        const sky_rb_node_t *const sentinel,
        sky_rb_node_t *const node
) {
    sky_rb_node_t *const temp = node->right;

    node->right = temp->left;
    if (temp->left != sentinel) {
        temp->left->parent = node;
    }
    temp->parent = node->parent;
    if (node == *root) {
        *root = temp;
    } else if (node == node->parent->left) {
        node->parent->left = temp;
    } else {
        node->parent->right = temp;
    }
    temp->left = node;
    node->parent = temp;
}

static sky_inline void
rb_tree_right_rotate(
        sky_rb_node_t **const root,
        const sky_rb_node_t *const sentinel,
        sky_rb_node_t *const node
) {
    sky_rb_node_t *const temp = node->left;

    node->left = temp->right;
    if (temp->right != sentinel) {
        temp->right->parent = node;
    }
    temp->parent = node->parent;
    if (node == *root) {
        *root = temp;
    } else if (node == node->parent->right) {
        node->parent->right = temp;
    } else {
        node->parent->left = temp;
    }
    temp->right = node;
    node->parent = temp;
}