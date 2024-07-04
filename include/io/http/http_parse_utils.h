//
// Created by weijing on 2024/7/4.
//

#ifndef SKY_HTTP_PARSE_UTILS_H
#define SKY_HTTP_PARSE_UTILS_H

#include "./http_base.h"

#include "../../core/palloc.h"
#include "../../core/list.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_multipart_parser_s sky_http_multipart_parser_t;
typedef enum sky_http_multipart_result_s sky_http_multipart_result_t;


enum sky_http_multipart_result_s {
    MULTIPART_PENDING = 0,
    MULTIPART_HEADERS,
    MULTIPART_DATA,
    MULTIPART_END,
    MULTIPART_ERROR
};

struct sky_http_multipart_parser_s {
    sky_list_t *headers;

    sky_usize_t boundary_len;
    sky_usize_t boundary_offset;

    sky_str_t *content_type;
    sky_str_t *content_disposition;

    sky_pool_t *pool;

    void *str_buf;

    union {
        struct {
            const sky_uchar_t *data;
            sky_usize_t data_size;
        };
        sky_str_t tmp_header_name;
    };

    sky_http_multipart_result_t result;

    sky_u32_t state;

    sky_uchar_t boundary[];
};

#define sky_http_multipart_header_foreach(_m, _item, _code) \
    sky_http_header_foreach((_m)->headers, _item, _code)


/**
 * 解析http参数
 * @param pool 内存池
 * @param data 待处理的字符串
 * @param decode 是否转义解码
 * @return 参数集合，可用 sky_http_req_params_foreach 进行遍历
 */
sky_list_t *sky_http_parse_params(sky_pool_t *pool, sky_str_t *data, sky_bool_t decode);


/**
 * 创建multipart流解析器
 * @param pool  内存池
 * @param boundary 边界标识
 * @return 流解析器
 */
sky_http_multipart_parser_t *sky_http_multipart_parse_create(sky_pool_t *pool, const sky_str_t *boundary);

/**
 * 读取 multipart 内容
 * @param m  流解析器
 * @param buf 待解析数据
 * @param size 待解析数据字节数
 * @return 已经解析的字节数
 */
sky_usize_t sky_http_multipart_parse_exec(
        sky_http_multipart_parser_t *m,
        const sky_uchar_t *buf,
        sky_usize_t size
);

static sky_inline sky_http_multipart_result_t
sky_http_multipart_result(sky_http_multipart_parser_t *const m) {
    return m->result;
}

static sky_inline sky_list_t *
sky_http_multipart_headers(sky_http_multipart_parser_t *const m) {
    return m->headers;
}

static sky_inline sky_str_t *
sky_http_multipart_content_type(sky_http_multipart_parser_t *const m) {
    return m->content_type;
}

static sky_inline sky_str_t *
sky_http_multipart_content_disposition(sky_http_multipart_parser_t *const m) {
    return m->content_disposition;
}

static sky_inline const sky_uchar_t *
sky_http_multipart_data(sky_http_multipart_parser_t *const m, sky_usize_t *bytes) {
    *bytes = m->data_size;
    return m->data;
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_PARSE_UTILS_H
