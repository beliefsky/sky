//
// Created by beliefsky on 2023/7/1.
//

#ifndef SKY_HTTP_SERVER_H
#define SKY_HTTP_SERVER_H

#include "../ev_loop.h"
#include "../fs.h"
#include "../../core/string.h"
#include "../../core/palloc.h"
#include "../../core/list.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_HTTP_GET        SKY_U8(0x01)
#define SKY_HTTP_HEAD       SKY_U8(0x02)
#define SKY_HTTP_POST       SKY_U8(0x04)
#define SKY_HTTP_PUT        SKY_U8(0x08)
#define SKY_HTTP_DELETE     SKY_U8(0x10)
#define SKY_HTTP_OPTIONS    SKY_U8(0x20)
#define SKY_HTTP_PATCH      SKY_U8(0x40)
#define SKY_HTTP_CONNECT    SKY_U8(0x80)

typedef struct sky_http_server_conf_s sky_http_server_conf_t;
typedef struct sky_http_server_s sky_http_server_t;
typedef struct sky_http_server_module_s sky_http_server_module_t;
typedef struct sky_http_connection_s sky_http_connection_t;
typedef struct sky_http_server_request_s sky_http_server_request_t;
typedef struct sky_http_server_header_s sky_http_server_header_t;
typedef struct sky_http_server_header_s sky_http_server_param_t;

typedef void (*sky_http_server_module_run_pt)(sky_http_server_request_t *r, void *module_data);

typedef void (*sky_http_server_next_pt)(sky_http_server_request_t *r, void *data);

typedef void (*sky_http_server_next_str_pt)(sky_http_server_request_t *r, sky_str_t *body, void *data);

typedef void (*sky_http_server_rw_pt)(sky_http_server_request_t *r, sky_usize_t size, void *data);


struct sky_http_server_conf_s {
    sky_usize_t body_str_max;
    sky_u32_t sendfile_max_chunk;
    sky_u32_t keep_alive;
    sky_u32_t timeout;
    sky_u32_t header_buf_size;
    sky_u8_t header_buf_n;
};

struct sky_http_server_module_s {
    sky_str_t host;
    sky_str_t prefix;
    sky_http_server_module_run_pt run;
    void *module_data;
};

struct sky_http_server_request_s {
    sky_str_t method_name;
    sky_str_t uri;
    sky_str_t exten;
    sky_str_t args;
    sky_str_t version_name;
    sky_str_t header_name;

    struct {
        sky_list_t headers;

        sky_str_t *host;
        sky_str_t *connection;
        sky_str_t *if_modified_since;
        sky_str_t *content_type;
        sky_str_t *content_length;
        sky_str_t *transfer_encoding;
        sky_str_t *range;
        sky_str_t *if_range;

        sky_usize_t content_length_n;
    } headers_in;

    struct {
        sky_list_t headers;
        sky_str_t content_type;
        sky_u64_t content_length_n;
    } headers_out;

    sky_list_t *params;

    sky_usize_t index;
    sky_uchar_t *req_pos;

    sky_pool_t *pool;
    sky_http_connection_t *conn;

    void *attr_data;

    sky_u32_t state;
    sky_u8_t method;
    sky_bool_t uri_no_decode: 1;
    sky_bool_t arg_no_decode: 1;
    sky_bool_t keep_alive: 1;
    sky_bool_t read_request_body: 1;
    sky_bool_t req_end_chunked: 1;
    sky_bool_t error: 1;
    sky_bool_t response: 1;
    sky_bool_t res_content_length: 1;
    sky_bool_t res_finish: 1;
};

struct sky_http_server_header_s {
    sky_str_t key;
    sky_str_t val;
};


#define sky_http_req_header_foreach(_r, _item, _code) \
    sky_list_foreach(&(_r)->headers_in.headers, sky_http_server_header_t, _item, _code)

#define sky_http_req_params_foreach(_list, _item, _code) \
    sky_list_foreach(_list, sky_http_server_param_t, _item, _code)



sky_http_server_t *sky_http_server_create(sky_ev_loop_t *ev_loop, const sky_http_server_conf_t *conf);

sky_bool_t sky_http_server_module_put(sky_http_server_t *server, sky_http_server_module_t *module);

sky_bool_t sky_http_server_bind(sky_http_server_t *server, const sky_inet_address_t *address);

/**
 * 获取http 请求参数
 *
 * @param r http req
 * @return 请求参数集合, 可用 sky_http_req_params_foreach 进行遍历
 */
sky_list_t *sky_http_req_query_params(sky_http_server_request_t *r);

/**
 * 读取 http body的所有数据，忽略其中的内容

 * @param r http req
 * @param call 回调函数
 * @param data 执行回调时自定义数据
 */
void sky_http_req_body_none(sky_http_server_request_t *r, sky_http_server_next_pt call, void *data);

/**
 * 读取 http body的所有数据，此函数受 body_str_max 参数影响，
 * 大于此参数会自动发送响应 413 并放弃内容
 *
 * @param r http req
 * @param call 回调函数
 * @param data 执行回调时自定义数据
 */
void sky_http_req_body_str(sky_http_server_request_t *r, sky_http_server_next_str_pt call, void *data);

/**
 * 使用原生读取 http body数据
 *
 * @param r http req
 * @param buf 待发送数据
 * @param size 待发送数据字节数
 * @param bytes 已发送字节数，当返回 REQ_SUCCESS 时有效
 * @param call 回调函数，如果不是 REQ_PENDING 时会立即返回，不会触发回调
 * @param data 执行回调时自定义数据
 * @return 调用结果
 */
sky_io_result_t sky_http_req_body_read(
        sky_http_server_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_rw_pt call,
        void *data
);

/**
 * 使用原生读取 http body数据并忽略其中的内容
 *
 * @param r http req
 * @param size 待发送数据字节数
 * @param bytes 已发送字节数，当返回 REQ_SUCCESS 时有效
 * @param call 回调函数，如果不是 REQ_PENDING 时会立即返回，不会触发回调
 * @param data 执行回调时自定义数据
 * @return 调用结果
 */
sky_io_result_t sky_http_req_body_skip(
        sky_http_server_request_t *r,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_rw_pt call,
        void *data
);

/**
 * http 发送响应，针对无内容的http响应，比如304，302之类的
 *
 * @param r http req
 * @param call 回调函数，如果为 null, 会自动调用 sky_http_req_finish 结束
 * @param cb_data 回调自定义数据
 */
void sky_http_res_nobody(sky_http_server_request_t *r, sky_http_server_next_pt call, void *cb_data);

/**
 * http 响应发送数据
 *
 * @param r http req
 * @param data 待待发送数据
 * @param call 回调函数，如果为 null, 会自动调用 sky_http_req_finish 结束
 * @param cb_data 回调自定义数据
 */
void sky_http_res_str(
        sky_http_server_request_t *r,
        const sky_str_t *data,
        sky_http_server_next_pt call,
        void *cb_data
);

/**
 * http 响应发送数据
 *
 * @param r  http req
 * @param data 待待发送数据
 * @param data_len 待发送数据长度
 * @param call 回调函数，如果为 null, 会自动调用 sky_http_req_finish 结束
 * @param cb_data 回调自定义数据
 */
void sky_http_res_str_len(
        sky_http_server_request_t *r,
        sky_uchar_t *data,
        sky_usize_t data_len,
        sky_http_server_next_pt call,
        void *cb_data
);


/**
 * http 响应发送文件
 *
 * @param r  http req
 * @param fs 文件
 * @param offset 偏移字节数
 * @param size 发送字节数
 * @param file_size 整个文件大小(206 按字节偏移发送时需要)
 * @param call 回调函数，如果为 null, 会自动调用 sky_http_req_finish 结束
 * @param cb_data 回调自定义数据
 */
void sky_http_res_file(
        sky_http_server_request_t *r,
        sky_fs_t *fs,
        sky_u64_t offset,
        sky_u64_t size,
        sky_u64_t file_size,
        sky_http_server_next_pt call,
        void *cb_data
);


/**
 * 使用原生方式发送数据，如果未 调用 sky_http_res_set_content_length 则采用 chunked发送。
 * 使用chunked发送完成时，必须再调用一次且 size = 0，表示已发送结束
 *
 * @param r http req
 * @param buf 待发送数据
 * @param size 待发送数据字节数
 * @param bytes 已发送字节数，当返回 REQ_SUCCESS 时有效
 * @param call 回调函数，如果不是 REQ_PENDING 时会立即返回，不会触发回调
 * @param data 执行回调时自定义数据
 * @return 调用结果
 */
sky_io_result_t sky_http_res_write(
        sky_http_server_request_t *r,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_usize_t *bytes,
        sky_http_server_rw_pt call,
        void *data
);

/**
 * http 请求结束，整个请求和响应完成
 *
 * @param r http req
 */
void sky_http_req_finish(sky_http_server_request_t *r);

/**
 * 获取http form 参数，只在content-type=application/x-www-form-urlencoded时生效
 *
 * @param r http req
 * @param body 获取到的body数据
 * @return 请求参数集合, 可用 sky_http_req_params_foreach 进行遍历
 */
sky_list_t *sky_http_req_body_parse_urlencoded(sky_http_server_request_t *r, sky_str_t *body);

/**
 * http 是否有异常，在读取body时可能报文不正确，超时，连接关闭等
 *
 * @param r http req
 * @return 是否异常
 */
static sky_inline sky_bool_t
sky_http_req_error(const sky_http_server_request_t *const r) {
    return r->error;
}

/**
 * 获取http内存池，会随着http请求完成统一回收
 *
 * @param r http req
 * @return  内存池
 */
static sky_inline sky_pool_t *
sky_http_req_pool(const sky_http_server_request_t *const r) {
    return r->pool;
}

/**
 * http req自定义数据设置
 *
 * @param r http req
 * @param data  自定义数据
 */
static sky_inline void
sky_http_req_set_data(sky_http_server_request_t *const r, void *const data) {
    r->attr_data = data;
}

/**
 * http req自定义数据获取
 *
 * @param r http req
 * @return 自定义数据
 */
static sky_inline void *
sky_http_req_get_data(sky_http_server_request_t *const r) {
    return r->attr_data;
}

static sky_inline sky_u8_t
sky_http_req_method(sky_http_server_request_t *const r) {
    return r->method;
}

static sky_inline sky_str_t *
sky_http_req_method_name(sky_http_server_request_t *const r) {
    return &r->method_name;
}

static sky_inline sky_str_t *
sky_http_req_uri(sky_http_server_request_t *const r) {
    return &r->uri;
}

static sky_inline sky_str_t *
sky_http_req_exten(sky_http_server_request_t *const r) {
    return &r->exten;
}

static sky_inline sky_str_t *
sky_http_req_host(sky_http_server_request_t *const r) {
    return r->headers_in.host;
}

static sky_inline sky_str_t *
sky_http_req_connection(sky_http_server_request_t *const r) {
    return r->headers_in.connection;
}

static sky_inline sky_str_t *
sky_http_req_if_modified_since(sky_http_server_request_t *const r) {
    return r->headers_in.if_modified_since;
}

static sky_inline sky_str_t *
sky_http_req_content_type(sky_http_server_request_t *const r) {
    return r->headers_in.content_type;
}

static sky_inline sky_str_t *
sky_http_req_range(sky_http_server_request_t *const r) {
    return r->headers_in.range;
}

static sky_inline sky_str_t *
sky_http_req_if_range(sky_http_server_request_t *const r) {
    return r->headers_in.if_range;
}

static sky_inline void
sky_http_res_set_status(sky_http_server_request_t *const r, const sky_u32_t status) {
    r->state = status;
}

static sky_inline void
sky_http_res_set_content_type(
        sky_http_server_request_t *const r,
        sky_uchar_t *const value,
        const sky_usize_t size
) {
    r->headers_out.content_type.data = value;
    r->headers_out.content_type.len = size;
}

/**
 * 在已知发送数据字节数时设置响应内容的字节数，采用 sky_http_res_write 发送时有效
 *
 * @param r http req
 * @param size  响应内容的字节数
 */
static sky_inline void
sky_http_res_set_content_length(sky_http_server_request_t *const r, const sky_u64_t size) {
    if (sky_likely(!r->response)) {
        r->headers_out.content_length_n = size;
        r->res_content_length = true;
    }
}

static sky_inline void
sky_http_res_add_header(
        sky_http_server_request_t *const r,
        sky_uchar_t *const key,
        const sky_usize_t key_len,
        sky_uchar_t *const val,
        const sky_usize_t val_len
) {
    sky_http_server_header_t *const h = sky_list_push(&r->headers_out.headers);
    h->key.data = key;
    h->key.len = key_len;
    h->val.data = val;
    h->val.len = val_len;
}

static sky_inline sky_http_server_header_t *
sky_http_res_push_header(sky_http_server_request_t *const r) {
    return sky_list_push(&r->headers_out.headers);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_SERVER_H
