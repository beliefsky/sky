//
// Created by edz on 2022/3/2.
//
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "share_msg.h"
#include "../core/memory.h"
#include "../core/log.h"
#include "../safe/mpsc_queue.h"

#ifdef HAVE_EVENT_FD

#include <sys/eventfd.h>

#else

#include <unistd.h>
#include <fcntl.h>

#endif

struct sky_share_msg_s {
    sky_share_msg_connect_t *conn;
    sky_u32_t num;
};

struct sky_share_msg_connect_s {
    sky_event_t event;
    sky_mpsc_queue_t data_queue;
    sky_share_msg_t *share_msg;
    sky_share_msg_handle_pt handle;
    void *data;
#ifndef HAVE_EVENT_FD
    sky_i32_t write_fd;
#endif
    sky_u32_t index;
    sky_atomic_bool_t flags;
};

static sky_bool_t event_conn_run(sky_event_t *ev);

static void event_conn_error(sky_event_t *ev);

sky_share_msg_t *
sky_share_msg_create(sky_u32_t num) {
    const sky_usize_t conn_size = sizeof(sky_share_msg_connect_t) * num;

    sky_share_msg_t *share_msg = sky_malloc(sizeof(sky_share_msg_t) + conn_size);
    share_msg->conn = (sky_share_msg_connect_t *) (share_msg + 1);
    share_msg->num = num;

    sky_memzero(share_msg->conn, conn_size);

    return share_msg;
}

sky_u32_t
sky_share_msg_num(sky_share_msg_t *share_msg) {
    return share_msg->num;
}

void sky_share_msg_destroy(sky_share_msg_t *share_msg) {
    sky_free(share_msg);
}

sky_bool_t
sky_share_msg_bind(
        sky_share_msg_t *share_msg,
        sky_event_loop_t *loop,
        sky_share_msg_handle_pt handle,
        void *data,
        sky_u32_t index
) {
    if (sky_unlikely(index >= share_msg->num)) {
        return false;
    }
    sky_share_msg_connect_t *conn = share_msg->conn + index;
    sky_mpsc_queue_init(&conn->data_queue, 1 << 16);
    conn->share_msg = share_msg;
    conn->handle = handle;
    conn->data = data;
    conn->index = index;
    conn->flags = SKY_ATOMIC_VAR_INIT(false);

#ifdef HAVE_EVENT_FD
    const sky_i32_t fd = eventfd(0, EFD_NONBLOCK | EFD_SEMAPHORE | EFD_CLOEXEC);
    sky_event_init(loop, &conn->event, fd, event_conn_run, event_conn_error);
#else
#ifdef HAVE_ACCEPT4
    return false;
#else
    sky_i32_t fd[2];
    pipe2(fd, O_NONBLOCK | O_CLOEXEC);
    sky_event_init(loop, &conn->event, fd[0], event_conn_run, event_conn_error);
    conn->write_fd = fd[1];
#endif
#endif

    sky_event_register_only_read(&conn->event, -1);

    return true;
}

sky_bool_t
sky_share_msg_scan(sky_share_msg_t *share_msg, sky_share_msg_iter_pt iter, void *user_data) {
    sky_share_msg_connect_t *conn = share_msg->conn;
    for (sky_u32_t i = 0; i < share_msg->num; ++i, ++conn) {
        if (!iter(conn, i, user_data)) {
            return false;
        }
    }

    return true;
}

sky_bool_t
sky_share_msg_send_index(sky_share_msg_t *share_msg, sky_u32_t index, void *data) {
    if (sky_unlikely(index >= share_msg->num)) {
        return false;
    }
    sky_share_msg_connect_t *conn = share_msg->conn + index;

    return sky_share_msg_send(conn, data);
}

sky_inline sky_bool_t
sky_share_msg_send(sky_share_msg_connect_t *conn, void *data) {
    if (sky_unlikely(!sky_mpsc_queue_push(&conn->data_queue, data))) {
        return false;
    }
#ifdef HAVE_EVENT_FD
        eventfd_write(conn->event.fd, 1);
#else
        sky_u8_t value = 1;
        write(conn->write_fd, &value, sizeof(sky_u8_t));
#endif

    return true;
}

static sky_bool_t
event_conn_run(sky_event_t *ev) {
    sky_share_msg_connect_t *conn = (sky_share_msg_connect_t *) ev;
#ifdef HAVE_EVENT_FD
    sky_u64_t value;
    for (;;) {
        if (eventfd_read(ev->fd, &value) != 0) {
            break;
        }
    }
#else
    sky_u64_t value[16];
    for (;;) {
        if (read(ev->fd, value, sizeof(value)) <= 0) {
            break;
        }
    }
#endif

    void *data;
    while ((data = sky_mpsc_queue_pop(&conn->data_queue)) != null) {
        conn->handle(data, conn->data);
    }

    return true;
}


static void
event_conn_error(sky_event_t *ev) {
    sky_log_info("%d: share_msg conn error", ev->fd);
}