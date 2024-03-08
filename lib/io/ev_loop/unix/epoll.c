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

    return ev_loop;
}

sky_api void
sky_ev_loop_run(sky_ev_loop_t *ev_loop) {
    static const event_req_pt OUT_TABLES[] = {
            [(EV_REQ_TCP_CONNECT -1) >> 1] = event_on_tcp_connect,
            [(EV_REQ_TCP_WRITE -1) >> 1] = event_on_tcp_write,
    };
    static const event_req_pt IN_TABLES[] = {
            [EV_REQ_TCP_READ >> 1] = event_on_tcp_read,
    };


    sky_ev_t *ev;
    const struct epoll_event *event;
    sky_ev_req_t *req;
    sky_i32_t n;

    event_on_status(ev_loop);

    for (;;) {
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
                    if (ev->out_req) {
                        *ev_loop->pending_req_tail = ev->out_req;
                        ev_loop->pending_req_tail = ev->out_req_tail;
                    }
                    if (ev->in_req) {
                        *ev_loop->pending_req_tail = ev->in_req;
                        ev_loop->pending_req_tail = ev->in_req_tail;
                    }
                    sky_log_info("event error: %d", ev->fd);
                } else {
                    if ((event->events & EPOLLOUT)) {
                        ev->flags |= EV_STATUS_WRITE;
                        sky_log_info("event out: %d", ev->fd);
                        if (ev->out_req) {
                            for (;;) {
                                req = ev->out_req;
                                if (!OUT_TABLES[(req->type -1) >> 1](ev, req)) {
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
                        sky_log_info("event in: %d", ev->fd);
                        if (ev->in_req) {
                            for (;;) {
                                req = ev->in_req;
                                if (!IN_TABLES[req->type >> 1](ev, req)) {
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
        event_on_pending(ev_loop);
        event_on_status(ev_loop);
    }
}

sky_api void
sky_ev_loop_stop(sky_ev_loop_t *ev_loop) {

}

static void
event_on_pending(sky_ev_loop_t *ev_loop) {
    static const event_pending_pt PENDING_TABLES[] = {
            [EV_REQ_TCP_CONNECT] = event_cb_tcp_connect,
            [EV_REQ_TCP_WRITE] = event_cb_tcp_rw,
            [EV_REQ_TCP_READ] = event_cb_tcp_rw,
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
            event_req_release(ev_loop, req);
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

     sky_ev_t *next;
    sky_u32_t opts, reg_opts;

    do {
        opts = EPOLLET;
        reg_opts = 0;

        if ((ev->flags & EV_REG_IN)) {
            opts |= EPOLLIN | EPOLLPRI;
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
        next = ev->status_next;
        ev->status_next = null;
        ev = next;
    } while (ev);

}

#endif

