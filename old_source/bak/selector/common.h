//
// Created by beliefsky on 2023/7/29.
//

#ifndef SKY_COMMON_H
#define SKY_COMMON_H

#include <io/selector.h>


#if defined(SKY_HAVE_EPOLL)

#define SELECTOR_USE_EPOLL

#elif defined(SKY_HAVE_KQUEUE)

#define SELECTOR_USE_KQUEUE

#else

#error Unsupported platform.

#endif

#endif //SKY_COMMON_H
