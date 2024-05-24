//
// Created by weijing on 2024/3/7.
//

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "./unix_io.h"

#include <sys/fcntl.h>
#include <sys/resource.h>
#include <core/log.h>
#include <sys/time.h>

#ifndef OPEN_MAX
#ifdef NOFILE
#define OPEN_MAX NOFILE
#else
#define OPEN_MAX 65535
#endif
#endif


sky_api sky_inline sky_i64_t
sky_ev_now_sec(sky_ev_loop_t *ev_loop) {
    return ev_loop->current_time.tv_sec;
}

sky_api sky_inline void
sky_ev_timeout_init(sky_ev_loop_t *ev_loop, sky_timer_wheel_entry_t *timer, sky_timer_wheel_pt cb) {
    sky_timer_entry_init(timer, ev_loop->timer_ctx, cb);
}

sky_api sky_inline void
sky_event_timeout_set(sky_ev_loop_t *ev_loop, sky_timer_wheel_entry_t *timer, sky_u32_t timeout) {
    sky_timer_wheel_link(timer, ev_loop->current_step + timeout);
}

sky_bool_t
set_socket_nonblock(sky_socket_t fd) {
    sky_i32_t flags;

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | FD_CLOEXEC) < 0)) {
        return false;
    }

    flags = fcntl(fd, F_GETFD);

    if (sky_unlikely(flags < 0)) {
        return false;
    }

    if (sky_unlikely(fcntl(fd, F_SETFD, flags | O_NONBLOCK) < 0)) {
        return false;
    }

    return true;
}

sky_i32_t
setup_open_file_count_limits() {
    struct rlimit r;

    if (getrlimit(RLIMIT_NOFILE, &r) < 0) {
        sky_log_error("Could not obtain maximum number of file descriptors. Assuming %d", OPEN_MAX);
        return OPEN_MAX;
    }

    if (r.rlim_max != r.rlim_cur) {
        const rlim_t current = r.rlim_cur;

        if (r.rlim_max == RLIM_INFINITY) {
            r.rlim_cur = OPEN_MAX;
        } else if (r.rlim_cur < r.rlim_max) {
            r.rlim_cur = r.rlim_max;
        } else {
            /* Shouldn't happen, so just return the current value. */
            return (sky_i32_t) r.rlim_cur;
        }

        if (setrlimit(RLIMIT_NOFILE, &r) < 0) {
            sky_log_error("Could not raise maximum number of file descriptors to %lu. Leaving at %lu", r.rlim_max,
                          current);
            r.rlim_cur = current;
        }
    }
    return (sky_i32_t) r.rlim_cur;
}

void
init_time(sky_ev_loop_t *ev_loop) {
    ev_loop->current_step = 0;
    gettimeofday(&ev_loop->current_time, null);
}

void
update_time(sky_ev_loop_t *ev_loop) {
    struct timeval current;
    gettimeofday(&current, null);
    ev_loop->current_step += (sky_u64_t) ((current.tv_sec - ev_loop->current_time.tv_sec) * 1000)
                             + (sky_u64_t) ((current.tv_usec - ev_loop->current_time.tv_usec) / 1000);

    ev_loop->current_time = current;
}

#endif
