//
// Created by weijing on 2024/5/9.
//

#ifndef SKY_WIN_TCP_H
#define SKY_WIN_TCP_H

#ifdef __WINNT__

#include "../win_io.h"
#include <io/tcp.h>

#define TCP_STATUS_READING          SKY_U32(0x00100000)
#define TCP_STATUS_WRITING          SKY_U32(0x00200000)

#define TCP_TYPE_MASK               SKY_U32(0x0000FFFF)


sky_socket_t create_socket(sky_i32_t domain);

#endif
#endif //SKY_WIN_TCP_H
