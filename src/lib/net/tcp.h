//
// Created by weijing on 18-11-6.
//

#ifndef SKY_TCP_H
#define SKY_TCP_H

#include "../event/event_loop.h"
#include "../core/palloc.h"
#include "../core/string.h"
#include <unistd.h>

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_pipe_s sky_tcp_pipe_t;

typedef sky_event_t *(*sky_tcp_accept_cb_pt)(sky_event_loop_t *loop, sky_int32_t fd, void *data);

typedef struct {
    sky_bool_t reuse_port;
    sky_uint16_t pipe_num;
    sky_int32_t pipe_fd;
    sky_int32_t timeout;
    sky_str_t host;
    sky_str_t port;
    sky_tcp_accept_cb_pt run;
    void *data;

} sky_tcp_conf_t;

struct sky_tcp_pipe_s {
    sky_uint16_t num;
    sky_int32_t *read_fd;
};

/**
 * 用于创建tcp 通信桥接器，用于不支持reuse port功能的补偿
 * @param pool 内存池
 * @param conf 配置
 * @return 通信流
 */
sky_tcp_pipe_t *sky_tcp_pipe_create(sky_pool_t *pool, sky_tcp_conf_t *conf);

void sky_tcp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool,
                             sky_tcp_conf_t *conf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TCP_H
