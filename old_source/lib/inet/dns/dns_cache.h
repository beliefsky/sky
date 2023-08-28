//
// Created by beliefsky on 2023/4/30.
//

#ifndef SKY_DNS_CACHE_H
#define SKY_DNS_CACHE_H

#include "../../core/rbtree.h"
#include "../../core/queue.h"
#include "../../core/string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_dns_cache_s sky_dns_cache_t;
typedef struct sky_dns_host_s sky_dns_host_t;

struct sky_dns_cache_s {
    sky_rb_tree_t host_tree;
};

struct sky_dns_host_s {
    sky_rb_node_t node;
    sky_queue_t ipv4_query;
    sky_str_t host;
    sky_u32_t hash;
    sky_bool_t query_ivp4:1;
};

void sky_dns_cache_init(sky_dns_cache_t *cache);

sky_dns_host_t *sky_dns_cache_host_get(sky_dns_cache_t *cache, const sky_str_t *name);

void sky_dns_cache_host_add(sky_dns_cache_t *cache, sky_dns_host_t *entry);

void sky_dns_cache_host_remove(sky_dns_cache_t *cache, sky_dns_host_t *host);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_DNS_CACHE_H
