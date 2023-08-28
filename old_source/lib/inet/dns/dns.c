//
// Created by beliefsky on 2023/4/25.
//

#include "dns.h"
#include "../udp.h"
#include "../../core/log.h"
#include "../../core/memory.h"
#include "dns_protocol.h"
#include "dns_cache.h"

#define DNS_BUF_SIZE 512

struct sky_dns_s {
    sky_udp_t udp;
    sky_udp_ctx_t ctx;
    sky_inet_addr_t address;
    sky_inet_addr_t remote;
    sky_dns_cache_t dns_cache;
    sky_pool_t *pool;
    sky_uchar_t buf[DNS_BUF_SIZE];
};

static void dns_read_process(sky_udp_t *udp);


sky_dns_t *
sky_dns_create(sky_event_loop_t *loop, const sky_dns_conf_t *conf) {
    sky_dns_t *dns = sky_malloc(sizeof(sky_dns_t) + sky_inet_addr_size(conf->addr));
    sky_udp_ctx_init(&dns->ctx);
    sky_udp_init(&dns->udp, &dns->ctx, sky_event_selector(loop));
    sky_inet_addr_set_ptr(&dns->address, (void *) (dns + 1));
    sky_inet_addr_copy(&dns->address, conf->addr);
    sky_inet_addr_set(&dns->remote, null, 0);
    sky_dns_cache_init(&dns->dns_cache);

    if (sky_unlikely(!sky_udp_open(&dns->udp, sky_inet_addr_family(&dns->address)))) {
        sky_log_error("create udp fail");
        sky_free(dns);
        return null;
    }
    dns->pool = sky_pool_create(4906);

    sky_udp_set_cb(&dns->udp, dns_read_process);
    dns_read_process(&dns->udp);

    return dns;
}

void
sky_dns_get_ipv4_by_name(sky_dns_t *dns, const sky_str_t *name) {
    sky_dns_host_t *entry = sky_dns_cache_host_get(&dns->dns_cache, name);
    if (entry) {
        return;
    }
    entry = sky_malloc(sizeof(sky_dns_host_t));
    sky_queue_init(&entry->ipv4_query);


    // 先查询，有结果，直接返回

    // 查询dns是否在查询中，如果是
}

void
sky_dns_destroy(sky_dns_t *dns) {
    if (sky_unlikely(!dns)) {
        return;
    }
    sky_udp_close(&dns->udp);
    sky_pool_destroy(dns->pool);
    sky_free(dns);
}

void test(sky_dns_t *dns) {
    sky_uchar_t domain[] = {
            3, 'w', 'w', 'w',
            8, 'b', 'i', 'l', 'i', 'b', 'i', 'l', 'i',
            3, 'c', 'o', 'm',
            0
    };

    sky_dns_question_t question[] = {
            {
                    .name = domain,
                    .name_len = sizeof(domain),
                    .type = SKY_DNS_TYPE_A,
                    .clazz = SKY_DNS_CLAZZ_IN
            }
    };

    sky_dns_packet_t packet = {
            .header = {
                    .id = 99,
                    .flags = 1 << 8,
                    .qd_count = 1
            },
            .questions = question
    };

    const sky_i32_t n = sky_dns_encode(&packet, dns->buf, DNS_BUF_SIZE);
    if (n < 0) {
        return;
    }

    for (sky_i32_t i = 0; i < n; ++i) {
        printf("%d\t", dns->buf[i]);
    }
    printf("\n");


    if (sky_unlikely(!sky_udp_write(&dns->udp, &dns->address, dns->buf, (sky_u32_t) n))) {
        sky_log_error("write fail");
    }
}


static void
dns_read_process(sky_udp_t *udp) {

    sky_dns_t *dns = sky_type_convert(udp, sky_dns_t, udp);

    for (;;) {
        const sky_isize_t n = sky_udp_read(udp, &dns->remote, dns->buf, DNS_BUF_SIZE);

        sky_log_warn("========== read size: %ld =============", n);
        if (n > 0) {
            sky_dns_packet_t packet;
            if (sky_unlikely(!sky_dns_decode_header(&packet, dns->buf, (sky_u32_t) n))) {
                sky_log_warn("dns decode error");
                continue;
            }
            if (!sky_dns_flags_qr(packet.header.flags)) {
                sky_log_warn("这是请求类型, 暂时不支持");
                continue;
            }

            if (sky_unlikely(!sky_dns_decode_body(&packet, dns->pool, dns->buf, (sky_u32_t) n))) {
                sky_log_warn("dns decode error");
                sky_pool_reset(dns->pool);
                continue;
            }

            sky_log_error("==============================");
            for (sky_u32_t i = 0; i < packet.header.an_count; ++i) {
                sky_dns_answer_t *an = packet.answers + i;

                if (an->type == SKY_DNS_TYPE_A) {
                    sky_log_info("%d %d -> (%d)%s", an->type, an->clazz, an->name[0], an->name);
                    sky_uchar_t *a = (sky_uchar_t *) &an->resource.ipv4;
                    sky_log_warn("%d.%d.%d.%d -> %d", a[0], a[1], a[2], a[3], an->ttl);
                }
            }

            sky_pool_reset(dns->pool);

            continue;
        }
        if (sky_likely(!n)) {
            sky_udp_try_register(udp);
            break;
        }

        sky_log_error("read error");

        sky_udp_close(udp);
        break;
    }
}


