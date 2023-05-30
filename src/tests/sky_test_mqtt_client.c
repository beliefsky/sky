//
// Created by edz on 2022/6/13.
//
#include <inet/mqtt/mqtt_client.h>
#include <stdio.h>
#include <netinet/in.h>
#include <core/log.h>

static void
mqtt_connected_cb(sky_mqtt_client_t *client) {
    sky_mqtt_topic_t topic = {
            .topic = sky_string("func/+/+"),
            .qos = 2,
    };

    sky_log_info("connected");

    sky_mqtt_client_sub(client, &topic, 1);
}

static void
mqtt_close_cb(sky_mqtt_client_t *client) {
    sky_log_info("closed");
}

static void
mqtt_msg_cb(sky_mqtt_client_t *client, sky_mqtt_head_t *head, sky_mqtt_publish_msg_t *msg) {
    sky_log_info("%s", msg->topic.data);
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *ev_loop = sky_event_loop_create();

    sky_uchar_t ip[] = {192, 168, 0, 15};
    struct sockaddr_in v4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) ip,
            .sin_port = sky_htons(1883)
    };
    sky_inet_addr_t address;
    sky_inet_addr_set(&address, &v4_address, sizeof(struct sockaddr_in));

    const sky_mqtt_client_conf_t mqtt_conf = {
            .client_id = sky_string("test_mqtt_client_11222"),
            .address = &address,
            .connected = mqtt_connected_cb,
            .msg_handle = mqtt_msg_cb,
            .closed = mqtt_close_cb
    };

    sky_mqtt_client_create(ev_loop, &mqtt_conf);

    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    return 0;
}
