//
// Created by weijing on 2024/5/9.
//

#ifndef SKY_UNIX_TCP_H
#define SKY_UNIX_TCP_H

#if defined(__unix__) || defined(__unix) || (defined(__APPLE__) && defined(__MACH__))

#include "../unix_io.h"
#include <io/tcp.h>


#define TCP_STATUS_READ             SKY_U32(0x00001000)
#define TCP_STATUS_WRITE            SKY_U32(0x00002000)
#define TCP_STATUS_CONNECTING       SKY_U32(0x00004000)

#endif

#endif //SKY_UNIX_TCP_H
