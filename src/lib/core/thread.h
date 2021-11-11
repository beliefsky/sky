//
// Created by edz on 2021/11/4.
//

#ifndef SKY_THREAD_H
#define SKY_THREAD_H

#include <pthread.h>
#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_THREAD_SCOPE_SYSTEM PTHREAD_SCOPE_SYSTEM
#define SKY_THREAD_SCOPE_PROCESS PTHREAD_SCOPE_PROCESS


typedef pthread_t sky_thread_t;
typedef pthread_attr_t sky_thread_attr_t;

typedef void *(*sky_thread_pt)(void *);


static sky_inline void
sky_thread_create(sky_thread_t *thread, sky_thread_attr_t *attr, sky_thread_pt handle, void *data) {
    pthread_create(thread, attr, handle, data);
}

static sky_inline sky_thread_t
sky_thread_self() {
    return pthread_self();
}

void sky_thread_set_cpu(sky_thread_t thread, sky_i32_t n);

static sky_inline void
sky_thread_join(sky_thread_t thread, void **data_ptr) {
    pthread_join(thread, data_ptr);
}


static sky_inline void
sky_thread_attr_init(sky_thread_attr_t *attr) {
    pthread_attr_init(attr);
}

static sky_inline void
sky_thread_attr_set_scope(sky_thread_attr_t *attr, sky_i32_t scope) {
    pthread_attr_setscope(attr, scope);
}

static sky_inline void
sky_thread_attr_set_stack_size(sky_thread_attr_t *attr, sky_usize_t size) {
    pthread_attr_setstacksize(attr, size);
}

static sky_inline void
sky_thread_attr_destroy(sky_thread_attr_t *attr) {
    pthread_attr_destroy(attr);
}


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_THREAD_H
