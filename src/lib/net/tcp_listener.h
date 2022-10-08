//
// Created by edz on 2021/11/1.
//

#ifndef SKY_TCP_LISTENER_H
#define SKY_TCP_LISTENER_H

#include "../event/event_loop.h"
#include "../core/coro.h"
#include "inet.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_listener_s sky_tcp_listener_t;
typedef struct sky_tcp_listener_conf_s sky_tcp_listener_conf_t;
typedef struct sky_tcp_listener_stream_s sky_tcp_listener_stream_t;

typedef void (*sky_tcp_listener_close_pt)(sky_tcp_listener_t *listener, void *data);

struct sky_tcp_listener_conf_s {
    sky_inet_address_t *address;
    void *data;
    sky_coro_func_t run;
    sky_tcp_listener_close_pt close;
    sky_u32_t address_len;
    sky_i32_t keep_alive;
    sky_i32_t timeout;
    sky_bool_t reconnect;
};

struct sky_tcp_listener_stream_s {
    sky_queue_t link;
    sky_uchar_t *data;
    sky_u32_t size;
};


sky_tcp_listener_t *sky_tcp_listener_create(
        sky_event_loop_t *loop,
        sky_coro_switcher_t *switcher,
        const sky_tcp_listener_conf_t *conf
);

sky_tcp_listener_stream_t *sky_tcp_listener_get_stream(sky_tcp_listener_t *listener, sky_u32_t need_size);

sky_bool_t sky_tcp_listener_write_packet(sky_tcp_listener_t *listener);

sky_usize_t sky_tcp_listener_read(sky_tcp_listener_t *listener, sky_uchar_t *data, sky_usize_t size);

void sky_tcp_listener_read_all(sky_tcp_listener_t *listener, sky_uchar_t *data, sky_usize_t size);

void sky_tcp_listener_destroy(sky_tcp_listener_t *listener);

static sky_inline sky_uchar_t *
sky_tcp_listener_stream_buff(sky_tcp_listener_stream_t *stream) {
    return stream->data + stream->size;
}

static sky_inline void
sky_tcp_listener_stream_set_n(sky_tcp_listener_stream_t *stream, sky_u32_t n) {
    stream->size += n;
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_LISTENER_H
