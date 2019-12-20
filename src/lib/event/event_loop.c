#if defined(__linux__)

#include "event_loop_epoll.c"
#elif defined(__FreeBSD__) || defined(__APPLE__)
#include "event_loop_kqueue.c"
#endif