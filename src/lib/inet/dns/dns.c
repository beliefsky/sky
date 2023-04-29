//
// Created by beliefsky on 2023/4/25.
//

#include "dns.h"
#include "../udp.h"
#include "../../core/log.h"
#include "../../core/memory.h"
#include "../../core/palloc.h"
#include "dns_protocol.h"

#define DNS_BUF_SIZE 512

struct sky_dns_s {
    sky_udp_t udp;
    sky_udp_ctx_t ctx;
    sky_inet_addr_t address;
    sky_inet_addr_t remote;
    sky_pool_t *pool;
    sky_event_loop_t *ev_loop;
    sky_uchar_t read_buf[DNS_BUF_SIZE];
    sky_uchar_t write_buf[DNS_BUF_SIZE];
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
    dns->ev_loop = loop;

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
                    .name_len = sizeof(domain) - 1,
                    .type = SKY_DNS_TYPE_A,
                    .clazz = SKY_DNS_CLAZZ_IN
            },
            {
                    .name = domain,
                    .name_len = sizeof(domain) - 1,
                    .type = SKY_DNS_TYPE_AAAA,
                    .clazz = SKY_DNS_CLAZZ_IN
            },
    };

    sky_dns_packet_t packet = {
            .header = {
                    .id = 99,
                    .flags = 1 << 8,
                    .qd_count = 1
            },
            .questions = question
    };

    const sky_i32_t n = sky_dns_encode(&packet, dns->write_buf, DNS_BUF_SIZE);
    if (n < 0) {
        return;
    }

    for (sky_i32_t i = 0; i < n; ++i) {
        printf("%d\t", dns->write_buf[i]);
    }
    printf("\n");


    if (sky_unlikely(!sky_udp_write(&dns->udp, &dns->address, dns->write_buf, (sky_u32_t) n))) {
        sky_log_error("write fail");
    }
}

static void
dns_read_process(sky_udp_t *udp) {

    sky_dns_t *dns = sky_type_convert(udp, sky_dns_t, udp);

    for (;;) {
        const sky_isize_t n = sky_udp_read(udp, &dns->remote, dns->read_buf, DNS_BUF_SIZE);

        sky_log_warn("========== read size: %ld =============", n);
        if (n > 0) {
            sky_dns_packet_t packet;
            if (sky_unlikely(!sky_dns_decode_header(&packet, dns->read_buf, (sky_u32_t) n))) {
                sky_log_warn("dns decode error");
                continue;
            }
            if (!sky_dns_flags_qr(packet.header.flags)) {
                sky_log_info("这是请求类型, 暂时不支持");
                continue;
            }
            sky_log_info(
                    "header -> id: %d, flags: %d, qd: %d, an: %d, ns: %d, ar: %d",
                    packet.header.id,
                    packet.header.flags,
                    packet.header.qd_count,
                    packet.header.an_count,
                    packet.header.ns_count,
                    packet.header.ar_count
            );

            sky_log_info(
                    "header.flags -> QR: %d OPCODE: %d AA: %d TC: %d RD: %d RA: %d RCODE: %d",
                    sky_dns_flags_qr(packet.header.flags),
                    sky_dns_flags_op_code(packet.header.flags),
                    sky_dns_flags_aa(packet.header.flags),
                    sky_dns_flags_tc(packet.header.flags),
                    sky_dns_flags_rd(packet.header.flags),
                    sky_dns_flags_ra(packet.header.flags),
                    sky_dns_flags_r_code(packet.header.flags)
            );

            if (packet.header.qd_count > 0) {
                packet.questions = sky_pnalloc(dns->pool, sizeof(sky_dns_answer_t) * packet.header.qd_count);
            }
            if (packet.header.an_count > 0) {
                packet.answers = sky_pnalloc(dns->pool, sizeof(sky_dns_answer_t) * packet.header.an_count);
            }
            if (packet.header.ns_count > 0) {
                packet.authorities = sky_pnalloc(dns->pool, sizeof(sky_dns_answer_t) * packet.header.ns_count);
            }
            if (packet.header.ar_count > 0) {
                packet.authorities = sky_pnalloc(dns->pool, sizeof(sky_dns_answer_t) * packet.header.ar_count);
            }

            if (sky_unlikely(!sky_dns_decode_body(&packet, dns->read_buf, (sky_u32_t) n))) {
                sky_log_warn("dns decode error");
                sky_pool_reset(dns->pool);
                continue;
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


