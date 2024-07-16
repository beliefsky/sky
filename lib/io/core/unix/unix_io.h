//
// Created by weijing on 2024/3/7.
//

#ifndef SKY_UNIX_IO_H
#define SKY_UNIX_IO_H

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include <io/ev_loop.h>
#include <core/memory.h>
#include <signal.h>

#if sky_has_include(<liburing.h>)

#define EVENT_USE_URING

#include <liburing.h>


#elif sky_has_include(<sys/epoll.h>)

#define EVENT_USE_EPOLL
#define EV_LOOP_USE_SELECTOR

#include <sys/epoll.h>

#elif sky_has_include(<sys/event.h>)

#define EVENT_USE_KQUEUE
#define EV_LOOP_USE_SELECTOR

#include <sys/event.h>

#else

//#error Unsupported platform.

#endif

#define EV_TYPE_TCP_SER     SKY_U32(0)
#define EV_TYPE_TCP_CLI     SKY_U32(1)
#define EV_TYPE_UDP         SKY_U32(2)
#define EV_TYPE_FS          SKY_U32(3)

#define EV_TYPE_MASK        SKY_U32(0x0000000F)


#ifdef EVENT_USE_URING

#define EV_REQ_CLOSE            SKY_U32(0)
#define EV_REQ_TCP_SER_OPEN     SKY_U32(1)
#define EV_REQ_TCP_ACCEPT       SKY_U32(2)
#define EV_REQ_TCP_SER_CLOSE    SKY_U32(3)

#define EV_REQ_TCP_CLI_OPEN     SKY_U32(4)
#define EV_REQ_TCP_CONNECT      SKY_U32(5)
#define EV_REQ_TCP_WRITE        SKY_U32(6)
#define EV_REQ_TCP_READ         SKY_U32(7)
#define EV_REQ_TCP_SENDFILE     SKY_U32(8)
#define EV_REQ_TCP_CLI_SHUTDOWN SKY_U32(9)
#define EV_REQ_TCP_CLI_CLOSE    SKY_U32(10)

#define EV_REQ_FS_OPEN          SKY_U32(11)
#define EV_REQ_FS_WRITE         SKY_U32(12)
#define EV_REQ_FS_READ          SKY_U32(13)
#define EV_REQ_FS_SYNC          SKY_U32(14)
#define EV_REQ_FS_CLOSE         SKY_U32(15)

typedef struct ev_req_s ev_req_t;

struct ev_req_s {
    sky_ev_t *ev;
    sky_u32_t type;
};

typedef struct {
    ev_req_t req;
    sky_ev_t ev;
} ev_req_close_t;

typedef void (*event_req_pt)(ev_req_t *req, sky_i32_t res);

#else

#define EV_REG_IN           SKY_U32(0x00000010)
#define EV_REG_OUT          SKY_U32(0x00000020)

#define EV_EP_IN            SKY_U32(0x00000040)
#define EV_EP_OUT           SKY_U32(0x00000080)
#define EV_PENDING          SKY_U32(0x00000100)


#endif

typedef void (*on_event_pt)(sky_ev_t *ev);

struct sky_ev_loop_s {
#ifdef EVENT_USE_URING
    struct io_uring ring;
#else
    sky_i32_t fd;
    sky_i32_t max_event;
#endif

    struct timeval current_time;
    sky_timer_wheel_t *timer_ctx;
    sky_ev_t *status_queue;
    sky_ev_t **status_queue_tail;
    sky_u64_t current_step;

#if defined(EVENT_USE_EPOLL)
    struct epoll_event sys_evs[];
#elif defined(EVENT_USE_KQUEUE)
    sky_i32_t event_n;
    struct kevent sys_evs[];
#endif
};


sky_bool_t set_socket_nonblock(sky_socket_t fd);

sky_i32_t setup_open_file_count_limits();

void init_time(sky_ev_loop_t *ev_loop);

void update_time(sky_ev_loop_t *ev_loop);

#ifdef EVENT_USE_URING

static sky_inline struct io_uring_sqe *
get_seq(sky_ev_loop_t *const ev_loop) {
    struct io_uring_sqe *sqe;

    do {
        sqe = io_uring_get_sqe(&ev_loop->ring);
        if (sqe) {
            return sqe;
        }
        io_uring_submit(&ev_loop->ring);
        sqe = io_uring_get_sqe(&ev_loop->ring);

    } while (!sqe);

    return sqe;
}

static sky_inline struct io_uring_sqe *
get_seq2(sky_ev_t *const ev) {
    return get_seq(ev->ev_loop);
}

/**
 * 异步关闭fd句柄，不触发任何回调
 *
 * @param ev_loop event loop
 * @param fd  fd
 */
void event_close(sky_ev_loop_t *ev_loop, sky_i32_t fd);

void event_on_tcp_ser_open(ev_req_t *req, sky_i32_t res);

void event_on_tcp_accept(ev_req_t *req, sky_i32_t res);

void event_on_tcp_ser_close(ev_req_t *req, sky_i32_t res);

void event_on_tcp_cli_open(ev_req_t *req, sky_i32_t res);

void event_on_tcp_connect(ev_req_t *req, sky_i32_t res);

void event_on_tcp_read(ev_req_t *req, sky_i32_t res);

void event_on_tcp_write(ev_req_t *req, sky_i32_t res);

void event_on_tcp_sendfile(ev_req_t *req, sky_i32_t res);

void event_on_tcp_cli_shutdown(ev_req_t *req, sky_i32_t res);

void event_on_tcp_cli_close(ev_req_t *req, sky_i32_t res);


void event_on_fs_open(ev_req_t *req, sky_i32_t res);

void event_on_fs_read(ev_req_t *req, sky_i32_t res);

void event_on_fs_write(ev_req_t *req, sky_i32_t res);

void event_on_fs_sync(ev_req_t *req, sky_i32_t res);

void event_on_fs_close(ev_req_t *req, sky_i32_t res);

#else

static sky_inline void
event_add(sky_ev_t *ev, sky_u32_t events) {
    if ((ev->flags & events) != events && !(ev->flags & EV_PENDING)) {
        ev->flags |= EV_PENDING;

        sky_ev_loop_t *const ev_loop = ev->ev_loop;
        *ev_loop->status_queue_tail = ev;
        ev_loop->status_queue_tail = &ev->next;
    }
    ev->flags |= events;
}

static sky_inline void
event_close_add(sky_ev_t *ev) {
    if (!(ev->flags & EV_PENDING) && ev->fd == SKY_SOCKET_FD_NONE) {
        ev->flags |= EV_PENDING;

        sky_ev_loop_t *const ev_loop = ev->ev_loop;
        *ev_loop->status_queue_tail = ev;
        ev_loop->status_queue_tail = &ev->next;
    }
}

void event_on_tcp_ser_error(sky_ev_t *ev);

void event_on_tcp_ser_in(sky_ev_t *ev);

void event_on_tcp_ser_close(sky_ev_t *ev);

void event_on_tcp_cli_error(sky_ev_t *ev);

void event_on_tcp_cli_in(sky_ev_t *ev);

void event_on_tcp_cli_out(sky_ev_t *ev);

void event_on_tcp_cli_close(sky_ev_t *ev);

void event_on_fs_close(sky_ev_t *ev);

#endif

#endif
#endif //SKY_UNIX_IO_H
