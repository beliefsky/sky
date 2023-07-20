//
// Created by beliefsky on 2023/7/20.
//

#include "pgsql_common.h"
#include <core/memory.h>

#define BLOCK_TASK_N 1024

typedef struct {
    sky_queue_t link;
    sky_pgsql_conn_pt cb;
    void *data;
} pgsql_task_t;

typedef struct {
    sky_queue_t link;
    sky_usize_t free_num;
    pgsql_task_t tasks[];
} pgsql_block_t;


struct sky_pgsql_pool_s {
    sky_queue_t free_conns;
    sky_queue_t tasks;
    sky_queue_t free_tasks;
    sky_queue_t blocks;
    pgsql_block_t *current_block;

    sky_u32_t conn_num;
    sky_u32_t conn_min;
    sky_u32_t conn_max;
};


static pgsql_task_t *pgsql_task_get(sky_pgsql_pool_t * pool);


sky_pgsql_pool_t *
sky_pgsql_pool_create() {
    sky_pgsql_pool_t *const pool = sky_malloc(sizeof(sky_pgsql_pool_t));
    sky_queue_init(&pool->free_conns);
    sky_queue_init(&pool->tasks);
    sky_queue_init(&pool->free_tasks);
    sky_queue_init(&pool->blocks);
    pool->current_block = null;

    return pool;
}

void
sky_pgsql_pool_get(sky_pgsql_pool_t *pool, sky_pgsql_conn_pt cb, void *data) {
    sky_queue_t *const item = sky_queue_next(&pool->free_conns);
    if (item != &pool->free_conns) {
        sky_queue_remove(item);
        sky_pgsql_conn_t *const conn = sky_type_convert(item, sky_pgsql_conn_t, link);
        cb(conn, data);
        return;
    }
    pgsql_task_t *const task = pgsql_task_get(pool);
    if (sky_unlikely(!task)) {
        cb(null, data);
        return;
    }
    task->cb = cb;
    task->data = data;
    sky_queue_insert_prev(&pool->tasks, &task->link);
}

void
sky_pgsql_conn_release(sky_pgsql_conn_t *const conn) {
    sky_pgsql_pool_t *const pool = conn->pool;
    sky_queue_t *const item = sky_queue_next(&pool->tasks);
    if (item == &pool->tasks) {
        // 此处需要加入回收列队
        sky_queue_insert_next(&pool->free_conns, &conn->link);
        return;
    }
    sky_queue_remove(item);

    pgsql_task_t *const task = sky_type_convert(item, pgsql_task_t, link);
    sky_queue_insert_prev(&pool->free_tasks, &task->link);
    task->cb(conn, task->data);
}

static sky_inline pgsql_task_t *
pgsql_task_get(sky_pgsql_pool_t *const pool) {
    sky_queue_t *const item = sky_queue_next(&pool->free_tasks);
    if (item != &pool->free_tasks) {
        sky_queue_remove(item);
        return sky_type_convert(item, pgsql_task_t, link);
    }

    pgsql_block_t *block = pool->current_block;
    if (!block || block->free_num) {
        block = sky_malloc(sizeof(pgsql_block_t) + (sizeof(pgsql_task_t) * BLOCK_TASK_N));
        if (sky_unlikely(!block)) {
            return null;
        }
        sky_queue_init_node(&block->link);
        sky_queue_insert_prev(&pool->blocks, &block->link);
        block->free_num = BLOCK_TASK_N;
        pool->current_block = block;
    }

    return block->tasks + (--block->free_num);
}
