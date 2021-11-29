//
// Created by weijing on 18-11-6.
//

#ifndef SKY_EVENT_LOOP_H
#define SKY_EVENT_LOOP_H

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
    sky_u32_t status;
};

struct sky_event_loop_s {
    sky_timer_wheel_t *ctx;
    sky_time_t now;
    sky_i32_t fd;
    sky_i32_t max_events;
    sky_bool_t update: 1;
};

#define sky_event_is_reg(_ev)    (((_ev)->status & 0x00000001) != 0)
#define sky_event_is_read(_ev)   (((_ev)->status & 0x00000002) != 0)
#define sky_event_is_write(_ev)  (((_ev)->status & 0x00000004) != 0)

#define sky_event_none_reg(_ev)    (((_ev)->status & 0x00000001) == 0)
#define sky_event_none_read(_ev)   (((_ev)->status & 0x00000002) == 0)
#define sky_event_none_write(_ev)  (((_ev)->status & 0x00000004) == 0)

#define sky_event_clean_read(_ev)   (_ev)->status &= 0xFFFFFFFD
#define sky_event_clean_write(_ev)  (_ev)->status &= 0xFFFFFFFB

#define sky_event_has_callback(_ev) (sky_event_is_reg(_ev) || sky_timer_is_link(&(_ev)->timer))
#define sky_event_none_callback(_ev) (sky_timer_none_link(&(_ev)->timer))

#define sky_event_init(_loop, _ev, _fd, _run, _close) \
    do {                                              \
        sky_timer_entry_init(&(_ev)->timer, null);    \
        (_ev)->loop = (_loop);                        \
        (_ev)->now = (_loop)->now;                    \
        (_ev)->run = (sky_event_run_pt)(_run);        \
        (_ev)->close = (sky_event_close_pt)(_close);  \
        (_ev)->fd = (_fd);                            \
        (_ev)->timeout = 0;                           \
        (_ev)->status = 0x0000FFFE;                   \
    } while(0)

#define sky_event_rebind(_ev, _fd)     \
    do {                               \
        (_ev)->fd = (_fd);             \
        (_ev)->status |= 0x0000FFFE;   \
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
void sky_event_loop_shutdown(sky_event_loop_t *loop);

/**
 * 加入监听需要触发
 * @param loop 事件触发服务
 * @param ev 加入的事件
 * @param timeout 设定超时时间(秒)， -1永久
 */
sky_bool_t sky_event_register(sky_event_t *ev, sky_i32_t timeout);

/**
 * 移除监听触发，该函数会马上关闭io，并在稍后会触发关闭事件
 * @param ev 已经加入的事件
 */
sky_bool_t sky_event_unregister(sky_event_t *ev);


void sky_event_reset_timeout_self(sky_event_t *ev, sky_i32_t timeout);

void sky_event_reset_timeout(sky_event_t *ev, sky_i32_t timeout);

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
