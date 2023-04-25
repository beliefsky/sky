//
// Created by weijing on 18-2-8.
//
#include <event/event_loop.h>
#include <core/log.h>
#include <inet/udp.h>
#include <netinet/in.h>
#include <core/memory.h>
#include <core/string.h>
#include <inet/dns/dns.h>

static void
test_read(sky_udp_t *udp) {
    struct sockaddr_in tmp;
    sky_inet_addr_t remote;
    sky_inet_addr_set(&remote, &tmp, sizeof(tmp));

    sky_uchar_t data[1520];


    for (;;) {
        const sky_isize_t n = sky_udp_read(udp, &remote, data, 1520);

        if (n > 0) {
            data[n] = '\0';
            sky_log_info("read: %ld -> %s", n, data);

            for (sky_isize_t i = 0; i < n; ++i) {
                printf("%c", data[i]);
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

static void
test_write(sky_udp_t *udp) {
    sky_uchar_t ip[4] = {114,114, 114, 114};
    struct sockaddr_in remote_addr = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) ip,
            .sin_port = sky_htons(53)
    };

    sky_inet_addr_t remote;
    sky_inet_addr_set(&remote, &remote_addr, sizeof(remote_addr));

    sky_str_t data = sky_string("REGISTER sip:34020000002000000001@3402000000 SIP/2.0\r\n"
                                "Via: SIP/2.0/UDP 172.18.29.114:8053;rport=8053;branch=z9hG4bK1377739937\r\n"
                                "Max-Forwards: 70\r\n"
                                "Contact: <sip:34020000002000000719@192.168.10.8:60719>\r\n"
                                "To: <sip:34020000002000000719@3402000000>\r\n"
                                "From: <sip:34020000002000000719@3402000000>;tag=1534571172\r\n"
                                "Call-ID: 1566767517\r\n"
                                "CSeq: 1 REGISTER\r\n"
                                "Expires: 3600\r\n"
                                "User-Agent: IP Camera\r\n"
                                "Content-Length: 0\r\n\r\n");

    sky_uchar_t tmp[512];

    sky_dns_header_t header = {
            .id = sky_htons(0xF7FB),
            .flags = sky_htons(1 << 8),
            .qd_count = sky_htons(1)
    };

    sky_memcpy16(tmp, &header);

    sky_usize_t n = 12;
    sky_uchar_t *p = tmp + n;

    const sky_uchar_t domain[] = "\3www\5baidu\3com";

    sky_memcpy(p, domain, sizeof(domain));
    n += sizeof(domain);
    p += sizeof(domain);


    *(sky_u16_t *)p = sky_htons(1);
    p += 2;
    *(sky_u16_t *)p = sky_htons(1);
    n +=4;

    for (sky_usize_t i = 0; i < n; ++i) {
        printf("%x  ", tmp[i]);
    }
    printf("\n");


    if (sky_unlikely(!sky_udp_write(udp, &remote, tmp, n))) {
        sky_log_error("write fail");
    }
}


static void
create(sky_event_loop_t *loop, sky_udp_ctx_t *ctx) {


    sky_udp_t *udp = sky_malloc(sizeof(sky_udp_t));

    sky_udp_init(udp, ctx, sky_event_selector(loop));

    if (sky_unlikely(!sky_udp_open(udp, AF_INET))) {
        sky_log_error("create udp fail");
        sky_free(udp);
        return;
    }

    sky_udp_set_cb(udp, test_read);
    test_read(udp);

    test_write(udp);
}


int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *loop = sky_event_loop_create();

    sky_udp_ctx_t ctx;
    sky_udp_ctx_init(&ctx);
    create(loop, &ctx);

    sky_event_loop_run(loop);
    sky_event_loop_destroy(loop);

    return 0;
}
