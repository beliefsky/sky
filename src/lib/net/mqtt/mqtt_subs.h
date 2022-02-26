//
// Created by edz on 2022/2/24.
//

#ifndef SKY_MQTT_SUBS_H
#define SKY_MQTT_SUBS_H

#include "../../core/topic_tree.h"
#include "mqtt_server.h"
#include "mqtt_protocol.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_topic_tree_t *sky_mqtt_subs_create();

void sky_mqtt_subs_sub(sky_topic_tree_t *subs, sky_str_t *topic, sky_mqtt_session_t *session, sky_u8_t qos);

void sky_mqtt_subs_unsub(sky_topic_tree_t *subs, sky_str_t *topic, sky_mqtt_session_t *session);

void sky_mqtt_subs_publish(sky_topic_tree_t *subs, const sky_mqtt_head_t *head, const sky_mqtt_publish_msg_t *msg);

void sky_mqtt_subs_destroy(sky_topic_tree_t *subs);

sky_hashmap_t *sky_mqtt_topics_create();

void sky_mqtt_topics_clean(sky_hashmap_t *topics);

void sky_mqtt_topics_destroy(sky_hashmap_t *topics);


#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_MQTT_SUBS_H
