//
// Created by weijing on 2020/6/11.
//

#ifndef SKY_MATRIX_H
#define SKY_MATRIX_H

#include <core/palloc.h>

typedef struct {
    sky_uint32_t row;
    sky_uint32_t col;
    sky_uint32_t num;
    double *v;
} matrix_t;

matrix_t *matrix_mul(sky_pool_t *pool, matrix_t *a, matrix_t *b) {
    if (a->col != b->row) {
        return null;
    }
    matrix_t *c = sky_palloc(pool, sizeof(matrix_t));
    c->row = a->row;
    c->col = b->col;
    c->num = c->row * c->col;
    c->v = sky_pcalloc(pool, c->num * sizeof(double));


    sky_uint32_t i, j, k;
    sky_uint32_t ai, ci, bt, at, ct;

    i = c->row;

    ai = a->num - 1;
    ci = c->num - 1;
    while (i--) {
        bt = b->num - 1;
        at = ai;
        j = a->col;
        while (j--) {
            ct = ci;
            k = b->col;
            while (k--) {
                sky_log_info("c[%d] += a[%d] * b[%d]", ct + 1, at + 1, bt + 1);
                c->v[ct--] += a->v[at] * b->v[bt--];
            }
            --at;
            sky_log_info("");
        }
        ci -= c->col;
        ai -= a->col;
        sky_log_info("================================================");
    }
    return c;
}

#endif //SKY_MATRIX_H
