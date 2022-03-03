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

typedef void (*sky_share_msg_handle_pt)(sky_u64_t msg, void *data);


sky_share_msg_t *sky_share_msg_create(sky_u32_t num);

sky_bool_t sky_share_msg_bind(
        sky_share_msg_t *share_msg,
        sky_event_loop_t *loop,
        sky_share_msg_handle_pt handle,
        void *data,
        sky_u32_t index
);

void sky_share_msg_destroy(sky_share_msg_t *share_msg);

sky_bool_t sky_share_msg_scan(sky_share_msg_t *share_msg, sky_share_msg_iter_pt iter, void *user_data);

sky_bool_t sky_share_msg_send_index(sky_share_msg_t *share_msg, sky_u32_t index, sky_u64_t msg);

sky_bool_t sky_share_msg_send(sky_share_msg_connect_t *conn, sky_u64_t msg);

// 1 -> n 发送, n 读取
// 1 -> 指定发送, 对应读取

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_SHARE_MSG_H
