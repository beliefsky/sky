//
// Created by edz on 2021/1/20.
//

#include "udp.h"
#include "../core/log.h"
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

typedef struct {
    sky_event_t ev;
    sky_int32_t timeout;
} listener_t;

static sky_bool_t udp_listener_connect(sky_event_t *ev);

static void udp_listener_error(sky_event_t *ev);


void
sky_udp_listener_create(sky_event_loop_t *loop, sky_pool_t *pool, const sky_udp_conf_t *conf) {
    sky_int32_t fd;
    sky_int32_t opt;
    listener_t *l;
    struct addrinfo *addrs;

    const struct addrinfo hints = {
            .ai_family = AF_UNSPEC,
            .ai_socktype = SOCK_DGRAM,
            .ai_flags = AI_PASSIVE
    };
    if (sky_unlikely(getaddrinfo((sky_char_t *) conf->host.data, (sky_char_t *) conf->port.data,
                                 &hints, &addrs) == -1)) {
        return;
    }
    for (struct addrinfo *addr = addrs; addr; addr = addr->ai_next) {
        fd = socket(addr->ai_family,
                    addr->ai_socktype | SOCK_NONBLOCK | SOCK_CLOEXEC,
                    addr->ai_protocol);

        if (sky_unlikely(fd == -1)) {
            continue;
        }
        opt = 1;
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(sky_int32_t));
#ifdef SO_REUSEPORT_LB
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT_LB, &opt, sizeof(sky_int32_t));
#else
        setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &opt, sizeof(sky_int32_t));
#endif
        if (sky_likely(bind(fd, addr->ai_addr, addr->ai_addrlen) != 0)) {
            close(fd);
            continue;
        }

        l = sky_palloc(pool, sizeof(listener_t));
        l->timeout = conf->timeout;
        sky_event_init(loop, &l->ev, fd, udp_listener_connect, udp_listener_error);
        sky_event_register(&l->ev, -1);
    }
    freeaddrinfo(addrs);
}

static sky_bool_t
udp_listener_connect(sky_event_t *ev) {
    sky_uchar_t buf[1024];

    struct sockaddr_in addr;


    recvfrom(ev->fd, buf, sizeof(buf), 0, (struct sockaddr *) &addr, (socklen_t *) sizeof(addr));

    sky_log_info("data: %s", buf);
    sky_log_info("addr: %d:%d", addr.sin_addr.s_addr, addr.sin_port);

    return true;
}

static void
udp_listener_error(sky_event_t *ev) {
    sky_log_info("%d: udp listener error", ev->fd);
}