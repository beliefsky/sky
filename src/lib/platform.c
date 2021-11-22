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
#endif

struct sky_platform_s {
    sky_u32_t thread_n;
    pthread_t *thread;
};

static void thread_bind_cpu(pthread_t thread, sky_i32_t n);

sky_platform_t *
sky_platform_create(const sky_platform_conf_t *conf) {
    sky_bool_t cpu_bind;
    sky_u32_t thread_size;
    pthread_t *thread;

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

    sky_platform_t *platform = sky_malloc(
            sizeof(sky_platform_t) + (sizeof(pthread_t) * (sky_usize_t) thread_size));
    platform->thread_n = thread_size;
    platform->thread = (pthread_t *) (platform + 1);

    pthread_attr_t attr;
    for (sky_u32_t i = 0; i < platform->thread_n; ++i) {
        thread = platform->thread + i;

        pthread_attr_init(&attr);
        pthread_attr_setscope(&attr, PTHREAD_SCOPE_SYSTEM);
        pthread_attr_setstacksize(&attr, 32768);

        pthread_create(thread, &attr, (void *(*)(void *)) conf->run, (void *) (sky_usize_t) i);
        if (cpu_bind) {
            thread_bind_cpu(*thread, (sky_i32_t) i);
        }
        pthread_attr_destroy(&attr);
    }

    return platform;
}

void
sky_platform_wait(sky_platform_t *platform) {
    for (sky_u32_t i = 0; i < platform->thread_n; ++i) {

        pthread_join(platform->thread[i], null);
    }
}

void
sky_platform_destroy(sky_platform_t *platform) {
    sky_free(platform);
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
