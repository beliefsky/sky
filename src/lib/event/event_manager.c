//
// Created by beliefsky on 2022/3/6.
//
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "event_manager.h"
#include "../core/memory.h"
#include <pthread.h>
#include <unistd.h>

#if defined(__FreeBSD__)

#include <sys/param.h>
#include <sys/cpuset.h>
#include <pthread_np.h>
#endif

typedef struct {
    sky_u32_t index;
    pthread_t thread;
    sky_event_loop_t *loop;
} event_thread_t;

struct sky_event_manager_s {
    sky_u32_t thread_n;
    event_thread_t event_threads[];
};

static void *thread_run(void *data);

static void thread_bind_cpu(pthread_attr_t *attr, sky_u32_t n);

static void main_bind_cpu();

sky_thread sky_u32_t event_manager_idx = SKY_U32_MAX;

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
        thread->index = i;
        thread->loop = sky_event_loop_create();
    }

    return manager;
}

sky_u32_t
sky_event_manager_thread_n(sky_event_manager_t *manager) {
    return manager->thread_n;
}

sky_u32_t
sky_event_manager_thread_idx(sky_event_manager_t *manager) {
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
