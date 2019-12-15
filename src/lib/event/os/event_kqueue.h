//
// Created by weijing on 2019/12/14.
//

#ifndef SKY_EVENT_KQUEUE_H
#define SKY_EVENT_KQUEUE_H

#include <sys/event.h>
#include "../../core/types.h"

typedef struct kevent event_t;

/**
 * 创建事件触发器
 */
#define sky_event_create()  kqueue()
/**
 * 监听事件触发器
 */
#define sky_event_wait(_ev_fd, _events, _max_events, _timeout)      \
    kevent((_ev_fd), null, 0,                                       \
            (_events), (_max_events),                               \
            (&(struct timespec){.tv_sec = (_timeout == -1 ? 3600 : _timeout), .tv_nsec = 0}))

#define sky_event_data(_ev)   (_ev)->udata

#define sky_event_is_hup(_ev) 0

#define sky_event_is_read(_ev) 1

#define sky_event_is_write(_ev) 1


/**
* 添加事件
*/
#define sky_event_add(_ev_fd, _fd, _data)                                   \
    event_t _ev[2];                                                         \
    EV_SET(_ev, _fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, _data);         \
    EV_SET(_ev + 1, _fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, _data);    \
    kevent(_ev_fd, _ev, 2, null, 0, null)
#endif //SKY_EVENT_KQUEUE_H
