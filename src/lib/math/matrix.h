//
// Created by ibeli on 2020/6/10.
//

#ifndef SKY_MATERIX_H
#define SKY_MATERIX_H

#include "../core/palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif
typedef double sky_matrix_v_t;

typedef struct {
    sky_uint32_t rows;
    sky_uint32_t cols;
    sky_uint32_t num;
    sky_matrix_v_t *vs;
} sky_matrix_t;

sky_bool_t sky_matrix_add(sky_matrix_t *from, sky_matrix_t *to);

sky_bool_t sky_matrix_sub(sky_matrix_t *from, sky_matrix_t *to);

sky_matrix_t *sky_matrix_mul(sky_pool_t *pool, sky_matrix_t *ma, sky_matrix_t *mb);

void sky_matrix_mul_num(sky_matrix_t *matrix, sky_matrix_v_t value);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_MATERIX_H
