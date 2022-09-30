//
// Created by edz on 2022/9/30.
//
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "process.h"
#include "log.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/prctl.h>


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

#define CPU_SETSIZE 1024

#include <sys/types.h>
#include <sys/sysctl.h>

typedef struct {
  sky_u32_t  count;
} sky_cpu_set_t;

static inline void
CPU_ZERO(sky_cpu_set_t *cs) {
    cs->count = 0;
}

static inline void
CPU_SET(sky_i32_t num, sky_cpu_set_t *cs) {
    cs->count |= (1 << num);
}

static inline int
CPU_ISSET(sky_i32_t num, sky_cpu_set_t *cs) {
    return (cs->count & (1 << num));
}

static int
sky_setaffinity(sky_cpu_set_t *cpu_set) {
    sky_i32_t core_count = 0;
    sky_usize_t len = sizeof(sky_i32_t);
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

static void fork_handler(int sig);


static sky_bool_t init = false;

sky_i32_t
sky_process_fork() {
    if (sky_unlikely(!init)) {
        struct sigaction act;
        act.sa_handler = fork_handler;
        sigemptyset(&act.sa_mask);
        act.sa_flags = 0;
        sigaction(SIGCHLD, &act, 0);

        init = true;
    }
    const pid_t pid = fork();
    if (pid == 0) {
        prctl(PR_SET_PDEATHSIG,SIGKILL);
    }

    return pid;
}

sky_i32_t sky_process_bind_cpu(sky_i32_t cpu) {
    if (cpu >= 0) {
        sky_cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(cpu, &mask);
        for (sky_i32_t j = 0; j < CPU_SETSIZE; ++j) {
            if (CPU_ISSET(j, &mask)) {
                sky_setaffinity(&mask);

                return j;
            }
        }
    }
    return -1;
}


static void
fork_handler(int sig) {
    int state;
    sky_log_info("11111111111111");
    waitpid(-1, &state, WNOHANG);
}
