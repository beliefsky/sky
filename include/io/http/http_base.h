//
// Created by weijing on 2024/7/4.
//

#ifndef SKY_HTTP_BASE_H
#define SKY_HTTP_BASE_H

#include "../../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_http_header_s sky_http_header_t;
typedef struct sky_http_header_s sky_http_param_t;

struct sky_http_header_s {
    sky_str_t key;
    sky_str_t val;
};


#define sky_http_header_foreach(_l, _item, _code) \
    sky_list_foreach(_l, sky_http_header_t, _item, _code)

#define sky_http_params_foreach(_l, _item, _code) \
    sky_list_foreach(_l, sky_http_param_t, _item, _code)

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_HTTP_BASE_H
