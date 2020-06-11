//
// Created by ibeli on 2020/6/10.
//

#include "matrix.h"
#include "../core/log.h"

sky_bool_t
sky_matrix_add(sky_matrix_t *from, sky_matrix_t *to) {
    if (sky_unlikely(from->rows != to->rows || from->cols != to->cols)) {
        sky_log_error("matrix add: from->rows != to->rows or from->cols != to->cols");
        return false;
    }

    for (sky_uint32_t index = from->num; index; --index) {
        to->vs[index] += from->vs[index];
    }
    return true;
}

sky_bool_t
sky_matrix_sub(sky_matrix_t *from, sky_matrix_t *to) {
    if (sky_unlikely(from->rows != to->rows && from->cols != to->cols)) {
        sky_log_error("matrix sub: from->rows != to->rows or from->cols != to->cols");
        return false;
    }
    for (sky_uint32_t index = from->num; index; --index) {
        to->vs[index] -= from->vs[index];
    }
    return true;
}


sky_matrix_t *
sky_matrix_mul(sky_pool_t *pool, sky_matrix_t *ma, sky_matrix_t *mb) {
    if (sky_unlikely(ma->rows != mb->cols)) {
        sky_log_error("matrix mul: from->rows != to->rows or from->cols != to->cols");
        return false;
    }
    sky_matrix_t *result = sky_palloc(pool, sizeof(sky_matrix_t));
    result->rows = result->cols = ma->rows;
    result->num = result->rows * result->cols;
    result->vs = sky_pnalloc(pool, sizeof(sky_matrix_v_t) * result->num);

    sky_uint32_t i, j;
    sky_uint32_t bi;


    for (i = result->rows; i; --i) {
        for (j = mb->num; j;) {
            mb->vs[j];
        }
    }
}

void
sky_matrix_mul_num(sky_matrix_t *matrix, sky_matrix_v_t value) {
    for (sky_uint32_t index = matrix->num; index; --index) {
        matrix->vs[index] *= value;
    }
}