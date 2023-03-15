//
// Created by weijing on 18-11-6.
//

#ifndef SKY_EVENT_LOOP_H
#define SKY_EVENT_LOOP_H

#include "../core/timer_wheel.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_EV_REG      SKY_U32(0x00000001)
#define SKY_EV_READ     SKY_U32(0x00000002)
#define SKY_EV_WRITE    SKY_U32(0x00000004)
#define SKY_EV_ERROR    SKY_U32(0x00000008)

typedef struct sky_event_loop_s sky_event_loop_t;
typedef struct sky_event_s sky_event_t;

typedef sky_bool_t (*sky_event_run_pt)(sky_event_t *ev);

typedef void (*sky_event_error_pt)(sky_event_t *ev);

struct sky_event_s {
    sky_timer_wheel_entry_t timer;
    sky_event_loop_t *loop; //该事件监听的主程
    sky_event_run_pt run; // 普通事件触发的回调函数
    sky_i32_t fd; //事件句柄
    sky_i32_t timeout; // 节点超时时间
    /*
     * |--- 2byte ---|--- ... ---|--- 1bit  ---|--- 1bit  ---|--- 1bit ---|--- 1bit ---|
     * |--- index ---|--- 预留 ---|--- error ---|--- write ---|--- read ---|--- reg  ---|
     */
    sky_u32_t status;
};

struct sky_event_loop_s {
    sky_timer_wheel_t *ctx;
    sky_time_t now;
    sky_i32_t fd;
    sky_i32_t max_events;
    sky_bool_t update: 1;
};

#define sky_event_none_reg(_ev) !sky_event_is_reg(_ev)
#define sky_event_none_read(_ev) !sky_event_is_read(_ev)
#define sky_event_none_write(_ev) !sky_event_is_write(_ev)

/**
 * 创建io事件触发服务
 * @param pool 创建时所需的内存池
 * @return 触发列队服务
 */
sky_event_loop_t *sky_event_loop_create();

/**
 * 执行事件触发服务，该服务线程阻塞
 * @param loop 事件触发服务
 */
void sky_event_loop_run(sky_event_loop_t *loop);

/**
 * 关闭事件触发服务
 * @param loop 事件触发服务
 */
void sky_event_loop_destroy(sky_event_loop_t *loop);

/**
 * 加入事件触发
 * @param loop 事件触发服务
 * @param ev 加入的事件
 * @param timeout 设定超时时间(秒)， -1永久
 */
sky_bool_t sky_event_register(sky_event_t *ev, sky_i32_t timeout);

/**
 * 加入事件触发
 * @param loop 事件触发服务
 * @param ev 加入的事件
 * @param timeout 设定超时时间(秒)， -1永久
 */
sky_bool_t sky_event_register_none(sky_event_t *ev, sky_i32_t timeout);

/**
 * 加入事件触发
 * @param loop 事件触发服务
 * @param ev 加入的事件
 * @param timeout 设定超时时间(秒)， -1永久
 */
sky_bool_t sky_event_register_only_read(sky_event_t *ev, sky_i32_t timeout);

/**
 * 加入事件触发
 * @param loop 事件触发服务
 * @param ev 加入的事件
 * @param timeout 设定超时时间(秒)， -1永久
 */
sky_bool_t sky_event_register_only_write(sky_event_t *ev, sky_i32_t timeout);

/**
 * 移除监听触发，该函数会马上关闭io，并在稍后会触发关闭事件
 * @param ev 已经加入的事件
 */
sky_bool_t sky_event_unregister(sky_event_t *ev);


/**
 * 标记为事件异常状态，会主动触发异常回调
 * @param ev 事件
 */
void sky_event_set_error(sky_event_t *ev);

/**
 * 重置当前事件超时时间
 * @param ev  事件
 * @param timeout 超时时间
 */
void sky_event_reset_timeout_self(sky_event_t *ev, sky_i32_t timeout);

/**
 * 重置指定事件超时时间
 * @param ev 事件
 * @param timeout 超时
 */
void sky_event_reset_timeout(sky_event_t *ev, sky_i32_t timeout);

/**
 * 初始化事件
 * @param ev 事件
 * @param loop 事件触发服务
 * @param fd io句柄
 * @param run_handle 正常执行函数
 * @param error_handle 异常执行函数
 */
static sky_inline void
sky_event_init(
        sky_event_t *ev,
        sky_event_loop_t *loop,
        sky_i32_t fd,
        sky_event_run_pt run_handle,
        sky_event_error_pt error_handle
) {
    sky_timer_entry_init(&ev->timer, (sky_timer_wheel_pt) error_handle);
    ev->loop = loop;
    ev->run = run_handle;
    ev->fd = fd;
    ev->timeout = 0;
    ev->status = SKY_EV_READ | SKY_EV_WRITE;
}

/**
 * 重新绑定io句柄, 只适用于未注册事件
 * @param ev 事件
 * @param fd io句柄
 */
static sky_inline void
sky_event_rebind(sky_event_t *ev, sky_i32_t fd) {
    ev->fd = fd;
    ev->status = SKY_EV_READ | SKY_EV_WRITE;
}

/**
 * 重新设置回调
 * @param ev 事件
 * @param run_handle 正常执行函数
 * @param error_handle 异常执行函数
 */
static sky_inline void
sky_event_reset(sky_event_t *ev, sky_event_run_pt run_handle, sky_event_error_pt error_handle) {
    ev->run = run_handle;
    ev->timer.cb = (sky_timer_wheel_pt) error_handle;
}

/**
 * 获取时间
 * @param loop 事件服务
 * @return 时间
 */
static sky_inline sky_i64_t
sky_event_loop_now(const sky_event_loop_t *loop) {
    return loop->now;
}

/**
 * 是否注册事件
 * @param ev 事件
 * @return 结果
 */
static sky_inline sky_bool_t
sky_event_is_reg(const sky_event_t *ev) {
    return !!(ev->status & SKY_EV_REG);
}

/**
 * 是否异常
 * @param ev 事件
 * @return 结果
 */
static sky_inline sky_bool_t
sky_event_is_error(const sky_event_t *ev) {
    return !!(ev->status & SKY_EV_ERROR);
}

/**
 * 是否可读
 * @param ev 事件
 * @return 结果
 */
static sky_inline sky_bool_t
sky_event_is_read(const sky_event_t *ev) {
    return !!(ev->status & SKY_EV_READ);
}

/**
 * 是否可写
 * @param ev 事件
 * @return 结果
 */
static sky_inline sky_bool_t
sky_event_is_write(const sky_event_t *ev) {
    return !!(ev->status & SKY_EV_WRITE);
}

/**
 * 标记不可读状态
 * @param ev 事件
 */
static sky_inline void
sky_event_clean_read(sky_event_t *ev) {
    ev->status &= ~SKY_EV_READ;
}

/**
 * 标记不可写状态
 * @param ev 事件
 */
static sky_inline void
sky_event_clean_write(sky_event_t *ev) {
    ev->status &= ~SKY_EV_WRITE;
}

/**
 * 获取事件io句柄
 * @param ev 事件
 * @return io句柄
 */
static sky_inline sky_i32_t
sky_event_get_fd(const sky_event_t *ev) {
    return ev->fd;
}

/**
 * 获取事件服务
 * @param ev 事件
 * @return 事件服务
 */
static sky_inline sky_event_loop_t *
sky_event_get_loop(sky_event_t *ev) {
    return ev->loop;
}

/**
 * 获取时间
 * @param ev 事件
 * @return 时间
 */
static sky_inline sky_i64_t
sky_event_get_now(const sky_event_t *ev) {
    return sky_event_loop_now(ev->loop);
}

/**
 * 刷新时间超时时间
 * @param ev 事件
 */
static sky_inline void
sky_event_timeout_expired(sky_event_t *event) {
    sky_timer_wheel_expired(event->loop->ctx, &event->timer, (sky_u64_t) (event->loop->now + event->timeout));
}

/**
 * 事件是否能回调
 * @param ev 事件
 * @return 结果
 */
static sky_inline sky_bool_t
sky_event_has_callback(sky_event_t *ev) {
    return sky_event_is_reg(ev) || sky_timer_linked(&ev->timer);
}

/**
 * 事件是否不能回调了
 * @param ev 事件
 * @return 结果
 */
static sky_inline sky_bool_t
sky_event_none_callback(sky_event_t *ev) {
    return sky_event_none_reg(ev) && !sky_timer_linked(&ev->timer);
}

/**
 * 向事件中主动加入定时器，自定义处理
 * @param loop 事件触发服务
 * @param timer 定时器
 * @param timeout 超时时间，单位秒
 */
static sky_inline void
sky_event_timer_register(sky_event_loop_t *loop, sky_timer_wheel_entry_t *timer, sky_u32_t timeout) {
    loop->update |= (timeout == 0);
    sky_timer_wheel_link(loop->ctx, timer, (sky_u64_t) (loop->now + timeout));
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_EVENT_LOOP_H
