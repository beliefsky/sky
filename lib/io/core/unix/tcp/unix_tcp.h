//
// Created by weijing on 2024/5/9.
//

#ifndef SKY_UNIX_TCP_H
#define SKY_UNIX_TCP_H

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "../unix_io.h"
#include <io/tcp.h>


#ifdef EVENT_USE_URING

#define TCP_STATUS_CONNECTING       SKY_U32(0x00002000)
#define TCP_STATUS_SHUTDOWN         SKY_U32(0x00004000)

#else

#define TCP_STATUS_READ             SKY_U32(0x00001000)
#define TCP_STATUS_WRITE            SKY_U32(0x00002000)
#define TCP_STATUS_CONNECTING       SKY_U32(0x00004000)

#endif




static sky_inline void
add_read_task(sky_tcp_cli_t *const cli, sky_tcp_task_t *const task) {
    task->next = null;

    *cli->read_queue_tail = task;
    cli->read_queue_tail = &task->next;
}

static sky_inline void
add_write_task(sky_tcp_cli_t *const cli, sky_tcp_task_t *const task) {
    task->next = null;

    *cli->write_queue_tail = task;
    cli->write_queue_tail = &task->next;
}

#endif

#endif //SKY_UNIX_TCP_H
