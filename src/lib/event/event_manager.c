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
#include <pthread.h>
#include <unistd.h>

#if defined(__FreeBSD__)

#include <sys/param.h>
#include <sys/cpuset.h>
#include <pthread_np.h>
#endif

#ifdef SKY_HAVE_EVENT_FD

#include <sys/eventfd.h>

#else

#include <fcntl.h>
#include "../net/inet.h"

#endif

typedef struct event_thread_s event_thread_t;

struct event_thread_s {
    sky_event_t msg_event;
    sky_mpsc_queue_t queue;
    sky_event_loop_t *loop;
    pthread_t thread;
#ifndef SKY_HAVE_EVENT_FD
    sky_i32_t write_fd;
#endif
    sky_u32_t thread_idx;
    sky_atomic_bool_t ready;
};

struct sky_event_manager_s {
    event_thread_t *event_threads;
    sky_u32_t thread_n;
};

static void *thread_run(void *data);

static sky_bool_t event_msg_run(event_thread_t *thread);

static void event_msg_error(event_thread_t *thread);

static void event_thread_msg_send(event_thread_t *thread);

static void thread_bind_cpu(pthread_attr_t *attr, sky_u32_t n);

static void main_bind_cpu();


static sky_thread sky_u32_t event_manager_idx = SKY_U32_MAX;

sky_event_manager_t *
sky_event_manager_create() {
    return sky_event_manager_create_with_capacity(65536);
}

sky_event_manager_t *
sky_event_manager_create_with_capacity(sky_usize_t share_msg_capacity) {
    sky_isize_t size = (sky_isize_t) sysconf(_SC_NPROCESSORS_CONF);
    if (size <= 0) {
        size = 1;
    }
    event_manager_idx = 0;
    const sky_u32_t thread_n = (sky_u32_t) size;

    sky_event_manager_t *manager = sky_malloc(sizeof(sky_event_manager_t) + sizeof(event_thread_t) * thread_n);

    event_thread_t *thread = (event_thread_t *) (manager + 1);

    manager->event_threads = thread;
    manager->thread_n = thread_n;

    for (sky_u32_t i = 0; i < thread_n; ++i, ++thread) {
        sky_mpsc_queue_init(&thread->queue, share_msg_capacity);
        thread->loop = sky_event_loop_create();
        thread->thread_idx = i;
        thread->ready = SKY_ATOMIC_VAR_INIT(true);
#if defined(SKY_HAVE_EVENT_FD)
        const sky_i32_t fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        sky_event_init(thread->loop, &thread->msg_event, fd, event_msg_run, event_msg_error);
#elif defined(SKY_HAVE_ACCEPT4)
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
    const sky_bool_t result = sky_mpsc_queue_push(&thread->queue, msg);
    if (sky_unlikely(!result)) {
        event_thread_msg_send(thread);
    } else if (sky_atomic_get_explicit(&thread->ready, SKY_ATOMIC_ACQUIRE)) {
        sky_atomic_set_explicit(&thread->ready, false, SKY_ATOMIC_RELEASE);
        event_thread_msg_send(thread);
    }

    return result;
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
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, null);
    pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, null);

    event_thread_t *item = data;
    event_manager_idx = item->thread_idx;

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
#ifdef SKY_HAVE_EVENT_FD
    sky_u64_t tmp;
    eventfd_read(thread->msg_event.fd, &tmp);
#else
    sky_uchar_t tmp[sizeof(sky_u8_t) << 6];
    for (;;) {
        if (read(thread->msg_event.fd, tmp, (sizeof(sky_u8_t) << 6)) < (sky_isize_t)(sizeof(sky_u8_t) << 6)) {
            break;
        }
    }
#endif

    sky_atomic_set_explicit(&thread->ready, true, SKY_ATOMIC_RELEASE);

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

static sky_inline void
event_thread_msg_send(event_thread_t *thread) {
#ifdef SKY_HAVE_EVENT_FD
    sky_u64_t tmp = SKY_U64(1);
    eventfd_write(thread->msg_event.fd, tmp);
#else
    sky_u8_t tmp = SKY_U8(1);
    write(thread->write_fd, &tmp, sizeof(sky_u8_t));
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
