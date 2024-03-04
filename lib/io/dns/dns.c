//
// Created by weijing on 2024/2/5.
//
#include <io/dns/dns.h>
#include <io/udp.h>
#include <core/memory.h>
#include <core/log.h>
#include "./dns_protocol.h"

#define DNS_BUF_SIZE 512

struct sky_dns_s {
    sky_udp_t udp;
    sky_inet_address_t address;
    sky_inet_address_t remote;
    sky_pool_t *pool;
    sky_uchar_t buf[DNS_BUF_SIZE];
};

static void dns_read_process(sky_udp_t *udp);

sky_dns_t *
sky_dns_create(sky_event_loop_t *const loop, const sky_dns_cli_conf_t *const conf) {
    sky_dns_t *const dns = sky_malloc(sizeof(sky_dns_t));
    sky_udp_init(&dns->udp, sky_event_selector(loop));
    dns->address = *conf->address;

    if (sky_unlikely(!sky_udp_open(&dns->udp, sky_inet_address_family(&dns->address)))) {
        sky_free(dns);
        return null;
    }
    dns->pool = sky_pool_create(4096);

    sky_udp_set_cb(&dns->udp, dns_read_process);
    dns_read_process(&dns->udp);

    return dns;
}

void
sky_dns_get_ipv4_by_name(sky_dns_t *cli, const sky_str_t *name) {
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

    const sky_i32_t n = sky_dns_encode(&packet, cli->buf, DNS_BUF_SIZE);
    if (n < 0) {
        return;
    }
    if (sky_unlikely(!sky_udp_write(&cli->udp, &cli->address, cli->buf, (sky_u32_t) n))) {
        sky_log_error("dns send packet fail");
    }
}

void
sky_dns_cli_destroy(sky_dns_t *cli) {

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
            sky_log_warn("============question ==================");
            for (sky_u32_t i = 0; i < packet.header.qd_count; ++i) {
                sky_dns_question_t *qd = packet.questions + i;

                if (qd->type == SKY_DNS_TYPE_A) {
                    sky_log_info("%d %d -> (%d)%s", qd->type, qd->clazz, qd->name[0], qd->name);
                }
            }

            sky_log_warn("============answer ==================");
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
            return;
        }

        sky_log_error("read error");

        sky_udp_close(udp);
        return;
    }
}

