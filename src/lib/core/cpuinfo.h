//
// Created by weijing on 2019/11/6.
//

#ifndef SKY_CPUINFO_H
#define SKY_CPUINFO_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#if defined(__linux__)
#ifndef __USE_GNU
#define __USE_GNU
#endif
#include <sched.h>

typedef cpu_set_t sky_cpu_set_t;

#define sky_setaffinity(_c)   sched_setaffinity(0, sizeof(sky_cpu_set_t), _c)
#elif defined(__FreeBSD__) || defined(__APPLE__)

#include <sys/cpuset.h>
typedef cpuset_t sky_cpu_set_t;
#define sky_setaffinity(_c) \
    cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(cpuset_t), _c)
#endif

extern sky_uint_t sky_cache_line_size;

void sky_cpu_info(void);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_CPUINFO_H
