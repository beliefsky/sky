//
// Created by ibeli on 2020/6/10.
//

#include "matrix.h"
#include "../core/log.h"
#include "../core/memory.h"

sky_bool_t
sky_matrix_add(sky_matrix_t *from, const sky_matrix_t *to) {
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


sky_matrix_t *
sky_matrix_add2(sky_pool_t *pool, const sky_matrix_t *from, const sky_matrix_t *to) {
    sky_uint32_t i;
    sky_matrix_t *c;
    sky_matrix_data_t *av, *bv, *cv;

    if (sky_unlikely(from->rows != to->rows || from->cols != to->cols)) {
        sky_log_error("matrix add: from->rows != to->rows or from->cols != to->cols");
        return null;
    }
    i = from->num;
    av = from->vs;
    bv = to->vs;

    c = sky_palloc(pool, sizeof(sky_matrix_t));
    c->rows = from->rows;
    c->cols = from->cols;
    c->num = i;
    c->vs = cv = sky_pnalloc(pool, i * sizeof(sky_matrix_data_t));

    while (i--) {
        cv[i] = av[i] + bv[i];
    }

    return c;
}

sky_bool_t
sky_matrix_sub(sky_matrix_t *from, const sky_matrix_t *to) {
    sky_uint32_t i;
    sky_matrix_data_t *av, *bv;

    if (sky_unlikely(from->rows != to->rows || from->cols != to->cols)) {
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
sky_matrix_sub2(sky_pool_t *pool, const sky_matrix_t *from, const sky_matrix_t *to) {
    sky_uint32_t i;
    sky_matrix_t *c;
    sky_matrix_data_t *av, *bv, *cv;

    if (sky_unlikely(from->rows != to->rows || from->cols != to->cols)) {
        sky_log_error("matrix sub: from->rows != to->rows or from->cols != to->cols");
        return null;
    }

    i = from->num;
    av = from->vs;
    bv = to->vs;

    c = sky_palloc(pool, sizeof(sky_matrix_t));
    c->rows = from->rows;
    c->cols = from->cols;
    c->num = i;
    c->vs = cv = sky_pnalloc(pool, i * sizeof(sky_matrix_data_t));

    while (i--) {
        cv[i] = av[i] - bv[i];
    }

    return c;
}


sky_matrix_t *
sky_matrix_mul(sky_pool_t *pool, const sky_matrix_t *a, const sky_matrix_t *b) {
    sky_uint32_t i, j, k;
    sky_uint32_t ai, ci, bt, at, ct;
    sky_matrix_t *c;
    sky_matrix_data_t *av, *bv, *cv;

    if (sky_unlikely(a->cols != b->rows)) {
        sky_log_error("matrix mul: a->rows != b->cols");
        return null;
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


sky_matrix_t *
sky_matrix_mul_num2(sky_pool_t *pool, const sky_matrix_t *matrix, sky_matrix_data_t value) {
    sky_uint32_t i;
    sky_matrix_t *c;
    sky_matrix_data_t *mv, *cv;

    i = matrix->num;
    mv = matrix->vs;

    c = sky_palloc(pool, sizeof(sky_matrix_data_t));
    c->rows = matrix->rows;
    c->cols = matrix->cols;
    c->num = i;
    c->vs = cv = sky_pnalloc(pool, i * sizeof(sky_matrix_t));

    while (i--) {
        cv[i] = mv[i] * value;
    }
    return c;
}

sky_matrix_t *
sky_matrix_trans(sky_pool_t *pool, const sky_matrix_t *matrix) {

    sky_uint32_t i, j, k, t, ar, jt, bc;
    sky_matrix_t *b;
    sky_matrix_data_t *va, *vb;

    k = 0;
    ar = 0;
    i = bc = matrix->rows;
    jt = matrix->cols;
    va = matrix->vs;

    b = sky_palloc(pool, sizeof(sky_matrix_t));
    b->rows = jt;
    b->cols = i;
    b->num = matrix->num;
    b->vs = vb = sky_pnalloc(pool, b->num * sizeof(sky_matrix_data_t));

    while (i--) {
        t = ar;
        j = jt;
        while (j--) {
            vb[t] = va[k++];
            t += bc;
        }
        ++ar;
    }

    return b;
}


sky_matrix_data_t
sky_matrix_det(const sky_matrix_t *matrix) {
    sky_matrix_data_t det = 0;

    if (matrix->rows != matrix->cols) {
        sky_log_error("matrix det: a->rows != b->cols");
        return 0;
    }

    return det;
}


sky_matrix_t *
sky_matrix_adj(sky_pool_t *pool, const sky_matrix_t *matrix) {
    return null;
}

sky_matrix_t *
sky_matrix_inv(sky_pool_t *pool, const sky_matrix_t *matrix) {
    return null;
}


sky_matrix_t *
sky_matrix_copy(sky_pool_t *pool, const sky_matrix_t *matrix) {
    sky_size_t size;
    sky_matrix_t *out;

    size = matrix->num * sizeof(sky_matrix_data_t);

    out = sky_palloc(pool, sizeof(sky_matrix_t));
    out->rows = matrix->rows;
    out->cols = matrix->cols;
    out->num = matrix->num;
    out->vs = sky_pnalloc(pool, size);

    sky_memcpy(matrix->vs, out->vs, size);

    return out;
}