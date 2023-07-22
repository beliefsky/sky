//
// Created by beliefsky on 2023/7/20.
//

#ifndef SKY_PGSQL_COMMON_H
#define SKY_PGSQL_COMMON_H

#include <io/postgres/pgsql_pool.h>
#include <core/queue.h>
#include <io/tcp.h>

struct sky_pgsql_conn_s {
    sky_tcp_t tcp;
    sky_queue_t link;
    sky_timer_wheel_entry_t timer;
    sky_pgsql_pool_t *pg_pool;
    sky_usize_t offset;
    sky_pool_t *current_pool;
    union {
        sky_pgsql_conn_pt conn_cb;
        sky_pgsql_exec_pt exec_cb;
    };
    void *cb_data;

    void *data;
};

typedef struct {
    sky_queue_t link;
    sky_pool_t *pool;
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
    sky_inet_addr_t address;
    sky_timer_wheel_entry_t timer;
    sky_str_t username;
    sky_str_t password;
    sky_str_t connect_info;
    pgsql_block_t *current_block;
    sky_event_loop_t *ev_loop;
    sky_u32_t conn_num;
    sky_u32_t free_conn_num;
    sky_bool_t destroy;
};

void pgsql_auth(sky_pgsql_conn_t *conn);

#endif //SKY_PGSQL_COMMON_H
