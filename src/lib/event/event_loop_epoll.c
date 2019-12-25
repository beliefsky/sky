//
// Created by weijing on 18-11-6.
//
#include <sys/resource.h>
#include <unistd.h>
#include <errno.h>
#include <signal.h>
#include <sys/epoll.h>
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


struct sky_event_loop_s {
    sky_pool_t *pool;
    sky_rbtree_t rbtree;
    sky_rbtree_node_t sentinel;
    sky_time_t now;
    sky_int32_t fd;
    sky_int32_t conn_max;
    sky_bool_t update:1;
};


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
    loop->fd = epoll_create1(EPOLL_CLOEXEC);
    loop->conn_max = setup_open_file_count_limits();
    loop->now = time(null);
    sky_rbtree_init(&loop->rbtree, &loop->sentinel, rbtree_insert_timer);

    return loop;
}

void
sky_event_loop_run(sky_event_loop_t *loop) {
    sky_int32_t fd, max_events, n, timeout;
    sky_rbtree_t *btree;
    sky_time_t now;
    sky_event_t *ev;
    struct epoll_event *events, *event;

    fd = loop->fd;
    timeout = -1;
    btree = &loop->rbtree;

    now = loop->now;

    max_events = sky_min(loop->conn_max, 1024);
    events = sky_palloc(loop->pool, sizeof(struct epoll_event) * (sky_uint32_t) max_events);


    for (;;) {
        n = epoll_wait(fd, events, max_events, timeout);
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

        loop->now = time(null);

        for (event = events; n--; ++event) {
            ev = event->data.ptr;

            // 需要处理被移除的请求
            if (!ev->reg) {
                sky_rbtree_delete(btree, &ev->node);
                ev->close(ev);
                continue;
            }
            // 是否出现异常
            if (event->events & (EPOLLRDHUP | EPOLLHUP)) {
                close(ev->fd);
                ev->reg = false;
                if (ev->timeout != -1) {
                    sky_rbtree_delete(btree, &ev->node);
                }
                // 触发回收资源待解决
                ev->close(ev);
                continue;
            }
            // 是否可读
            ev->read = (event->events & EPOLLIN) != 0;
            // 是否可写
            ev->write = (event->events & EPOLLOUT) != 0;

            if (ev->wait) {
                continue;
            }
            if (!ev->run(ev)) {
                close(ev->fd);
                ev->reg = false;
                if (ev->timeout != -1) {
                    sky_rbtree_delete(btree, &ev->node);
                }
                // 触发回收资源待解决
                ev->close(ev);
                continue;
            }
            ev->now = loop->now;
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
                timeout = -1;
                break;
            }
            ev = (sky_event_t *) sky_rbtree_min(btree->root, btree->sentinel);
            if (loop->now < ev->key) {
                timeout = (sky_int32_t) ((ev->key - now) * 1000);
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
    struct epoll_event event;
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

    event.events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLERR | EPOLLET;
    event.data.ptr = ev;
    epoll_ctl(ev->loop->fd, EPOLL_CTL_ADD, ev->fd, &event);
}


void
sky_event_unregister(sky_event_t *ev) {
    if (sky_unlikely(!ev->reg)) {
        return;
    }
    close(ev->fd);
    ev->reg = false;
    if (ev->timeout != -1) {
        sky_rbtree_delete(&ev->loop->rbtree, &ev->node);
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