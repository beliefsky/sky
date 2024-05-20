//
// Created by weijing on 2024/5/20.
//

#ifndef SKY_CONTEXT_H
#define SKY_CONTEXT_H

#include "./types.h"

#if defined(__cplusplus)
} /* extern "C" { */
#endif

typedef struct sky_context_from_s sky_context_from_t;

typedef void (*sky_context_pt)(sky_context_from_t from);

typedef struct {
    sky_i32_t dump;
} const *sky_context_ref_t;

struct sky_context_from_s {
    sky_context_ref_t context;
    void *data; // the passed user private data
};

sky_context_ref_t sky_context_make(sky_uchar_t *stacks, sky_usize_t size, sky_context_pt cb);

sky_context_from_t sky_context_jump(sky_context_ref_t context, void *data);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_CONTEXT_H
