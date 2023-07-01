//
// Created by beliefsky on 2023/7/1.
//

#ifndef SKY_HTTP_SERVER_REQ_H
#define SKY_HTTP_SERVER_REQ_H

#include "http_server_common.h"
#include <io/http/http_server.h>

void http_server_request_process(sky_http_connection_t *conn);

#endif //SKY_HTTP_SERVER_REQ_H
