//
// Created by edz on 2022/2/22.
//

#ifndef SKY_TOPIC_TREE_H
#define SKY_TOPIC_TREE_H

#include "string.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_topic_tree_s sky_topic_tree_t;

typedef sky_bool_t (*sky_topic_tree_sub_pt)(void **client, void *user_data);

typedef void (*sky_topic_tree_unsub_pt)(void **client, void *user_data);

typedef void (*sky_topic_tree_iter_pt)(void *client, void *user_data);

typedef void (*sky_topic_tree_destroy_pt)(void *client);


sky_topic_tree_t *sky_topic_tree_create(
        sky_topic_tree_sub_pt sub,
        sky_topic_tree_unsub_pt unsub,
        sky_topic_tree_destroy_pt destroy
);

sky_bool_t sky_topic_tree_sub(sky_topic_tree_t *tree, const sky_str_t *topic, void *user_data);

sky_bool_t sky_topic_tree_unsub(sky_topic_tree_t *tree, const sky_str_t *topic, void *user_data);

void sky_topic_tree_scan(sky_topic_tree_t *tree, const sky_str_t *topic,
                         sky_topic_tree_iter_pt iter, void *user_data);

sky_bool_t sky_topic_tree_scan_one(sky_topic_tree_t *tree, const sky_str_t *topic,
                             sky_topic_tree_iter_pt iter, void *user_data);

void sky_topic_tree_destroy(sky_topic_tree_t *tree);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TOPIC_TREE_H
