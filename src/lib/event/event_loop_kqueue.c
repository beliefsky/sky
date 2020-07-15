//
// Created by weijing on 18-11-6.
//
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/event.h>
#include "event_loop.h"
#include "../core/palloc.h"
#include "../core/log.h"
#include "../core/memory.h"

#ifndef OPEN_MAX

#include <sys/param.h>

#ifndef OPEN_MAX
#ifdef NOFILE
#define OPEN_MAX NOFILE
#else
#define OPEN_MAX 65535
#endif
#endif
#endif

static sky_int32_t setup_open_file_count_limits();

static void
rbtree_insert_timer(sky_rbtree_node_t *temp, sky_rbtree_node_t *node,
                    sky_rbtree_node_t *sentinel);

sky_event_loop_t *
sky_event_loop_create(sky_pool_t *pool) {
    sky_event_loop_t *loop;
    struct sigaction sa;

    sky_memzero(&sa, sizeof(struct sigaction));
    sa.sa_handler = SIG_IGN;
    sigaction(SIGPIPE, &sa, null);

    loop = sky_pcalloc(pool, sizeof(sky_event_loop_t));
    loop->pool = pool;
    loop->fd = kqueue();
    loop->conn_max = setup_open_file_count_limits();
    loop->now = time(null);
    sky_rbtree_init(&loop->rbtree, &loop->sentinel, rbtree_insert_timer);

    return loop;
}

void
sky_event_loop_run(sky_event_loop_t *loop) {
    sky_bool_t timeout;
    sky_int16_t index, i;
    sky_int32_t fd, max_events, n;
    sky_rbtree_t *btree;
    sky_time_t now;
    sky_event_t *ev, **run_ev;
    struct kevent *events, *event;
    struct timespec timespec = {
            .tv_sec = 0,
            .tv_nsec = 0
    };

    fd = loop->fd;
    timeout = false;
    btree = &loop->rbtree;

    now = loop->now;

    max_events = sky_min(loop->conn_max, 1024);
    events = sky_pnalloc(loop->pool, sizeof(struct kevent) * (sky_uint32_t) max_events);
    run_ev = sky_pnalloc(loop->pool, sizeof(sky_event_t *) * (sky_uint32_t) max_events);

    for (;;) {
        n = kevent(fd, null, 0, events, max_events, timeout ? &timespec : null);
        if (sky_unlikely(n < 0)) {
            switch (errno) {
                case EBADF:
                case EINVAL:
                    break;
                default:
                    continue;
            }
            break;
        }

        // 合并事件状态, 防止多个事件造成影响
        for (index = 0, event = events; n--; ++event) {
            ev = event->udata;
            // 需要处理被移除的请求
            if (!ev->reg) {
                if (ev->index == -1) {
                    ev->index = index;
                    run_ev[index++] = ev;
                }
                continue;
            }
            // 是否出现异常
            if (sky_unlikely(event->flags & EV_ERROR)) {
                close(ev->fd);
                ev->reg = false;
                if (ev->index == -1) {
                    ev->index = index;
                    run_ev[index++] = ev;
                }
                continue;
            }
            // 是否可读
            // 是否可写
            event->filter == EVFILT_READ ? (ev->read = true) : (ev->write = true);

            if (ev->wait) {
                continue;
            }
            if (event->filter == EVFILT_READ) {
                if (!ev->read_run(ev)) {
                    if (ev->index == -1) {
                        ev->index = index;
                        run_ev[index++] = ev;
                    }
                }
            } else {
                if (!ev->write_run(ev)) {
                    if (ev->index == -1) {
                        ev->index = index;
                        run_ev[index++] = ev;
                    }
                }
            }
        }
        loop->now = time(null);

        for (i = 0; i != index; ++i) {
            ev = run_ev[i];
            ev->index = -1;
            if (ev->reg) {
                close(ev->fd);
                ev->reg = false;
            }
            if (ev->timeout != -1) {
                sky_rbtree_delete(btree, &ev->node);
            }
            // 触发回收资源待解决
            ev->close(ev);
        }

        if (loop->update) {
            loop->update = false;
        } else if (now == loop->now) {
            continue;
        }
        now = loop->now;
        // 处理超时的连接
        for (;;) {
            if (btree->root == btree->sentinel) {
                timeout = false;
                break;
            }
            ev = (sky_event_t *) sky_rbtree_min(btree->root, btree->sentinel);
            if (loop->now < ev->key) {
                timeout = true;
                timespec.tv_sec = (ev->key - now);
                break;
            }
            sky_rbtree_delete(btree, &ev->node);
            if (ev->reg) {
                ev->key = ev->now + ev->timeout;
                if (ev->key > now) {
                    ev->node.key = (sky_uintptr_t) &ev->key;
                    sky_rbtree_insert(btree, &ev->node);
                } else {
                    close(ev->fd);
                    ev->reg = false;
                    // 触发回收资源待解决
                    ev->close(ev);
                }
            } else {
                // 触发回收资源待解决
                ev->close(ev);
            }
        }
    }
}


void
sky_event_loop_shutdown(sky_event_loop_t *loop) {
    close(loop->fd);
    sky_destroy_pool(loop->pool);
}


void
sky_event_register(sky_event_t *ev, sky_int32_t timeout) {
    struct kevent event[2];
    if (timeout < 0) {
        timeout = -1;
    } else {
        if (timeout == 0) {
            ev->loop->update = true;
        }
        ev->key = ev->loop->now + timeout;
        ev->node.key = (sky_uintptr_t) &ev->key;
        sky_rbtree_insert(&ev->loop->rbtree, &ev->node);
    }
    ev->timeout = timeout;
    ev->reg = true;
    ev->index = -1;

    EV_SET(event, ev->fd, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    EV_SET(event + 1, ev->fd, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0, ev);
    kevent(ev->loop->fd, event, 2, null, 0, null);
}


void
sky_event_unregister(sky_event_t *ev) {
    if (!ev->reg) {
        return;
    }
    close(ev->fd);
    ev->reg = false;
    if (ev->timeout != -1) {
        sky_rbtree_delete(&ev->loop->rbtree, &ev->node);
    } else {
        ev->timeout = 0;
    }
    // 此处应添加 应追加需要处理的连接
    ev->loop->update = true;
    ev->key = 0;
    ev->node.key = (sky_uintptr_t) &ev->key;
    sky_rbtree_insert(&ev->loop->rbtree, &ev->node);
}


static sky_int32_t
setup_open_file_count_limits() {
    struct rlimit r;

    if (getrlimit(RLIMIT_NOFILE, &r) < 0) {
        sky_log_error("Could not obtain maximum number of file descriptors. Assuming %d", OPEN_MAX);
        return OPEN_MAX;
    }

    if (r.rlim_max != r.rlim_cur) {
        const rlim_t current = r.rlim_cur;

        if (r.rlim_max == RLIM_INFINITY) {
            r.rlim_cur = OPEN_MAX;
        } else if (r.rlim_cur < r.rlim_max) {
            r.rlim_cur = r.rlim_max;
        } else {
            /* Shouldn't happen, so just return the current value. */
            goto out;
        }

        if (setrlimit(RLIMIT_NOFILE, &r) < 0) {
            sky_log_error("Could not raise maximum number of file descriptors to %lu. Leaving at %lu", r.rlim_max,
                          current);
            r.rlim_cur = current;
        }
    }

    out:
    return (sky_int32_t) r.rlim_cur;
}


static void
rbtree_insert_timer(sky_rbtree_node_t *temp, sky_rbtree_node_t *node,
                    sky_rbtree_node_t *sentinel) {
    sky_rbtree_node_t **p;
    sky_time_t node_key;

    node_key = *(time_t *) (node->key);
    for (;;) {
        p = (node_key < (*(time_t *) (temp->key))) ? &temp->left : &temp->right;
        if (*p == sentinel) {
            break;
        }
        temp = *p;
    }
    *p = node;
    node->parent = temp;
    node->left = sentinel;
    node->right = sentinel;
    sky_rbt_red(node);
}