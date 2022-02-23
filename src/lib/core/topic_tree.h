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

sky_topic_tree_t *sky_topic_tree_create();

void sky_topic_tree_sub(sky_topic_tree_t *tree, const sky_str_t *topic, void *client);

void sky_topic_tree_unsub(sky_topic_tree_t *tree, const sky_str_t *topic, void *client);

void *sky_topic_tree_scan(sky_topic_tree_t *tree, const sky_str_t *topic);

void sky_topic_tree_destroy(sky_topic_tree_t *tree);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_TOPIC_TREE_H
