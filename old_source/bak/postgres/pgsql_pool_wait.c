//
// Created by beliefsky on 2023/7/22.
//
#include <io/postgres/pgsql_pool_wait.h>

static void pgsql_connect_cb(sky_pgsql_conn_t *conn, void *data);

static void pgsql_exec_cb(sky_pgsql_conn_t *conn, sky_pgsql_result_t *result, void *data);

sky_api sky_pgsql_conn_t *
sky_pgsql_pool_wait_get(
        sky_pgsql_pool_t *const pg_pool,
        sky_pool_t *const pool,
        sky_sync_wait_t *const wait
) {
    sky_sync_wait_yield_before(wait);
    sky_pgsql_pool_get(pg_pool, pool, pgsql_connect_cb, wait);

    return sky_sync_wait_yield(wait);
}

sky_api sky_pgsql_result_t *
sky_pgsql_wait_exec(
        sky_pgsql_conn_t *const conn,
        sky_sync_wait_t *const wait,
        const sky_str_t *const cmd,
        sky_pgsql_params_t *const params,
        const sky_u16_t param_len
) {
    sky_sync_wait_yield_before(wait);
    sky_pgsql_exec(conn, pgsql_exec_cb, wait, cmd, params, param_len);

    return sky_sync_wait_yield(wait);
}

static void
pgsql_connect_cb(sky_pgsql_conn_t *const conn, void *const data) {
    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, conn);
}

static void
pgsql_exec_cb(sky_pgsql_conn_t *const conn, sky_pgsql_result_t *const result, void *const data) {
    (void) conn;
    sky_sync_wait_t *const wait = data;
    sky_sync_wait_resume(wait, result);
}
