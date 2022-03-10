//
// Created by edz on 2022/2/24.
//

#include "mqtt_subs.h"
#include "../../core/memory.h"
#include "../../core/log.h"
#include "../../core/crc32.h"
#include "../../safe/atomic.h"
#include "mqtt_response.h"

typedef struct {
    sky_event_msg_t msg;
    sky_mqtt_server_t *server;
    sky_str_t topic;

    union {
        sky_u32_t topic_index;
        sky_str_t payload;
    };
    sky_atomic_u32_t ref;

    sky_bool_t dup: 1;
    sky_u8_t qos: 2;
    sky_bool_t retain: 1;

} mqtt_share_msg_t;

static sky_bool_t mqtt_sub(void **client, void *user_data);

static void mqtt_unsub(void **client, void *user_data);

static void mqtt_node_destroy(void *client);

static void mqtt_publish(void *client, void *user_data);

static sky_u64_t topic_hash(const void *item, void *secret);

static sky_bool_t topic_equals(const void *a, const void *b);

static void topic_destroy(void *item);

static void mqtt_thread_node_sub(sky_event_msg_t *msg);

static void mqtt_thread_node_unsub(sky_event_msg_t *msg);

static void mqtt_thread_node_publish(sky_event_msg_t *msg);

static void share_message_free(mqtt_share_msg_t *msg);


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
    sky_mqtt_thread_node_t *node = server->thread_node;
    for (sky_u32_t i = 0; i < server->thread_node_n; ++i, ++node) {
        node->sub_tree = sky_topic_tree_create(mqtt_sub, mqtt_unsub, mqtt_node_destroy);
        node->topic_tree = sky_malloc(sizeof(sky_topic_tree_t *) * server->thread_node_n);
        for (sky_u32_t j = 0; j < server->thread_node_n; ++j) {
            if (i == j) {
                node->topic_tree[j] = null;
            } else {
                node->topic_tree[j] = sky_topic_tree_create(null, null, null);
            }
        }
    }
}

sky_bool_t
sky_mqtt_subs_sub(sky_mqtt_server_t *server, sky_str_t *topic, sky_mqtt_session_t *session, sky_u8_t qos) {
    const sky_u32_t idx = sky_event_manager_thread_idx();
    sky_mqtt_thread_node_t *node = server->thread_node + idx;

    subs_topic_tmp_t sub = {
            .topic = topic,
            .session = session,
            .qos = qos
    };

    if (sky_topic_tree_sub(node->sub_tree, topic, &sub)) {
        if (server->thread_node_n > 1) {
            mqtt_share_msg_t *share_msg = sky_malloc(
                    sizeof(mqtt_share_msg_t)
                    + topic->len
            );
            share_msg->msg.handle = mqtt_thread_node_sub;
            share_msg->server = server;
            share_msg->ref = SKY_ATOMIC_VAR_INIT(server->thread_node_n - 1);
            share_msg->topic_index = idx;
            share_msg->topic.data = (sky_uchar_t *) (share_msg + 1);
            share_msg->topic.len = topic->len;
            sky_memcpy(share_msg->topic.data, topic->data, topic->len);

            for (sky_u32_t i = 0; i < server->thread_node_n; ++i) {
                if (i != idx) {
                    if (!sky_event_manager_idx_msg(server->manager, &share_msg->msg, i)) {
                        share_message_free(share_msg);
                    }
                }
            }
        }
        return true;
    }

    return false;
}

sky_bool_t
sky_mqtt_subs_unsub(sky_mqtt_server_t *server, sky_str_t *topic, sky_mqtt_session_t *session) {
    const sky_u32_t idx = sky_event_manager_thread_idx();
    sky_mqtt_thread_node_t *node = server->thread_node + idx;

    subs_topic_tmp_t unsub = {
            .topic = topic,
            .session = session
    };
    if (sky_topic_tree_unsub(node->sub_tree, topic, &unsub)) {
        if (server->thread_node_n > 1) {
            mqtt_share_msg_t *share_msg = sky_malloc(
                    sizeof(mqtt_share_msg_t)
                    + topic->len
            );
            share_msg->msg.handle = mqtt_thread_node_unsub;
            share_msg->server = server;
            share_msg->ref = SKY_ATOMIC_VAR_INIT(server->thread_node_n - 1);
            share_msg->topic_index = idx;
            share_msg->topic.data = (sky_uchar_t *) (share_msg + 1);
            share_msg->topic.len = topic->len;
            sky_memcpy(share_msg->topic.data, topic->data, topic->len);

            for (sky_u32_t i = 0; i < server->thread_node_n; ++i) {
                if (i != idx) {
                    if (!sky_event_manager_idx_msg(server->manager, &share_msg->msg, i)) {
                        share_message_free(share_msg);
                    }
                }
            }
        }
        return true;
    }

    return false;
}

void
sky_mqtt_subs_publish(sky_mqtt_server_t *server, const sky_mqtt_head_t *head, const sky_mqtt_publish_msg_t *msg) {
    const sky_u32_t idx = sky_event_manager_thread_idx();
    sky_mqtt_thread_node_t *node = server->thread_node + idx;

    subs_publish_tmp_t publish = {
            .head = head,
            .msg = msg
    };

    sky_topic_tree_scan(node->sub_tree, &msg->topic, mqtt_publish, &publish);
    if (server->thread_node_n < 2) {
        return;
    }
    mqtt_share_msg_t *share_msg = sky_malloc(
            sizeof(mqtt_share_msg_t)
            + msg->topic.len
            + msg->payload.len
    );
    share_msg->msg.handle = mqtt_thread_node_publish;
    share_msg->server = server;
    share_msg->dup = head->dup;
    share_msg->qos = head->qos;
    share_msg->retain = head->retain;
    share_msg->ref = SKY_ATOMIC_VAR_INIT(server->thread_node_n - 1);
    share_msg->topic_index = idx;
    share_msg->topic.data = (sky_uchar_t *) (share_msg + 1);
    share_msg->topic.len = msg->topic.len;
    share_msg->payload.data = share_msg->topic.data + share_msg->topic.len;
    share_msg->payload.len = msg->payload.len;
    sky_memcpy(share_msg->topic.data, msg->topic.data, msg->topic.len);
    sky_memcpy(share_msg->payload.data, msg->payload.data, msg->payload.len);


    for (sky_u32_t i = 0; i < server->thread_node_n; ++i) {
        if (i != idx) {
            if (!sky_topic_tree_filter(node->topic_tree[i], &msg->topic)
                || !sky_event_manager_idx_msg(server->manager, &share_msg->msg, i)) {
                share_message_free(share_msg);
            }
        }
    }
}

void
sky_mqtt_subs_destroy(sky_mqtt_server_t *server) {
//    sky_topic_tree_destroy(server->sub_tree);
//    server->sub_tree = null;
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

static void
mqtt_thread_node_sub(sky_event_msg_t *msg) {
    mqtt_share_msg_t *share_msg = (mqtt_share_msg_t *) msg;
    sky_mqtt_thread_node_t *node = share_msg->server->thread_node + sky_event_manager_thread_idx();
    sky_topic_tree_t *sub_tree = node->topic_tree[share_msg->topic_index];
    sky_topic_tree_sub(sub_tree, &share_msg->topic, null);

    share_message_free(share_msg);
}

static void
mqtt_thread_node_unsub(sky_event_msg_t *msg) {
    mqtt_share_msg_t *share_msg = (mqtt_share_msg_t *) msg;
    sky_mqtt_thread_node_t *node = share_msg->server->thread_node + sky_event_manager_thread_idx();

    sky_topic_tree_t *sub_tree = node->topic_tree[share_msg->topic_index];
    sky_topic_tree_unsub(sub_tree, &share_msg->topic, null);

    share_message_free(share_msg);
}

static void
mqtt_thread_node_publish(sky_event_msg_t *msg) {
    mqtt_share_msg_t *share_msg = (mqtt_share_msg_t *) msg;
    sky_mqtt_thread_node_t *node = share_msg->server->thread_node + sky_event_manager_thread_idx();

    const sky_mqtt_head_t mqtt_head = {
            .dup = share_msg->dup,
            .qos = share_msg->qos,
            .retain = share_msg->retain
    };
    const sky_mqtt_publish_msg_t mqtt_msg = {
            .topic = share_msg->topic,
            .payload = share_msg->payload
    };
    subs_publish_tmp_t publish = {
            .head = &mqtt_head,
            .msg = &mqtt_msg
    };
    sky_topic_tree_scan(node->sub_tree, &share_msg->topic, mqtt_publish, &publish);

    share_message_free(share_msg);
}


static sky_inline void
share_message_free(mqtt_share_msg_t *msg) {
    const sky_u32_t result = sky_atomic_get_sub_explicit(&msg->ref, SKY_U32(1), SKY_ATOMIC_RELAXED);
    if (result == 1) {
        sky_free(msg);
    }
}