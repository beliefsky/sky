#if defined(__linux__)

#include "event_loop_epoll.c"

#elif defined(__FreeBSD__) || defined(__APPLE__)
#include "event_loop_kqueue.c"
#endif

void
sky_event_timer_callback(sky_event_t *ev) {
    close(ev->fd);
    ev->reg = false;
    ev->fd = -1;
    ev->close(ev);
}