//
// Created by edz on 2022/3/4.
//

#ifndef SKY_ATOMIC_H
#define SKY_ATOMIC_H

#if defined(__cplusplus)
extern "C" {
#endif

#define sky_atomic_sub_and_get(_ptr, _val) __sync_sub_and_fetch(_ptr, _val)

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_ATOMIC_H
