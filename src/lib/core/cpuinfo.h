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

#include <sched.h>

typedef cpu_set_t sky_cpu_set_t;

#define sky_setaffinity(_c) \
    sched_setaffinity(0, sizeof(sky_cpu_set_t), _c)
#elif defined(__FreeBSD__)

#include <sys/param.h>
#include <sys/cpuset.h>

typedef cpuset_t sky_cpu_set_t;

#define sky_setaffinity(_c) \
    cpuset_setaffinity(CPU_LEVEL_WHICH, CPU_WHICH_PID, -1, sizeof(cpuset_t), _c)
#elif defined(__APPLE__)

typedef struct {
  sky_uint32_t  count;
} sky_cpu_set_t;

static inline void
CPU_ZERO(sky_cpu_set_t *cs) {
    cs->count = 0;
}

static inline void
CPU_SET(sky_int32_t num, sky_cpu_set_t *cs) {
    cs->count |= (1 << num);
}

static inline int
CPU_ISSET(sky_int32_t num, sky_cpu_set_t *cs) {
    return (cs->count & (1 << num));
}

int
sky_setaffinity(sky_cpu_set_t *cpu_set) {
    sky_int32_t core_count = 0;
    sky_size_t len = sizeof(sky_int32_t);
    if (sysctlbyname("machdep.cpu.core_count", &core_count, &len, 0, 0)) {
        return -1;
    }
    cpu_set->count = 0;
    for (int i = 0; i < core_count; i++) {
        cpu_set->count |= (1 << i);
    }

    return 0;
}


#endif

extern sky_uint_t sky_cache_line_size;

void sky_cpu_info(void);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_CPUINFO_H
