//
// Created by beliefsky on 2023/7/20.
//

#ifndef SKY_PGSQL_POOL_H
#define SKY_PGSQL_POOL_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_pgsql_pool_s sky_pgsql_pool_t;
typedef struct sky_pgsql_conn_s sky_pgsql_conn_t;
typedef void (*sky_pgsql_conn_pt)(sky_pgsql_conn_t *conn, void *data);

sky_pgsql_pool_t *sky_pgsql_pool_create();

void sky_pgsql_pool_get(sky_pgsql_pool_t *pool, sky_pgsql_conn_pt cb, void *data);

void sky_pgsql_conn_release(sky_pgsql_conn_t *conn);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_PGSQL_POOL_H
