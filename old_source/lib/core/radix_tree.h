//
// Created by weijing on 17-11-12.
//

#ifndef SKY_RADIX_TREE_H
#define SKY_RADIX_TREE_H

#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif


#define SKY_RADIX_NO_VALUE   (sky_usize_t) -1
typedef struct sky_radix_node_s sky_radix_node_t;

//基数树的节点
struct sky_radix_node_s {
    sky_radix_node_t *right;//右子指针
    sky_radix_node_t *left;//左子指针
    sky_radix_node_t *parent;//父节点指针
    sky_usize_t value;//指向存储数据的指针
};
typedef struct {
    sky_radix_node_t *root;//根节点
    sky_pool_t *pool;//内存池，负责分配内存
    sky_radix_node_t *free;//回收释放的节点，在添加新节点时，会首先查看free中是否有空闲可用的节点
    sky_uchar_t *start;//已分配内存中还未使用内存的首地址
    sky_usize_t size;//已分配内存内中还未使用内存的大小
} sky_radix_tree_t;

//创建基数树，preallocate是预分配节点的个数
sky_radix_tree_t *sky_radix_tree_create(sky_pool_t *pool, sky_isize_t preallocate);

//根据key值和掩码向基数树中插入value,返回值可能是OK 1,ERROR 0, BUSY -1
sky_bool_t sky_radix32tree_insert(sky_radix_tree_t *tree, sky_u32_t key, sky_u32_t mask, sky_usize_t value);

//根据key值和掩码删除节点（value的值）
sky_bool_t sky_radix32tree_delete(sky_radix_tree_t *tree, sky_u32_t key, sky_u32_t mask);

//根据key值在基数树中查找返回value数据
sky_usize_t sky_radix32tree_find(sky_radix_tree_t *tree, sky_u32_t key);

sky_bool_t sky_radix128tree_insert(sky_radix_tree_t *tree, sky_uchar_t *key, sky_uchar_t *mask, sky_usize_t value);

sky_bool_t sky_radix128tree_delete(sky_radix_tree_t *tree, sky_uchar_t *key, sky_uchar_t *mask);

sky_usize_t sky_radix128tree_find(sky_radix_tree_t *tree, sky_uchar_t *key);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_RADIX_TREE_H
