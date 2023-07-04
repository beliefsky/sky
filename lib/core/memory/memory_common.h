//
// Created by beliefsky on 2023/7/4.
//

#ifndef SKY_MEMORY_COMMON_H
#define SKY_MEMORY_COMMON_H

#include <core/memory.h>

#if defined(__SSE2__)

#ifndef MEMCPY_SSE2
#define MEMCPY_SSE2
#endif

#else

#ifndef MEMCPY_DEFAULT
#define MEMCPY_DEFAULT
#endif

#endif

#endif //SKY_MEMORY_COMMON_H
