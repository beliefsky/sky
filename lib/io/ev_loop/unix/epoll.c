//
// Created by weijing on 2024/3/7.
//
#include "./unix_socket.h"

#ifdef EVENT_USE_EPOLL

#include <signal.h>
#include <unistd.h>
#include <sys/errno.h>
#include <core/log.h>

static void event_on_pending(sky_ev_loop_t *ev_loop);

static void event_on_status(sky_ev_loop_t *ev_loop);

sky_api sky_ev_loop_t *
sky_ev_loop_create() {
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    const sky_i32_t fd = epoll_create1(EPOLL_CLOEXEC);
    if (sky_unlikely(fd == -1)) {
        return null;
    }
    sky_i32_t max_event = setup_open_file_count_limits();
    max_event = sky_min(max_event, SKY_I32(1024));

    sky_ev_loop_t *const ev_loop = sky_malloc(
            sizeof(sky_ev_loop_t) + (sizeof(struct epoll_event) * (sky_usize_t) max_event)
    );

    ev_loop->fd = fd;
    ev_loop->max_event = max_event;
    ev_loop->current_block = null;
    ev_loop->pending_req = null;
    ev_loop->pending_req_tail = &ev_loop->pending_req;
    ev_loop->status_queue = null;
    ev_loop->status_queue_tail = &ev_loop->status_queue;

    return ev_loop;
}

sky_api void
sky_ev_loop_run(sky_ev_loop_t *ev_loop) {
    static const event_req_pt REQ_TABLES[] = {
            [EV_REQ_TCP_ACCEPT] = event_on_tcp_accept,
            [EV_REQ_TCP_CONNECT] = event_on_tcp_connect,
            [EV_REQ_TCP_READ] = event_on_tcp_read,
            [EV_REQ_TCP_WRITE] = event_on_tcp_write,
            [EV_REQ_TCP_READ_V] = event_on_tcp_read_v,
            [EV_REQ_TCP_WRITE_V] = event_on_tcp_write_v
    };


    sky_ev_t *ev;
    const struct epoll_event *event;
    sky_ev_req_t *req;
    sky_i32_t n;

    for (;;) {
        event_on_pending(ev_loop);
        event_on_status(ev_loop);

        n = epoll_wait(
                ev_loop->fd,
                ev_loop->sys_evs,
                ev_loop->max_event,
                -1
        );

        if (sky_unlikely(n == -1)) {
            return;
        }
        if (n) {
            event = ev_loop->sys_evs;
            do {
                ev = event->data.ptr;
                if ((event->events & (EPOLLHUP | EPOLLERR))) {
                    ev->flags |= EV_STATUS_ERROR;
                    event_pending_out_all(ev_loop, ev);
                    event_pending_in_all(ev_loop, ev);
                } else {
                    if ((event->events & EPOLLOUT)) {
                        ev->flags |= EV_STATUS_WRITE;
                        if (ev->out_req) {
                            for (;;) {
                                req = ev->out_req;
                                if (!REQ_TABLES[req->type](ev, req)) {
                                    break;
                                }
                                ev->out_req = req->next;
                                event_pending_add2(ev_loop, ev, req);
                                if (!ev->out_req) {
                                    ev->out_req_tail = &ev->out_req;
                                    break;
                                }
                            }
                        }
                    }
                    if ((event->events & EPOLLIN)) {
                        ev->flags |= EV_STATUS_READ;
                        if (ev->in_req) {
                            for (;;) {
                                req = ev->in_req;
                                if (!REQ_TABLES[req->type](ev, req)) {
                                    break;
                                }
                                ev->in_req = req->next;
                                event_pending_add2(ev_loop, ev, req);
                                if (!ev->in_req) {
                                    ev->in_req_tail = &ev->in_req;
                                    break;
                                }
                            }
                        }
                    }
                }
                ++event;
            } while ((--n));
        }
    }
}

sky_api void
sky_ev_loop_stop(sky_ev_loop_t *ev_loop) {

}

static void
event_on_pending(sky_ev_loop_t *ev_loop) {
    static const event_pending_pt PENDING_TABLES[] = {
            [EV_REQ_TCP_ACCEPT] = event_cb_tcp_accept,
            [EV_REQ_TCP_CONNECT] = event_cb_tcp_connect,
            [EV_REQ_TCP_READ] = event_cb_tcp_read,
            [EV_REQ_TCP_WRITE] = event_cb_tcp_write,
            [EV_REQ_TCP_READ_V] = event_cb_tcp_read_v,
            [EV_REQ_TCP_WRITE_V] = event_cb_tcp_write_v
    };

    if (!ev_loop->pending_req) {
        return;
    }
    sky_ev_req_t *req, *next;

    do {
        req = ev_loop->pending_req;
        ev_loop->pending_req = null;
        ev_loop->pending_req_tail = &ev_loop->pending_req;

        do {
            next = req->next;
            PENDING_TABLES[req->type](req->ev, req);
            req = next;
        } while (req);

    } while (ev_loop->pending_req);
}

static void
event_on_status(sky_ev_loop_t *ev_loop) {
    sky_ev_t *ev = ev_loop->status_queue;
    if (!ev) {
        return;
    }
    ev_loop->status_queue = null;
    ev_loop->status_queue_tail = &ev_loop->status_queue;

    sky_ev_t *next;
    sky_u32_t opts, reg_opts;

    do {
        next = ev->next;
        ev->next = null;

        if (ev->fd == SKY_SOCKET_FD_NONE) {
            ev->cb(ev);
        } else {
            opts = EPOLLET;
            reg_opts = 0;

            if ((ev->flags & EV_REG_IN)) {
                opts |= EPOLLIN;
                reg_opts |= EV_EP_IN;
            }
            if ((ev->flags & EV_REG_OUT)) {
                opts |= EPOLLOUT;
                reg_opts |= EV_EP_OUT;
            }
            struct epoll_event ep_ev = {
                    .events = opts,
                    .data.ptr = ev
            };

            if (sky_unlikely(epoll_ctl(
                    ev->ev_loop->fd,
                    (ev->flags & (EV_EP_IN | EV_EP_OUT)) ? EPOLL_CTL_MOD : EPOLL_CTL_ADD,
                    ev->fd,
                    &ep_ev
            ) == -1)) {
                sky_log_error("epoll_ctl error: %d ->%d", ev->fd, errno);
            } else {
                ev->flags |= reg_opts;
            }
        }
        ev = next;
    } while (ev);
}

#endif

