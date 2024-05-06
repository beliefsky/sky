//
// Created by weijing on 2024/5/6.
//

#ifdef __WINNT__

#include "./coro_common.h"

sky_inline void
sky_coro_set(
        sky_coro_t *coro,
        sky_coro_func_t func,
        void *data
) {
    coro->context.ContextFlags = CONTEXT_FULL;
}


#endif
