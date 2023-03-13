//
// Created by edz on 2022/6/13.
//
#include <net/mqtt/mqtt_client.h>
#include <stdio.h>
#include <netinet/in.h>
#include <core/log.h>
#include <core/memory.h>
#include <net/un_inet.h>

void test(sky_un_inet_t *un_inet, void *data) {
    sky_mqtt_client_t *client = data;

    sky_mqtt_client_writer_t writer;

    sky_str_t topic = sky_string("func/pro/a");
    sky_str_t payload = sky_string("hello world");
    for (int i = 0; i < 500000; ++i) {
        sky_mqtt_client_bind(client, &writer, sky_un_inet_event(un_inet), sky_un_inet_coro(un_inet));

        sky_mqtt_client_pub(&writer, &topic, &payload, 1, false, false);

        sky_mqtt_client_unbind(&writer);

    }
    sky_log_info("11111111111111111111");
}

static void
mqtt_connected_cb(sky_mqtt_client_t *client) {
    sky_event_t *event = sky_mqtt_client_event(client);
    sky_coro_t *coro = sky_mqtt_client_coro(client);
    sky_mqtt_topic_t topic = {
            .topic = sky_string("func/+/+"),
            .qos = 2,
    };

    sky_mqtt_client_writer_t writer;

    sky_mqtt_client_bind(client, &writer, sky_mqtt_client_event(client), sky_mqtt_client_coro(client));
    sky_mqtt_client_sub(&writer, &topic, 1);
    sky_mqtt_client_unbind(&writer);

    sky_coro_switcher_t *switcher = sky_coro_malloc(coro, (sky_u32_t)sky_coro_switcher_size());
    sky_un_inet_run(sky_event_get_loop(event), switcher, test, client);
}

static void
mqtt_msg_cb(sky_mqtt_client_t *client, sky_mqtt_head_t *head, sky_mqtt_publish_msg_t *msg) {
//    sky_log_info("msg");
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *ev_loop = sky_event_loop_create();

    sky_uchar_t ip[] = {0, 0, 0, 0};
    struct sockaddr_in v4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) ip,
            .sin_port = sky_htons(1883)
    };
    const sky_mqtt_client_conf_t mqtt_conf = {
            .client_id = sky_string("test_mqtt_client_11222"),
            .address_len = sizeof(v4_address),
            .address = (sky_inet_addr_t *) &v4_address,
            .connected = mqtt_connected_cb,
            .msg_handle = mqtt_msg_cb
    };

    sky_coro_switcher_t *switcher = sky_malloc(sky_coro_switcher_size());

    sky_mqtt_client_create(ev_loop, switcher, &mqtt_conf);

    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    sky_free(switcher);

    return 0;
}
