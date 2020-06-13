//
// Created by ibeli on 2020/6/10.
//

#ifndef SKY_MATERIX_H
#define SKY_MATERIX_H

#include "../core/palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif
typedef double sky_matrix_data_t;

typedef struct {
    sky_uint32_t rows;
    sky_uint32_t cols;
    sky_uint32_t num;
    sky_matrix_data_t *vs;
} sky_matrix_t;

/**
 * 矩阵加法
 * @param from 矩阵A，该值会改变
 * @param to   矩阵B，
 * @return 是否成功
 */
sky_bool_t sky_matrix_add(sky_matrix_t *from, const sky_matrix_t *to);

/**
 * 矩阵加法，并创建新矩阵
 * @param pool 内存池
 * @param from 矩阵A
 * @param to   矩阵B
 * @return 新的矩阵
 */
sky_matrix_t *sky_matrix_add2(sky_pool_t *pool, const sky_matrix_t *from, const sky_matrix_t *to);

/**
 * 矩阵减法
 * @param from 矩阵A，该值会改变
 * @param to   矩阵B
 * @return 是否成功
 */
sky_bool_t sky_matrix_sub(sky_matrix_t *from, const sky_matrix_t *to);

/**
 * 矩阵减法，并创建新矩阵
 * @param pool 内存池
 * @param from 矩阵A
 * @param to   矩阵B
 * @return 新的矩阵
 */
sky_matrix_t *sky_matrix_sub2(sky_pool_t *pool, const sky_matrix_t *from, const sky_matrix_t *to);

/**
 * 矩阵乘法
 * @param pool 内存池
 * @param a    矩阵A
 * @param b    矩阵B
 * @return 新的矩阵
 */
sky_matrix_t *sky_matrix_mul(sky_pool_t *pool, const sky_matrix_t *a, const sky_matrix_t *b);

/**
 * 矩阵数乘
 * @param matrix 矩阵，该值会改变
 * @param value  值
 */
void sky_matrix_mul_num(sky_matrix_t *matrix, sky_matrix_data_t value);

/**
 * 矩阵数乘，并创建矩阵
 * @param pool    内存池
 * @param matrix  矩阵
 * @param value   值
 * @return 新的矩阵
 */
sky_matrix_t *sky_matrix_mul_num2(sky_pool_t *pool, const sky_matrix_t *matrix, sky_matrix_data_t value);

/**
 * 矩阵置换
 * @param pool   内存池
 * @param matrix 矩阵
 * @return 新的矩阵
 */
sky_matrix_t *sky_matrix_trans(sky_pool_t *pool, const sky_matrix_t *matrix);

/**
 * 矩阵行列式
 * @param matrix 矩阵
 * @return
 */
sky_matrix_data_t sky_matrix_det(const sky_matrix_t *matrix);
//
//float MatDet(Mat* mat);
//Mat* MatAdj(Mat* src, Mat* dst);
//Mat* MatInv(Mat* src, Mat* dst);
//
//void MatCopy(Mat* src, Mat* dst);
#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_MATERIX_H
