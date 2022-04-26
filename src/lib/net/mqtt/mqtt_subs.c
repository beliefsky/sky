//
// Created by edz on 2022/2/24.
//

#include "mqtt_subs.h"
#include "../../core/memory.h"
#include "../../core/log.h"
#include "../../core/crc32.h"
#include "mqtt_response.h"

static sky_bool_t mqtt_sub(void **client, void *user_data);

static void mqtt_unsub(void **client, void *user_data);

static void mqtt_node_destroy(void *client);

static void mqtt_publish(void *client, void *user_data);

static sky_u64_t topic_hash(const void *item, void *secret);

static sky_bool_t topic_equals(const void *a, const void *b);

static void topic_destroy(void *item);


typedef struct {
    sky_str_t *topic;
    sky_mqtt_session_t *session;
    sky_u8_t qos;
} subs_topic_tmp_t;

typedef struct {
    const sky_mqtt_head_t *head;
    const sky_mqtt_publish_msg_t *msg;
} subs_publish_tmp_t;

typedef struct {
    sky_queue_t link;
    sky_str_t topic;
    sky_mqtt_session_t *session;
    sky_u8_t qos: 2;
} mqtt_subs_node_t;


void
sky_mqtt_subs_init(sky_mqtt_server_t *server) {
    server->sub_tree = sky_topic_tree_create(mqtt_sub, mqtt_unsub, mqtt_node_destroy);
}

sky_bool_t
sky_mqtt_subs_sub(sky_mqtt_server_t *server, sky_str_t *topic, sky_mqtt_session_t *session, sky_u8_t qos) {
    subs_topic_tmp_t sub = {
            .topic = topic,
            .session = session,
            .qos = qos
    };

    return sky_topic_tree_sub(server->sub_tree, topic, &sub);
}

sky_bool_t
sky_mqtt_subs_unsub(sky_mqtt_server_t *server, sky_str_t *topic, sky_mqtt_session_t *session) {
    subs_topic_tmp_t unsub = {
            .topic = topic,
            .session = session
    };

    return sky_topic_tree_unsub(server->sub_tree, topic, &unsub);
}

void
sky_mqtt_subs_publish(sky_mqtt_server_t *server, const sky_mqtt_head_t *head, const sky_mqtt_publish_msg_t *msg) {
    subs_publish_tmp_t publish = {
            .head = head,
            .msg = msg
    };

    sky_topic_tree_scan(server->sub_tree, &msg->topic, mqtt_publish, &publish);
}

void
sky_mqtt_subs_destroy(sky_mqtt_server_t *server) {
    sky_topic_tree_destroy(server->sub_tree);
    server->sub_tree = null;
}

sky_bool_t
sky_mqtt_topics_init(sky_hashmap_t *topics) {
    return sky_hashmap_init(topics, topic_hash, topic_equals, null);
}

void
sky_mqtt_topics_clean(sky_hashmap_t *topics) {
    sky_hashmap_clean(topics, topic_destroy, false);
}

void
sky_mqtt_topics_destroy(sky_hashmap_t *topics) {
    sky_hashmap_destroy(topics, topic_destroy);
}


static sky_bool_t
mqtt_sub(void **client, void *user_data) {
    const subs_topic_tmp_t *tmp = user_data;

    const mqtt_subs_node_t search = {
            .topic = *tmp->topic
    };

    const sky_u64_t hash = sky_hashmap_get_hash(&tmp->session->topics, &search);
    mqtt_subs_node_t *node = sky_hashmap_get_with_hash(&tmp->session->topics, hash, &search);
    if (null != node) {
        node->qos = tmp->qos;
        return false;
    }

    sky_queue_t *queue = *client;
    if (null == queue) {
        queue = sky_malloc(sizeof(sky_queue_t));
        sky_queue_init(queue);
        *client = queue;
    }

    node = sky_malloc(sizeof(mqtt_subs_node_t) + tmp->topic->len);
    node->topic.data = (sky_uchar_t *) (node + 1);
    node->topic.len = tmp->topic->len;
    sky_memcpy(node->topic.data, tmp->topic->data, tmp->topic->len);
    node->session = tmp->session;
    node->qos = tmp->qos;

    sky_hashmap_put_with_hash(&tmp->session->topics, hash, node);

    sky_queue_insert_prev(queue, &node->link);

    return true;
}

static void
mqtt_unsub(void **client, void *user_data) {
    const subs_topic_tmp_t *tmp = user_data;

    if (null == tmp->session) {
        return;
    }
    const mqtt_subs_node_t search = {
            .topic = *tmp->topic
    };

    mqtt_subs_node_t *node = sky_hashmap_del(&tmp->session->topics, &search);
    if (null != node) {
        sky_queue_remove(&node->link);
        sky_free(node);
    }
}

static void
mqtt_node_destroy(void *client) {
    sky_queue_t *queue = client;

    if (sky_unlikely(!sky_queue_is_empty(queue))) {
        sky_log_error("sub queue is not empty");
        sky_queue_t *n = sky_queue_next(queue);
        mqtt_subs_node_t *node = sky_queue_data(n, mqtt_subs_node_t, link);
        sky_log_warn("%s", node->topic.data);
    }

    sky_free(queue);
}

static void
mqtt_publish(void *client, void *user_data) {
    sky_queue_t *queue = client;
    subs_publish_tmp_t *publish = user_data;
    if (sky_unlikely(sky_queue_is_empty(queue))) {
        sky_log_error("sub queue is empty");
        return;
    }
    sky_queue_t *node = sky_queue_next(queue);
    mqtt_subs_node_t *item;
    while (queue != node) {
        item = sky_queue_data(node, mqtt_subs_node_t, link);

        sky_mqtt_publish_msg_t msg = {
                .topic = publish->msg->topic,
                .payload = publish->msg->payload
        };
        sky_u8_t qos = sky_min(publish->head->qos, item->qos);
        if (qos > 0) {
            if (0 == (++item->session->packet_identifier)) {
                msg.packet_identifier = ++item->session->packet_identifier;
            } else {
                msg.packet_identifier = item->session->packet_identifier;
            }
        }

        sky_mqtt_send_publish(
                item->session->conn,
                &msg,
                qos,
                0,
                0
        );

        node = sky_queue_next(node);
    }

}

static sky_u64_t
topic_hash(const void *item, void *secret) {
    (void) secret;
    const mqtt_subs_node_t *topic = item;

    sky_u32_t crc = sky_crc32_init();
    crc = sky_crc32c_update(crc, topic->topic.data, topic->topic.len);

    return sky_crc32_final(crc);
}

static sky_bool_t
topic_equals(const void *a, const void *b) {
    const mqtt_subs_node_t *ua = a;
    const mqtt_subs_node_t *ub = b;

    return sky_str_equals(&ua->topic, &ub->topic);
}

static void
topic_destroy(void *item) {
    mqtt_subs_node_t *node = item;

    sky_queue_remove(&node->link);

    sky_mqtt_subs_unsub(node->session->server, &node->topic, null);

    sky_free(node);
}