//
// Created by edz on 2021/11/1.
//

#ifndef SKY_TCP_LISTENER_H
#define SKY_TCP_LISTENER_H

#include "../event/event_loop.h"
#include "../core/coro.h"
#include "tcp.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_tcp_listener_s sky_tcp_listener_t;
typedef struct sky_tcp_listener_writer_s sky_tcp_listener_writer_t;
typedef struct sky_tcp_listener_reader_s sky_tcp_listener_reader_t;
typedef struct sky_tcp_listener_conf_s sky_tcp_listener_conf_t;

typedef sky_bool_t (*sky_tcp_listener_opts_pt)(sky_tcp_t *conn, void *data);
typedef void (*sky_tcp_listener_close_pt)(sky_tcp_listener_t *listener, void *data);


struct sky_tcp_listener_writer_s {
    sky_queue_t link;
    sky_ev_t *ev;
    sky_coro_t *coro;
    sky_tcp_listener_t *client;
    sky_defer_t *defer;
};

#define sky_tcp_listener_writer_event(_writer) (_writer)->ev
#define sky_tcp_listener_writer_coro(_writer) (_writer)->coro

struct sky_tcp_listener_conf_s {
    sky_tcp_ctx_t *ctx;
    sky_inet_addr_t *address;
    void *data;
    sky_coro_func_t run;
    sky_tcp_listener_opts_pt options;
    sky_tcp_listener_close_pt close;
    sky_u32_t address_len;
    sky_u32_t timeout;
    sky_bool_t reconnect;
};


sky_tcp_listener_t *sky_tcp_listener_create(
        sky_event_loop_t *loop,
        sky_coro_switcher_t *switcher,
        const sky_tcp_listener_conf_t *conf
);


sky_bool_t sky_tcp_listener_bind(
        sky_tcp_listener_t *listener,
        sky_tcp_listener_writer_t *writer,
        sky_ev_t *event,
        sky_coro_t *coro
);

sky_bool_t sky_tcp_listener_bind_self(sky_tcp_listener_reader_t *reader, sky_tcp_listener_writer_t *writer);

sky_usize_t sky_tcp_listener_write(sky_tcp_listener_writer_t *writer, const sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_listener_write_all(sky_tcp_listener_writer_t *writer, const sky_uchar_t *data, sky_usize_t size);

sky_isize_t sky_tcp_listener_write_nowait(sky_tcp_listener_writer_t *writer, const sky_uchar_t *data, sky_usize_t size);

void sky_tcp_listener_unbind(sky_tcp_listener_writer_t *writer);

void *sky_tcp_listener_reader_data(sky_tcp_listener_reader_t *reader);

sky_ev_t *sky_tcp_listener_reader_event(sky_tcp_listener_reader_t *reader);

sky_coro_t *sky_tcp_listener_reader_coro(sky_tcp_listener_reader_t *reader);

sky_usize_t sky_tcp_listener_read(sky_tcp_listener_reader_t *reader, sky_uchar_t *data, sky_usize_t size);

sky_bool_t sky_tcp_listener_read_all(sky_tcp_listener_reader_t *reader, sky_uchar_t *data, sky_usize_t size);

void sky_tcp_listener_destroy(sky_tcp_listener_t *listener);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TCP_LISTENER_H
