//
// Created by beliefsky on 2022/3/6.
//
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "event_manager.h"
#include "../core/memory.h"
#include "../safe/mpsc_queue.h"
#include "../core/log.h"
#include "../net/inet.h"
#include <pthread.h>
#include <unistd.h>

#if defined(__FreeBSD__)

#include <sys/param.h>
#include <sys/cpuset.h>
#include <pthread_np.h>
#endif

#ifdef HAVE_EVENT_FD

#include <sys/eventfd.h>

#else

#include <unistd.h>
#include <fcntl.h>

#endif

typedef struct {
    sky_event_t msg_event;
    sky_timer_wheel_entry_t timer;
    sky_mpsc_queue_t queue;
    sky_event_loop_t *loop;
    pthread_t thread;
    sky_u32_t msg_n;
#ifndef HAVE_EVENT_FD
    sky_i32_t write_fd;
#endif
    sky_u32_t index;
} event_thread_t;

struct sky_event_manager_s {
    sky_u32_t thread_n;
    event_thread_t event_threads[];
};

static void *thread_run(void *data);

static sky_bool_t event_msg_run(event_thread_t *thread);

static void event_msg_error(event_thread_t *thread);

static void event_timer_cb(sky_timer_wheel_entry_t *entry);

static void thread_bind_cpu(pthread_attr_t *attr, sky_u32_t n);

static void main_bind_cpu();


static sky_thread sky_u32_t event_manager_idx = SKY_U32_MAX;

sky_event_manager_t *
sky_event_manager_create() {
    sky_isize_t size = (sky_isize_t) sysconf(_SC_NPROCESSORS_CONF);
    if (size <= 0) {
        size = 1;
    }
    event_manager_idx = 0;
    const sky_u32_t thread_n = (sky_u32_t) size;

    sky_event_manager_t *manager = sky_malloc(sizeof(sky_event_manager_t) + sizeof(event_thread_t) * thread_n);
    manager->thread_n = thread_n;

    event_thread_t *thread = manager->event_threads;
    for (sky_u32_t i = 0; i < thread_n; ++i, ++thread) {
        sky_mpsc_queue_init(&thread->queue, 1 << 16);
        sky_timer_entry_init(&thread->timer, event_timer_cb);
        thread->loop = sky_event_loop_create();
        thread->msg_n = 0;
        thread->index = i;
#if defined(HAVE_EVENT_FD)
        const sky_i32_t fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        sky_event_init(thread->loop, &thread->msg_event, fd, event_msg_run, event_msg_error);
#elif defined(HAVE_ACCEPT4)
        sky_i32_t fd[2];
        pipe2(fd, O_NONBLOCK | O_CLOEXEC);
        sky_event_init(thread->loop, &thread->msg_event, fd[0], event_msg_run, event_msg_error);
        thread->write_fd = fd[1];
#else
        sky_i32_t fd[2];
        pipe(fd);
        sky_set_socket_nonblock(fd[0]);
        sky_event_init(thread->loop, &thread->msg_event, fd[0], event_msg_run, event_msg_error);
        thread->write_fd = fd[1];
#endif
        sky_event_register_only_read(&thread->msg_event, -1);

    }

    return manager;
}

sky_u32_t
sky_event_manager_thread_n(sky_event_manager_t *manager) {
    return manager->thread_n;
}

sky_u32_t
sky_event_manager_thread_idx() {
    return event_manager_idx;
}

sky_event_loop_t *
sky_event_manager_thread_event_loop(sky_event_manager_t *manager) {
    if (sky_unlikely(event_manager_idx == SKY_U32_MAX)) {
        return null;
    } else {
        return manager->event_threads[event_manager_idx].loop;
    }
}

sky_event_loop_t *
sky_event_manager_idx_event_loop(sky_event_manager_t *manager, sky_u32_t idx) {
    if (sky_likely(idx < manager->thread_n)) {
        return manager->event_threads[idx].loop;
    } else {
        return null;
    }
}

sky_bool_t
sky_event_manager_idx_msg(sky_event_manager_t *manager, sky_event_msg_t *msg, sky_u32_t idx) {
    if (sky_unlikely(null == msg || idx >= manager->thread_n)) {
        return false;
    }
    event_thread_t *thread = manager->event_threads + idx;

    sky_bool_t result = sky_mpsc_queue_push(&thread->queue, msg);
    if (sky_unlikely(!result)) {
        return false;
    }
    if (idx == event_manager_idx) {
        while (null != (msg = sky_mpsc_queue_pop(&thread->queue))) {
            msg->handle(msg);
        }
        return true;
    }
    if (thread->msg_n == 0) {
        ++thread->msg_n;
        sky_event_timer_register(thread->loop, &thread->timer, 1);
        return true;
    } else if (thread->msg_n < 8192) {
        ++thread->msg_n;
        return true;
    } else {
        thread->msg_n = 0;
        sky_timer_wheel_unlink(&thread->timer);
    }

#ifdef HAVE_EVENT_FD
    eventfd_write(thread->msg_event.fd, SKY_U64(1));
#else
    sky_usize_t tmp = SKY_USIZE(1);
    write(thread->write_fd, &tmp, sizeof(sky_usize_t));
#endif
    return true;
}

sky_bool_t
sky_event_manager_scan(sky_event_manager_t *manager, sky_event_iter_pt iter, void *u_data) {
    event_thread_t *item = manager->event_threads;
    for (sky_u32_t i = 0; i < manager->thread_n; ++i, ++item) {
        if (!iter(item->loop, u_data, i)) {
            return false;
        }
    }
    return true;
}

void
sky_event_manager_run(sky_event_manager_t *manager) {
    sky_u32_t i;
    pthread_attr_t thread;
    event_thread_t *item;

    if (sky_unlikely(event_manager_idx != 0)) {
        return;
    }
    main_bind_cpu();

    item = manager->event_threads + 1;
    for (i = 1; i < manager->thread_n; ++i, ++item) {
        pthread_attr_init(&thread);
        pthread_attr_setscope(&thread, PTHREAD_SCOPE_SYSTEM);
        thread_bind_cpu(&thread, i);
        pthread_create(&item->thread, &thread, thread_run, item);
        pthread_attr_destroy(&thread);
    }
    item = manager->event_threads;
    thread_run(item++);
    for (i = 1; i < manager->thread_n; ++i, ++item) {
        pthread_join(item->thread, null);
    }
}

void
sky_event_manager_destroy(sky_event_manager_t *manager) {
    sky_free(manager);
}

static void *
thread_run(void *data) {
    event_thread_t *item = data;
    event_manager_idx = item->index;

    sky_event_loop_run(item->loop);
    sky_event_loop_destroy(item->loop);

    return null;
}

static sky_bool_t
event_msg_run(event_thread_t *thread) {
    sky_event_msg_t *msg;


    while (null != (msg = sky_mpsc_queue_pop(&thread->queue))) {
        msg->handle(msg);
    }
#ifdef HAVE_EVENT_FD
    sky_u64_t tmp;
    for (;;) {
        if (eventfd_read(thread->msg_event.fd, &tmp) == -1) {
            break;
        }
    }
#else
    sky_uchar_t tmp[128];
    for (;;) {
        if (read(thread->msg_event.fd, tmp, 128) <= 0) {
            break;
        }
    }
#endif

    while (null != (msg = sky_mpsc_queue_pop(&thread->queue))) {
        msg->handle(msg);
    }

    return true;
}

static void
event_msg_error(event_thread_t *thread) {
    (void) thread;
    sky_log_info("event_msg conn error");
}

static void
event_timer_cb(sky_timer_wheel_entry_t *entry) {
    event_thread_t *thread = sky_type_convert(entry, event_thread_t, timer);
    thread->msg_n = 0;
#ifdef HAVE_EVENT_FD
    eventfd_write(thread->msg_event.fd, SKY_U64(1));
#else
    sky_usize_t tmp = SKY_USIZE(1);
    write(thread->write_fd, &tmp, sizeof(sky_usize_t));
#endif
}

static sky_inline void
thread_bind_cpu(pthread_attr_t *attr, sky_u32_t n) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(n, &set);
    pthread_attr_setaffinity_np(attr, sizeof(cpu_set_t), &set);
#elif defined(__FreeBSD__)
    cpuset_t set;

    CPU_ZERO(&set);
    CPU_SET(n, &set);
    pthread_attr_setaffinity_np(attr, sizeof(cpuset_t), &set);
#endif
}

static sky_inline void
main_bind_cpu() {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(0, &set);
    sched_setaffinity(0, sizeof(cpu_set_t), &set);
#elif defined(__FreeBSD__)
    cpuset_t set;

    CPU_ZERO(&set);
    CPU_SET(0, &set);
    cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(cpuset_t), &set);
#endif
}
