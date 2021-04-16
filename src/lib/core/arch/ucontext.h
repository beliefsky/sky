//
// Created by edz on 2021/4/16.
//

#ifndef SKY_UCONTEXT_H
#define SKY_UCONTEXT_H

#include <ucontext.h>
typedef ucontext_t sky_ucontext_t;

#define sky_getcontext getcontext
#define sky_makecontext makecontext
#define sky_swapcontext swapcontext


#endif //SKY_UCONTEXT_H
