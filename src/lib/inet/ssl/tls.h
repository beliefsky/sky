//
// Created by beliefsky on 2023/4/21.
//

#ifndef SKY_TLS_H
#define SKY_TLS_H

#include "../tcp.h"

#if defined(__cplusplus)
extern "C" {
#endif


void sky_tls_library_init();

void sky_tls_client_ctx_init(sky_tcp_ctx_t *ctx);

void sky_tls_ctx_destroy(sky_tcp_ctx_t *ctx);

/**
 * 初始化tls,在tcp_open或tcp_accept之后调用
 * @param tls tls连接
 */
void sky_tls_init(sky_tcp_t *tls);

/**
 * 客户端tls握手,在tcp_connect之后调用
 * @param tls tls连接
 * @return 状态, -1异常，0 等待, 1 完成
 */
sky_i8_t sky_tls_connect(sky_tcp_t *tls);

/**
 * 服务端tls握手,在tcp_accept之后调用
 * @param tls tls连接
 * @return 状态, -1异常，0 等待, 1 完成
 */
sky_i8_t sky_tls_accept(sky_tcp_t *tls);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TLS_H
