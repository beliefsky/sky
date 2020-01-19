//
// Created by weijing on 18-11-6.
//

#ifndef SKY_EVENT_LOOP_H
#define SKY_EVENT_LOOP_H

#include "../core/palloc.h"
#include "../core/rbtree.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_event_loop_s sky_event_loop_t;
typedef struct sky_event_s sky_event_t;

typedef sky_bool_t (*sky_event_run_pt)(sky_event_t *ev);

typedef void (*sky_event_close_pt)(sky_event_t *ev);

struct sky_event_s {
    sky_rbtree_node_t node;  // 注册节点，用于超时时处理
    sky_event_loop_t *loop; //该事件监听的主程
    sky_event_run_pt run; // 普通事件触发的回调函数
    sky_event_close_pt close; // 异常事件或主动要求关闭时触发回调函数
    sky_time_t now; // 当前时间
    sky_time_t key; // 节点关键key
    sky_int32_t timeout; // 节点超时时间
    sky_int32_t fd; //时间句柄
    sky_int16_t index; // 用于内部多时间合并寻址的相关功能
    sky_bool_t reg:1; // 该事件监听是否注册，用于防止非法提交
    sky_bool_t wait:1; // 期间不会触发run函数
    sky_bool_t read:1; // 目前io可读
    sky_bool_t write:1; // 目前io可写
};

#define sky_event_init(_loop, _ev, _fd, _run, _close)   \
    (_ev)->fd = (_fd);                                  \
    (_ev)->loop = (_loop);                              \
    (_ev)->run = (sky_event_run_pt)(_run);              \
    (_ev)->close = (sky_event_close_pt)(_close);        \
    (_ev)->reg = false;                                 \
    (_ev)->wait = false;                                \
    (_ev)->read = true;                                 \
    (_ev)->write = true

/**
 * 创建io事件触发服务
 * @param pool 创建时所需的内存池
 * @return 触发列队服务
 */
sky_event_loop_t *sky_event_loop_create(sky_pool_t *pool);

/**
 * 执行事件触发服务，该服务线程阻塞
 * @param loop 事件触发服务
 */
void sky_event_loop_run(sky_event_loop_t *loop);

/**
 * 关闭事件触发服务
 * @param loop 事件触发服务
 */
void sky_event_loop_shutdown(sky_event_loop_t *loop);

/**
 * 加入监听需要触发
 * @param loop 事件触发服务
 * @param ev 加入的事件
 * @param timeout 设定超时时间(秒)， -1永久
 */
void sky_event_register(sky_event_t *ev, sky_int32_t timeout);

/**
 * 移除监听触发，该函数会马上关闭io，并在稍后会触发关闭事件
 * @param ev 已经加入的事件
 */
void sky_event_unregister(sky_event_t *ev);

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_EVENT_LOOP_H
