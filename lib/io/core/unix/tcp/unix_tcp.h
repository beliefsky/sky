//
// Created by weijing on 2024/5/9.
//

#ifndef SKY_UNIX_TCP_H
#define SKY_UNIX_TCP_H

#ifdef __unix__

#include "../unix_io.h"
#include <io/tcp.h>

#define TCP_STATUS_READ         SKY_U32(0x00001000)
#define TCP_STATUS_WRITE        SKY_U32(0x00002000)
#define TCP_STATUS_EOF          SKY_U32(0x00004000)
#define TCP_STATUS_ERROR        SKY_U32(0x00008000)
#define TCP_STATUS_CLOSING      SKY_U32(0x00010000)
#define TCP_STATUS_CONNECTING   SKY_U32(0x00020000)

#define TCP_STATUS_CONNECTED    SKY_U32(0x01000000)

#endif

#endif //SKY_UNIX_TCP_H
