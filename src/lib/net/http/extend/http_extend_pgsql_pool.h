//
// Created by weijing on 2019/12/9.
//

#ifndef SKY_HTTP_EXTEND_PGSQL_POOL_H
#define SKY_HTTP_EXTEND_PGSQL_POOL_H

#include "../../clients/pgsql_pool.h"
#include "../http_request.h"

#if defined(__cplusplus)
extern "C" {
#endif

static sky_inline sky_pg_conn_t *
sky_http_ex_pgsql_conn_get(sky_pg_pool_t *pg_pool, sky_http_request_t *req) {
    return sky_pgsql_conn_get(pg_pool, req->pool, &req->conn->ev, req->conn->coro);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_EXTEND_PGSQL_POOL_H
