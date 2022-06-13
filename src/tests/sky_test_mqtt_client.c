//
// Created by edz on 2022/6/13.
//
#include <net/mqtt/mqtt_client.h>
#include <stdio.h>
#include <netinet/in.h>
#include <core/log.h>

static sky_event_loop_t *ev_loop;
static sky_mqtt_client_t *mqtt_client;

static void
timer_cb(sky_timer_wheel_entry_t *timer) {
    sky_mqtt_topic_t topic = {
            .topic = sky_string("func/#"),
            .qos = 0,
    };

    sky_bool_t result = sky_mqtt_client_sub(mqtt_client, &topic, 1);
    if (!result) {
        sky_event_timer_register(ev_loop, timer, 5);
    }
}

static void
mqtt_msg_cb(sky_mqtt_client_t *client, sky_mqtt_head_t *head, sky_mqtt_publish_msg_t *msg) {
    msg->payload.data[msg->payload.len] = '\0';
    sky_log_info("%s", msg->payload.data);
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    ev_loop = sky_event_loop_create();

    sky_uchar_t ip[] = {192, 168, 0, 15};
    struct sockaddr_in v4_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) ip,
            .sin_port = sky_htons(1883)
    };
    const sky_mqtt_client_conf_t mqtt_conf = {
            .client_id = sky_string("test_mqtt_client_11222"),
            .address_len = sizeof(v4_address),
            .address = (sky_inet_address_t *) &v4_address,
            .msg_handle = mqtt_msg_cb
    };

    mqtt_client = sky_mqtt_client_create(ev_loop, &mqtt_conf);

    sky_timer_wheel_entry_t timer;
    sky_timer_entry_init(&timer, timer_cb);
    timer_cb(&timer);

    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);

    return 0;
}
