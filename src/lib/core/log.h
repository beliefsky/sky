//
// Created by weijing on 17-11-13.
//

#ifndef SKY_LOG_H
#define SKY_LOG_H
#if defined(__cplusplus)
extern "C" {
#endif

#include <stdio.h>

#define sky_log_info(format, ...)    printf("\033[0;34m[INFO] (%s:%d) " format "\n\033[0m",__FILE__, __LINE__, ##__VA_ARGS__)
#define sky_log_warn(format, ...)    printf("\033[0;32m[WARN] (%s:%d) " format "\n\033[0m", __FILE__, __LINE__, ##__VA_ARGS__)
#define sky_log_error(format, ...)   printf("\033[0;31m[ERROR] (%s:%d) " format "\n\033[0m",__FILE__, __LINE__, ##__VA_ARGS__)

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_LOG_H
