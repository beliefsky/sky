//
// Created by beliefsky on 2023/4/25.
//

#ifndef SKY_DNS_H
#define SKY_DNS_H

#include "../../event/event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_dns_s sky_dns_t;

typedef struct {
    sky_inet_addr_t *addr;
} sky_dns_conf_t;

sky_dns_t *sky_dns_create(sky_event_loop_t *loop, const sky_dns_conf_t *conf);

void sky_dns_destroy(sky_dns_t *dns);

void test(sky_dns_t *dns);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_DNS_H
