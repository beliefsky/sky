//
// Created by edz on 2022/3/2.
//

#ifndef SKY_SHARE_MSG_H
#define SKY_SHARE_MSG_H

#include "../event/event_loop.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_share_msg_s sky_share_msg_t;
typedef struct sky_share_msg_connect_s sky_share_msg_connect_t;

typedef sky_bool_t (*sky_share_msg_iter_pt)(sky_share_msg_connect_t *conn, sky_u32_t index, void *user_data);

typedef void (*sky_share_msg_handle_pt)(void *data, void *u_data);


sky_share_msg_t *sky_share_msg_create(sky_u32_t num);

sky_bool_t sky_share_msg_bind(
        sky_share_msg_t *share_msg,
        sky_event_loop_t *loop,
        sky_share_msg_handle_pt handle,
        void *u_data,
        sky_u32_t index
);

void sky_share_msg_destroy(sky_share_msg_t *share_msg);

sky_u32_t sky_share_msg_num(sky_share_msg_t *share_msg);

sky_bool_t sky_share_msg_scan(sky_share_msg_t *share_msg, sky_share_msg_iter_pt iter, void *user_data);

sky_bool_t sky_share_msg_send_index(sky_share_msg_t *share_msg, sky_u32_t index, void *data);

sky_bool_t sky_share_msg_send(sky_share_msg_connect_t *conn, void *data);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_SHARE_MSG_H
