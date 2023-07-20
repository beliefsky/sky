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
    sky_pgsql_pool_t *pool;
};

#endif //SKY_PGSQL_COMMON_H
