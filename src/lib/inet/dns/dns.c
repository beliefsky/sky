//
// Created by beliefsky on 2023/4/25.
//

#include "dns.h"
#include "../udp.h"
#include "../../core/log.h"
#include "../../core/memory.h"
#include "dns_protocol.h"

struct sky_dns_s {
    sky_udp_t udp;
    sky_udp_ctx_t ctx;
    sky_inet_addr_t address;
    sky_inet_addr_t remote;
    sky_event_loop_t *ev_loop;
};

static void dns_read_process(sky_udp_t *udp);


sky_dns_t *
sky_dns_create(sky_event_loop_t *loop, const sky_dns_conf_t *conf) {
    sky_dns_t *dns = sky_malloc(sizeof(sky_dns_t) + sky_inet_addr_size(conf->addr));
    sky_udp_ctx_init(&dns->ctx);
    sky_udp_init(&dns->udp, &dns->ctx, sky_event_selector(loop));
    sky_inet_addr_set_ptr(&dns->address, (void *)(dns + 1));
    sky_inet_addr_copy(&dns->address, conf->addr);
    sky_inet_addr_set(&dns->remote, null, 0);
    dns->ev_loop = loop;

    if (sky_unlikely(!sky_udp_open(&dns->udp, sky_inet_addr_family(&dns->address)))) {
        sky_log_error("create udp fail");
        sky_free(dns);
        return null;
    }
    sky_udp_set_cb(&dns->udp, dns_read_process);
    dns_read_process(&dns->udp);

    return dns;
}

void
sky_dns_destroy(sky_dns_t *dns) {
    if (sky_unlikely(!dns)) {
        return;
    }
    sky_udp_close(&dns->udp);
    sky_free(dns);
}

void test(sky_dns_t *dns) {
    sky_uchar_t tmp[512];

    sky_uchar_t domain[] = "\3www\5baidu\3com";

    sky_dns_question_t question = {
            .name = domain,
            .name_len = sizeof(domain) -1,
            .type = SKY_DNS_TYPE_A,
            .clazz = SKY_DNS_CLAZZ_IN
    };

    sky_dns_packet_t packet = {
            .header = {
                    .id = 0xF7FB,
                    .flags = 1 << 8,
                    .qd_count = 1
            },
            .questions = &question
    };

    const sky_u32_t n = sky_dns_encode(&packet, tmp);

    for (sky_usize_t i = 0; i < n; ++i) {
        printf("%x  ", tmp[i]);
    }
    printf("\n");


    if (sky_unlikely(!sky_udp_write(&dns->udp, &dns->address, tmp, n))) {
        sky_log_error("write fail");
    }
}

static void
dns_read_process(sky_udp_t *udp) {

    sky_dns_t *dns = sky_type_convert(udp, sky_dns_t, udp);

    sky_uchar_t data[512];


    for (;;) {
        const sky_isize_t n = sky_udp_read(udp, &dns->remote, data, 1520);

        if (n > 0) {
            data[n] = '\0';
            sky_log_info("read: %ld -> %s", n, data);

            for (sky_isize_t i = 0; i < n; ++i) {
                printf("%c  ", data[i]);
            }
            printf("\n");

            for (sky_isize_t i = 0; i < n; ++i) {
                printf("%x  ", data[i]);
            }
            printf("\n");

            continue;
        }
        if (sky_likely(!n)) {
            sky_log_warn("========== wait =============");
            sky_udp_try_register(udp);
            break;
        }

        sky_log_error("read error");

        sky_udp_close(udp);
        sky_free(udp);

        break;
    }
}


