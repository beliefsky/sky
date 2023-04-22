//
// Created by weijing on 2019/12/21.
//

#ifndef SKY_HTTP_EXTEND_REDIS_POOL_H
#define SKY_HTTP_EXTEND_REDIS_POOL_H

#include "../../redis/redis_pool.h"
#include "../http_request.h"

#if defined(__cplusplus)
extern "C" {
#endif

static sky_inline sky_redis_conn_t*
sky_http_ex_redis_conn_get(sky_redis_pool_t *redis_pool, sky_http_request_t *req) {
    return sky_redis_conn_get(redis_pool, req->pool, sky_tcp_ev(&req->conn->tcp));
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_EXTEND_REDIS_POOL_H
