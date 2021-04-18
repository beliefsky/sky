//
// Created by weijing on 17-11-9.
//

#include "rbtree.h"

static void sky_rbtree_left_rotate(sky_rbtree_node_t* *root, sky_rbtree_node_t* sentinel, sky_rbtree_node_t* node);

static void sky_rbtree_right_rotate(sky_rbtree_node_t* *root, sky_rbtree_node_t* sentinel, sky_rbtree_node_t* node);

void
sky_rbtree_insert(sky_rbtree_t* tree, sky_rbtree_node_t* node) {
    sky_rbtree_node_t* *root, *temp, *sentinel;

    /* a binary tree insert */
    root = &tree->root;
    sentinel = tree->sentinel;
    if (*root == sentinel) {
        node->parent = null;
        node->left = sentinel;
        node->right = sentinel;
        sky_rbt_black(node);
        *root = node;
        return;
    }
    tree->insert(*root, node, sentinel);
    /* re-balance tree */
    while (node != *root && sky_rbt_is_red(node->parent)) {
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
                    sky_rbtree_left_rotate(root, sentinel, node);
                }
                sky_rbt_black(node->parent);
                sky_rbt_red(node->parent->parent);
                sky_rbtree_right_rotate(root, sentinel, node->parent->parent);
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
                    sky_rbtree_right_rotate(root, sentinel, node);
                }
                sky_rbt_black(node->parent);
                sky_rbt_red(node->parent->parent);
                sky_rbtree_left_rotate(root, sentinel, node->parent->parent);
            }
        }
    }
    sky_rbt_black(*root);
}

void
sky_rbtree_delete(sky_rbtree_t* tree, sky_rbtree_node_t* node) {
    sky_uchar_t red;
    sky_rbtree_node_t* *root, *sentinel, *subst, *temp, *w;

    /* a binary tree delete */
    root = &tree->root;
    sentinel = tree->sentinel;
    if (node->left == sentinel) {
        temp = node->right;
        subst = node;
    } else if (node->right == sentinel) {
        temp = node->left;
        subst = node;
    } else {
        subst = sky_rbtree_min(node->right, sentinel);
        temp = subst->right;
    }
    if (subst == *root) {
        *root = temp;
        sky_rbt_black(temp);
        /* DEBUG stuff */
        node->left = null;
        node->right = null;
        node->parent = null;
        node->key = 0x0;
        return;
    }
    red = sky_rbt_is_red(subst);
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
    /* DEBUG stuff */
    node->left = null;
    node->right = null;
    node->parent = null;
    node->key = 0x0;
    if (red) {
        return;
    }
    /* a delete fixup */
    while (temp != *root && sky_rbt_is_black(temp)) {
        if (temp == temp->parent->left) {
            w = temp->parent->right;
            if (sky_rbt_is_red(w)) {
                sky_rbt_black(w);
                sky_rbt_red(temp->parent);
                sky_rbtree_left_rotate(root, sentinel, temp->parent);
                w = temp->parent->right;
            }
            if (sky_rbt_is_black(w->left) && sky_rbt_is_black(w->right)) {
                sky_rbt_red(w);
                temp = temp->parent;
            } else {
                if (sky_rbt_is_black(w->right)) {
                    sky_rbt_black(w->left);
                    sky_rbt_red(w);
                    sky_rbtree_right_rotate(root, sentinel, w);
                    w = temp->parent->right;
                }
                sky_rbt_copy_color(w, temp->parent);
                sky_rbt_black(temp->parent);
                sky_rbt_black(w->right);
                sky_rbtree_left_rotate(root, sentinel, temp->parent);
                temp = *root;
            }
        } else {
            w = temp->parent->left;
            if (sky_rbt_is_red(w)) {
                sky_rbt_black(w);
                sky_rbt_red(temp->parent);
                sky_rbtree_right_rotate(root, sentinel, temp->parent);
                w = temp->parent->left;
            }
            if (sky_rbt_is_black(w->left) && sky_rbt_is_black(w->right)) {
                sky_rbt_red(w);
                temp = temp->parent;
            } else {
                if (sky_rbt_is_black(w->left)) {
                    sky_rbt_black(w->right);
                    sky_rbt_red(w);
                    sky_rbtree_left_rotate(root, sentinel, w);
                    w = temp->parent->left;
                }
                sky_rbt_copy_color(w, temp->parent);
                sky_rbt_black(temp->parent);
                sky_rbt_black(w->left);
                sky_rbtree_right_rotate(root, sentinel, temp->parent);
                temp = *root;
            }
        }
    }
    sky_rbt_black(temp);
}

void
sky_rbtree_insert_value(sky_rbtree_node_t* temp, sky_rbtree_node_t* node, sky_rbtree_node_t* sentinel) {
    sky_rbtree_node_t* *p;

    for (;;) {
        p = (node->key < temp->key) ? &temp->left : &temp->right;
        if (*p == sentinel) {
            break;
        }
        temp = *p;
    }
    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    sky_rbt_red(node);
}

sky_rbtree_node_t*
sky_rbtree_next(sky_rbtree_t* tree, sky_rbtree_node_t* node) {
    sky_rbtree_node_t* root, *sentinel, *parent;

    sentinel = tree->sentinel;
    if (node->right != sentinel) {
        return sky_rbtree_min(node->right, sentinel);
    }
    root = tree->root;
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

//=============================================================
static sky_inline void
sky_rbtree_left_rotate(sky_rbtree_node_t* *root, sky_rbtree_node_t* sentinel, sky_rbtree_node_t* node) {
    sky_rbtree_node_t* temp;

    temp = node->right;
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
sky_rbtree_right_rotate(sky_rbtree_node_t* *root, sky_rbtree_node_t* sentinel, sky_rbtree_node_t* node) {
    sky_rbtree_node_t* temp;

    temp = node->left;
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