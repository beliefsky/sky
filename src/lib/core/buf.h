//
// Created by weijing on 17-11-13.
//

#ifndef SKY_BUF_H
#define SKY_BUF_H

#include "types.h"
#include "palloc.h"

#if defined(__cplusplus)
extern "C" {
#endif


typedef struct sky_buf_s sky_buf_t;

// sky_buf_s是nginx用于处理大数据的关键数据结构
// 它既应用于内存数据，也应用于磁盘数据。
struct sky_buf_s {
    // 处理内存数据
    sky_uchar_t* pos;       //告知需要处理的内存数据的起始位置
    sky_uchar_t* last;      //告知需要处理的内存数据的结束位置，即希望处理的数据为[pos,last)

    // 处理内存数据
    sky_uchar_t* start;     //当一整块内存被包含在多个buf中的时候，那么这些buf里面的start和end都指向这块内存的起始位置
    //和终止位置，和pos不同，pos会大于等于start
    sky_uchar_t* end;       //见start分析，和last不同，last会小于等于end

    sky_pool_t* pool;
};

#define sky_buf_reset(_buf)                      \
    (_buf)->pos = (_buf)->last = (_buf)->start


void sky_buf_init(sky_buf_t* buf, sky_pool_t* pool, sky_uint32_t size);

sky_buf_t* sky_buf_create(sky_pool_t* pool, sky_uint32_t size);

void sky_buf_rebuild(sky_buf_t* buf, sky_uint32_t size);

void sky_buf_destroy(sky_buf_t* buf);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_BUF_H
