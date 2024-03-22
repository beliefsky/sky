//
// Created by weijing on 2024/3/8.
//
#include "./unix_socket.h"

#ifdef EVENT_USE_KQUEUE

#include <signal.h>
#include <unistd.h>

static void event_on_pending(sky_ev_loop_t *ev_loop);

static void event_on_status(sky_ev_loop_t *ev_loop);

sky_api sky_ev_loop_t *
sky_ev_loop_create() {
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    const sky_i32_t fd = kqueue();
    if (sky_unlikely(fd == -1)) {
        return null;
    }

    sky_i32_t max_event = setup_open_file_count_limits();
    max_event = sky_min(max_event, SKY_I32(1024));

    sky_ev_loop_t *const ev_loop = sky_malloc(
            sizeof(sky_ev_loop_t) + (sizeof(struct kevent) * (sky_usize_t) (max_event << 1))
    );

    ev_loop->fd = fd;
    ev_loop->max_event = max_event;
    ev_loop->current_block = null;
    ev_loop->pending_req = null;
    ev_loop->pending_req_tail = &ev_loop->pending_req;
    ev_loop->status_queue = null;
    ev_loop->status_queue_tail = &ev_loop->status_queue;
    ev_loop->event_n = 0;

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
    const struct kevent *event;
    sky_ev_req_t *req;
    sky_i32_t n;

    event_on_status(ev_loop);

    for (;;) {
        n = kevent(
                ev_loop->fd,
                ev_loop->sys_evs,
                ev_loop->event_n,
                ev_loop->sys_evs + + ev_loop->max_event,
                ev_loop->max_event,
                null
        );
        if (sky_unlikely(n == -1)) {
            return;
        }
        ev_loop->event_n = 0;

        if (n) {
            event = ev_loop->sys_evs + ev_loop->max_event;
            do {
                ev = event->udata;

                if (sky_unlikely((event->flags & (EV_ERROR)))) {
                    event_pending_out_all(ev_loop, ev);
                    event_pending_in_all(ev_loop, ev);
                } else if (event->filter == EVFILT_WRITE) {
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
                } else {
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

                ++event;
            } while ((--n));
        }
        event_on_pending(ev_loop);
        event_on_status(ev_loop);
    }
}

sky_api void
sky_ev_loop_stop(sky_ev_loop_t *ev_loop) {
    close(ev_loop->fd);
    sky_free(ev_loop);
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
    do {
        next = ev->next;
        ev->next = null;

        if (ev->fd == SKY_SOCKET_FD_NONE) {
            ev->cb(ev);
        } else {
            if (!(ev->flags & EV_EP_IN) && (ev->flags & EV_REG_IN)) {
                if (ev_loop->event_n == ev_loop->max_event) {
                    kevent(
                            ev->fd,
                            ev_loop->sys_evs,
                            ev_loop->event_n,
                            null,
                            0,
                            null
                    );
                    ev_loop->event_n = 0;
                }
                EV_SET(
                        &ev_loop->sys_evs[ev_loop->event_n++],
                        ev->fd, EVFILT_READ,
                        EV_ADD | EV_CLEAR,
                        0,
                        0,
                        ev
                );
                ev->flags |= EV_EP_IN;
            }
            if (!(ev->flags & EV_EP_OUT) && (ev->flags & EV_REG_OUT)) {
                if (ev_loop->event_n == ev_loop->max_event) {
                    kevent(
                            ev->fd,
                            ev_loop->sys_evs,
                            ev_loop->event_n,
                            null,
                            0,
                            null
                    );
                    ev_loop->event_n = 0;
                }
                EV_SET(
                        &ev_loop->sys_evs[ev_loop->event_n++],
                        ev->fd, EVFILT_WRITE,
                        EV_ADD | EV_CLEAR,
                        0,
                        0,
                        ev
                );
                ev->flags |= EV_EP_OUT;
            }
        }
        ev = next;
    } while (ev);
}

#endif

