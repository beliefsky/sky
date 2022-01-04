//
// Created by edz on 2021/11/4.
//
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "platform.h"
#include "core/memory.h"
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
    sky_platform_t *parent;
    sky_event_loop_t *loop;
} thread_item_t;

struct sky_platform_s {
    sky_bool_t cpu_bind;
    sky_u32_t thread_n;
    sky_platform_run_pt run;
    sky_platform_destroy_pt destroy;
    thread_item_t *items;
};

static void *thread_run(void *data);

static void thread_bind_cpu(pthread_t thread, sky_i32_t n);

sky_platform_t *
sky_platform_create(const sky_platform_conf_t *conf) {
    sky_bool_t cpu_bind;
    sky_u32_t thread_size;
    sky_platform_t *platform;
    thread_item_t *item;

    if (!conf->run) {
        return null;
    }
    if (conf->thread_size > 0) {
        cpu_bind = false;
        thread_size = conf->thread_size;
    } else {
        cpu_bind = true;
        sky_isize_t size = (sky_isize_t) sysconf(_SC_NPROCESSORS_CONF);
        if (size <= 0) {
            size = 1;
        }
        thread_size = (sky_u32_t) size;
    }

    platform = sky_malloc(sizeof(sky_platform_t) + (sizeof(thread_item_t) * (sky_usize_t) thread_size));
    item = (thread_item_t *) (platform + 1);

    platform->cpu_bind = cpu_bind;
    platform->thread_n = thread_size;
    platform->run = conf->run;
    platform->destroy = conf->destroy;
    platform->items = item;


    for (sky_u32_t i = 0; i < thread_size; ++i, ++item) {
        item->index = i;
        item->parent = platform;
        item->loop = sky_event_loop_create();
    }


    return platform;
}

sky_u32_t
sky_platform_thread_n(const sky_platform_t platform) {
    return platform.thread_n;
}

void
sky_platform_run(sky_platform_t *platform) {
    sky_u32_t i;
    thread_item_t *item;
    pthread_attr_t attr;

    item = platform->items;
    for (i = 0; i < platform->thread_n; ++i, ++item) {
        pthread_attr_init(&attr);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        pthread_attr_setstacksize(&attr, 32768);

        pthread_create(&item->thread, &attr, thread_run, item);
        if (platform->cpu_bind) {
            thread_bind_cpu(item->thread, (sky_i32_t) i);
        }
        pthread_attr_destroy(&attr);
    }

    item = platform->items;
    for (i = 0; i < platform->thread_n; ++i, ++item) {
        pthread_join(item->thread, null);
    }
}

void
sky_platform_destroy(sky_platform_t *platform) {
    sky_free(platform);
}

static void *
thread_run(void *data) {
    thread_item_t *item = data;
    void *result = null;
    sky_platform_run_pt handle = item->parent->run;

    if (sky_likely(handle)) {
        result = handle(item->loop, item->index);
    }

    sky_event_loop_run(item->loop);

    sky_platform_destroy_pt destroy = item->parent->destroy;
    if (sky_likely(destroy)) {
        destroy(item->loop, result);
    }

    sky_event_loop_destroy(item->loop);

    return null;
}


static sky_inline void
thread_bind_cpu(pthread_t thread, sky_i32_t n) {
#if defined(__linux__)
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(n, &set);
    pthread_setaffinity_np(thread, sizeof(cpu_set_t), &set);
#elif defined(__FreeBSD__)
    cpuset_t set;

    CPU_ZERO(&set);
    CPU_SET(n, &set);
    pthread_setaffinity_np(thread, sizeof(cpuset_t), &set);
#endif
}
