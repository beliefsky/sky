//
// Created by weijing on 2024/4/24.
//

#ifndef SKY_RING_BUF_H
#define SKY_RING_BUF_H

#include "./types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_ring_buf_s sky_ring_buf_t;

sky_ring_buf_t *sky_ring_buf_create(sky_u32_t capacity);

void sky_ring_buf_destroy(sky_ring_buf_t *rb);

sky_bool_t sky_ring_is_full(const sky_ring_buf_t *rb);

sky_bool_t sky_ring_is_empty(const sky_ring_buf_t *rb);

sky_u32_t sky_ring_buf_read(sky_ring_buf_t *rb, sky_uchar_t *data, sky_u32_t size);

sky_u32_t sky_ring_buf_write(sky_ring_buf_t *rb, const sky_uchar_t *data, sky_u32_t size);


sky_u32_t sky_ring_buf_read_buf(sky_ring_buf_t *rb, sky_uchar_t *buf[2], sky_u32_t size[2]);

/**
 * 回去可写入的buf指针
 *
 * @param rb ring buff
 * @param buf 回写的buf指针，保证有两个指针
 * @param size 回写的buf长度，保证有两个指针
 * @return buf的数量, 范围 0~2
 */
sky_u32_t sky_ring_buf_write_buf(sky_ring_buf_t *rb, sky_uchar_t *buf[2], sky_u32_t size[2]);

sky_u32_t sky_ring_buf_commit_read(sky_ring_buf_t *rb, sky_u32_t size);

/**
 * 提交写入的字节数，(sky_ring_buf_write_buf自定义处理的后续提交)
 *
 * @param rb ring buff
 * @param size 需要提交的字节数
 * @return 实际提交的字节数
 */
sky_usize_t sky_ring_buf_commit_write(sky_ring_buf_t *rb, sky_u32_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_RING_BUF_H
