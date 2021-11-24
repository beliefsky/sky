//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>
#include <event/event_loop.h>
#include <platform.h>
#include <core/log.h>
#include <net/tcp_listener.h>

static void server_start(sky_u32_t index);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    const sky_platform_conf_t conf = {
            .thread_size = 2,
            .run = server_start
    };

    sky_platform_t *platform = sky_platform_create(&conf);
    sky_platform_wait(platform);
    sky_platform_destroy(platform);

    return 0;
}


static sky_bool_t
mqtt_listener(sky_tcp_r_t *reader, void *data) {
    (void) data;

    sky_uchar_t buff[512];


    sky_log_info("start read");
    sky_tcp_listener_read(reader, data, 512);

    sky_log_info("read: %s", buff);

    return true;
}

static void
server_start(sky_u32_t index) {
    sky_log_info("thread-%u", index);

    sky_event_loop_t *loop = sky_event_loop_create();


    sky_uchar_t mq_ip[] = {192, 168, 0, 15};
    struct sockaddr_in mqtt_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) mq_ip,
            .sin_port = sky_htons(1883)
    };
    const sky_tcp_listener_conf_t listener_conf = {
            .listener = mqtt_listener,
            .address = (sky_inet_address_t *) &mqtt_address,
            .address_len = sizeof(struct sockaddr_in)
    };

    sky_tcp_listener_create(loop, &listener_conf);

    sky_event_loop_run(loop);
    sky_event_loop_shutdown(loop);
}
