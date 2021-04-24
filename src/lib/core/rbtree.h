//
// Created by weijing on 17-11-9.
//

#ifndef SKY_RBTREE_H
#define SKY_RBTREE_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_rbtree_node_s sky_rbtree_node_t;

/**
  * 红黑树
**/

struct sky_rbtree_node_s {
    sky_usize_t key;                /*无符号整形的关键字*/
    sky_rbtree_node_t *left;              /*左子节点*/
    sky_rbtree_node_t *right;             /*右子节点*/
    sky_rbtree_node_t *parent;            /*父节点*/
    sky_uchar_t color;              /*节点的颜色，0表示黑色，1表示红色*/
};

typedef struct sky_rbtree_s sky_rbtree_t;

/*如果不希望出现具有相同key关键字的不同节点再向红黑树添加时出现覆盖原节点的情况就需要实现自有的ngx_rbtree_insert_bt方法*/
typedef void (*sky_rbtree_insert_pt)(sky_rbtree_node_t *root, sky_rbtree_node_t *node, sky_rbtree_node_t *sentinel);

struct sky_rbtree_s {
    sky_rbtree_node_t *root;          /*指向树的根节点*/
    sky_rbtree_node_t *sentinel;      /*指向NIL哨兵节点*/
    /**
     * 表示红黑树添加元素的函数指针，它决定在添加新节点时的行为究竟是替换还是新增;
     * 红黑树内部插入函数用于将待插入的节点放在合适的NIL叶子节点处
    **/

    sky_rbtree_insert_pt insert;
};


#define sky_rbtree_init(tree, s, i)   \
    sky_rbtree_sentinel_init(s);    \
    (tree)->root = s;               \
    (tree)->sentinel = s;           \
    (tree)->insert = i

void sky_rbtree_insert(sky_rbtree_t *tree, sky_rbtree_node_t *node);

void sky_rbtree_delete(sky_rbtree_t *tree, sky_rbtree_node_t *node);

void sky_rbtree_insert_value(sky_rbtree_node_t *root, sky_rbtree_node_t *node, sky_rbtree_node_t *sentinel);

sky_rbtree_node_t *sky_rbtree_next(sky_rbtree_t *tree, sky_rbtree_node_t *node);


#define sky_rbt_red(node)               ((node)->color = 0x1)
#define sky_rbt_black(node)             ((node)->color = 0x0)
#define sky_rbt_is_red(node)            ((node)->color)
#define sky_rbt_is_black(node)          (!sky_rbt_is_red(node))
#define sky_rbt_copy_color(n1, n2)      ((n1)->color = (n2)->color)
/* a sentinel must be black */
#define sky_rbtree_sentinel_init(node)  sky_rbt_black(node)

static sky_inline sky_rbtree_node_t*
sky_rbtree_min(sky_rbtree_node_t *node, sky_rbtree_node_t *sentinel) {
    while (node->left != sentinel) {
        node = node->left;
    }
    return node;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_RBTREE_H
