//
// Created by edz on 2022/9/30.
//

#ifndef SKY_PROCESS_H
#define SKY_PROCESS_H

#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_i32_t sky_process_fork();

sky_i32_t sky_process_bind_cpu(sky_i32_t cpu);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_PROCESS_H
