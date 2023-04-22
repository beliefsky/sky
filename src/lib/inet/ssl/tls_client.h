//
// Created by beliefsky on 2023/4/22.
//

#ifndef SKY_TLS_CLIENT_H
#define SKY_TLS_CLIENT_H

#include "../tcp_client.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_bool_t sky_tls_client_connect(sky_tcp_client_t *client);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TLS_CLIENT_H
