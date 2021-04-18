//
// Created by edz on 2021/2/1.
//

#ifndef SKY_HTTP_IO_WRAPPERS_H
#define SKY_HTTP_IO_WRAPPERS_H

#include "http_server.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_size_t http_read(sky_http_connection_t *conn, sky_uchar_t *data, sky_size_t size);

void http_write(sky_http_connection_t *conn, const sky_uchar_t *data, sky_size_t size);

void
http_send_file(sky_http_connection_t *conn, sky_int32_t fd, sky_int64_t offset, sky_size_t size,
               const sky_uchar_t *header, sky_uint32_t header_len);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_IO_WRAPPERS_H
