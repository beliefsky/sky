//
// Created by weijing on 18-11-6.
//

#ifndef SKY_EVENT_LOOP_H
#define SKY_EVENT_LOOP_H

#include "../core/palloc.h"
#include "../core/timer_wheel.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct sky_event_loop_s sky_event_loop_t;
typedef struct sky_event_s sky_event_t;

typedef sky_bool_t (*sky_event_run_pt)(sky_event_t *ev);

typedef void (*sky_event_close_pt)(sky_event_t *ev);

struct sky_event_s {
    sky_timer_wheel_entry_t timer;
    sky_event_loop_t *loop; //该事件监听的主程
    sky_event_run_pt run; // 普通事件触发的回调函数
    sky_event_close_pt close; // 关闭事件回调函数
    sky_time_t now; // 当前时间
    sky_i32_t fd; //事件句柄
    sky_i32_t timeout; // 节点超时时间
    sky_i16_t index; // 用于内部多时间合并寻址的相关功能
    sky_bool_t reg: 1; // 该事件监听是否注册，用于防止非法提交
    sky_bool_t read: 1; // 目前io可读
    sky_bool_t write: 1; // 目前io可写
};

struct sky_event_loop_s {
    sky_timer_wheel_t *ctx;
    sky_pool_t *pool;
    sky_time_t now;
    sky_i32_t fd;
    sky_i32_t conn_max;
    sky_bool_t update: 1;
};

#define sky_event_init(_loop, _ev, _fd, _run, _close) \
    do {                                              \
        sky_timer_entry_init(&(_ev)->timer, null);    \
        (_ev)->loop = (_loop);                        \
        (_ev)->now = (_loop)->now;                    \
        (_ev)->run = (sky_event_run_pt)(_run);        \
        (_ev)->close = (sky_event_close_pt)(_close);  \
        (_ev)->fd = (_fd);                            \
        (_ev)->timeout = 0;                           \
        (_ev)->reg = false;                           \
        (_ev)->read = true;                           \
        (_ev)->write = true;                          \
    } while(0)

#define sky_event_rebind(_ev, _fd) \
    do {                           \
        (_ev)->fd = (_fd);         \
        (_ev)->read = true;        \
        (_ev)->write = true;       \
    } while(0)


#define sky_event_reset(_ev, _run, _close) \
    do {                                   \
        (_ev)->run = (sky_event_run_pt)(_run); \
        (_ev)->close = (sky_event_close_pt)(_close); \
    } while(0)

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
void sky_event_register(sky_event_t *ev, sky_i32_t timeout);

/**
 * 移除监听触发，该函数会马上关闭io，并在稍后会触发关闭事件
 * @param ev 已经加入的事件
 */
void sky_event_unregister(sky_event_t *ev);


/**
 * 向事件中主动加入定时器，自定义处理
 * @param loop 事件触发服务
 * @param timer 定时器
 * @param timeout 超时时间，单位秒
 */
static sky_inline void
sky_event_timer_register(sky_event_loop_t *loop, sky_timer_wheel_entry_t *timer, sky_u32_t timeout) {
    if (!timeout) {
        loop->update = true;
    }
    sky_timer_wheel_link(loop->ctx, timer, (sky_u64_t) (loop->now + timeout));
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_EVENT_LOOP_H
