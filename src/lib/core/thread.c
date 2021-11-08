//
// Created by edz on 2021/11/4.
//

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "thread.h"

#if defined(__FreeBSD__)

#include <sys/param.h>
#include <sys/cpuset.h>
#endif


void
sky_thread_set_cpu(sky_thread_t thread, sky_i32_t n) {
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
