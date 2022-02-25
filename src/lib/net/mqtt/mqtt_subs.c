//
// Created by edz on 2022/2/24.
//

#include "mqtt_subs.h"

static sky_bool_t mqtt_sub(void **client, void *user_data);

static void mqtt_unsub(void **client, void *user_data);

static void mqtt_node_destroy(void *client);

static void mqtt_publish(void *client, void *user_data);


typedef struct {
    sky_str_t *topic;
    sky_mqtt_session_t *session;
    sky_u8_t qos;
} mqtt_subs_topic_t;

typedef struct {
    const sky_mqtt_head_t *head;
    const sky_mqtt_publish_msg_t *msg;
} mqtt_subs_publish_t;

sky_topic_tree_t *
sky_mqtt_subs_create() {
    return sky_topic_tree_create(mqtt_sub, mqtt_unsub, mqtt_node_destroy);
}

void
sky_mqtt_subs_sub(sky_topic_tree_t *subs, sky_str_t *topic, sky_mqtt_session_t *session, sky_u8_t qos) {
    mqtt_subs_topic_t sub = {
            .topic = topic,
            .session = session,
            .qos = qos
    };

    sky_topic_tree_sub(subs, topic, &sub);
}

void
sky_mqtt_subs_unsub(sky_topic_tree_t *subs, sky_str_t *topic, sky_mqtt_session_t *session) {
    mqtt_subs_topic_t unsub = {
            .topic = topic,
            .session = session
    };

    sky_topic_tree_unsub(subs, topic, &unsub);
}

void
sky_mqtt_subs_publish(sky_topic_tree_t *subs, const sky_mqtt_head_t *head, const sky_mqtt_publish_msg_t *msg) {
    mqtt_subs_publish_t publish = {
            .head = head,
            .msg = msg
    };

    sky_topic_tree_scan(subs, &msg->topic, mqtt_publish, &publish);
}

void
sky_mqtt_subs_destroy(sky_topic_tree_t *subs) {

}


static sky_bool_t
mqtt_sub(void **client, void *user_data) {
    const sky_mqtt_session_t *session = user_data;

    return false;
}

static void
mqtt_unsub(void **client, void *user_data) {
    const sky_mqtt_session_t *session = user_data;
}

static void
mqtt_node_destroy(void *client) {
}

static void
mqtt_publish(void *client, void *user_data) {
    mqtt_subs_publish_t *publish = user_data;
}
