//
// Created by beliefsky on 2023/7/20.
//

#ifndef SKY_PGSQL_POOL_H
#define SKY_PGSQL_POOL_H

#if defined(__cplusplus)
extern "C" {
#endif

#include "../../core/string.h"
#include "../../core/palloc.h"
#include "../event_loop.h"

typedef struct sky_pgsql_pool_s sky_pgsql_pool_t;
typedef struct sky_pgsql_conn_s sky_pgsql_conn_t;

typedef void (*sky_pgsql_conn_pt)(sky_pgsql_conn_t *conn, void *data);

typedef struct {
    sky_str_t username;
    sky_str_t password;
    sky_str_t database;
    sky_u32_t connection_size;
    sky_inet_addr_t address;
} sky_pgsql_conf_t;

sky_pgsql_pool_t *sky_pgsql_pool_create(sky_event_loop_t *ev_loop, const sky_pgsql_conf_t *conf);

void sky_pgsql_pool_get(sky_pgsql_pool_t *pg_pool, sky_pool_t *pool, sky_pgsql_conn_pt cb, void *data);

void sky_pgsql_conn_release(sky_pgsql_conn_t *conn);

void sky_pgsql_pool_destroy(sky_pgsql_pool_t *pg_pool);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_PGSQL_POOL_H
