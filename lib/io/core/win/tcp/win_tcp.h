//
// Created by weijing on 2024/5/9.
//

#ifndef SKY_WIN_TCP_H
#define SKY_WIN_TCP_H

#ifdef __WINNT__

#include "../win_io.h"
#include <io/tcp.h>

#define TCP_STATUS_BIND         SKY_U32(0x01000000)
#define TCP_STATUS_CONNECTED    SKY_U32(0x02000000)
#define TCP_STATUS_READING      SKY_U32(0x04000000)
#define TCP_STATUS_WRITING      SKY_U32(0x08000000)
#define TCP_STATUS_EOF          SKY_U32(0x10000000)
#define TCP_STATUS_ERROR        SKY_U32(0x20000000)
#define TCP_STATUS_CLOSING      SKY_U32(0x40000000)
#define TCP_STATUS_LISTENER     SKY_U32(0x80000000)

#define TCP_TYPE_MASK           SKY_U32(0x0000FFFF)


sky_socket_t create_socket(sky_i32_t domain);

#endif
#endif //SKY_WIN_TCP_H
