//
// Created by beliefsky on 2023/7/22.
//

#ifndef SKY_PGSQL_POOL_WAIT_H
#define SKY_PGSQL_POOL_WAIT_H

#include "./pgsql_pool.h"
#include "../sync_wait.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_pgsql_conn_t *sky_pgsql_pool_wait_get(
        sky_pgsql_pool_t *pg_pool,
        sky_pool_t *pool,
        sky_sync_wait_t *wait
);

sky_pgsql_result_t *sky_pgsql_wait_exec(
        sky_pgsql_conn_t *conn,
        sky_sync_wait_t *wait,
        const sky_str_t *cmd,
        sky_pgsql_params_t *params,
        sky_u16_t param_len
);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_PGSQL_POOL_WAIT_H
