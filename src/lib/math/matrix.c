//
// Created by ibeli on 2020/6/10.
//

#include "matrix.h"
#include "../core/log.h"

sky_bool_t
sky_matrix_add(sky_matrix_t *from, sky_matrix_t *to) {
    sky_uint32_t i;
    sky_matrix_data_t *av, *bv;

    if (sky_unlikely(from->rows != to->rows || from->cols != to->cols)) {
        sky_log_error("matrix add: from->rows != to->rows or from->cols != to->cols");
        return false;
    }

    i = from->num;
    av = from->vs;
    bv = to->vs;
    while (i--) {
        av[i] += bv[i];
    }
    return true;
}

sky_bool_t
sky_matrix_sub(sky_matrix_t *from, sky_matrix_t *to) {
    sky_uint32_t i;
    sky_matrix_data_t *av, *bv;

    if (sky_unlikely(from->rows != to->rows && from->cols != to->cols)) {
        sky_log_error("matrix sub: from->rows != to->rows or from->cols != to->cols");
        return false;
    }

    i = from->num;
    av = from->vs;
    bv = to->vs;
    while (i--) {
        av[i] -= bv[i];
    }
    return true;
}


sky_matrix_t *
sky_matrix_mul(sky_pool_t *pool, sky_matrix_t *a, sky_matrix_t *b) {
    sky_uint32_t i, j, k;
    sky_uint32_t ai, ci, bt, at, ct;
    sky_matrix_t *c;
    sky_matrix_data_t *av, *bv, *cv;

    if (sky_unlikely(a->cols != b->rows)) {
        sky_log_error("matrix mul: a->rows != b->cols");
        return false;
    }
    c = sky_palloc(pool, sizeof(sky_matrix_t));
    c->rows = a->rows;
    c->cols = b->cols;
    c->num = c->rows * c->cols;
    c->vs = sky_pcalloc(pool, c->num * sizeof(sky_matrix_data_t));

    i = c->rows;

    ai = a->num - 1;
    ci = c->num - 1;
    av = a->vs;
    bv = b->vs;
    cv = c->vs;
    while (i--) {
        bt = b->num - 1;
        at = ai;
        j = a->cols;
        while (j--) {
            ct = ci;
            k = b->cols;
            while (k--) {
                cv[ct--] += av[at] * bv[bt--];
            }
            --at;
        }
        ci -= c->cols;
        ai -= a->cols;
    }
    return c;

}

void
sky_matrix_mul_num(sky_matrix_t *matrix, sky_matrix_data_t value) {
    sky_uint32_t i;
    sky_matrix_data_t *mv;

    i = matrix->num;
    mv = matrix->vs;
    while (i--) {
        mv[i] *= value;
    }
}