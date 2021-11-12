//
// Created by weijing on 18-2-8.
//
#include <netinet/in.h>
#include <event/event_loop.h>
#include <core/memory.h>
#include <core/thread.h>
#include <core/log.h>
#include <net/tcp_listener.h>

static void *server_start(void *args);

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    const sky_i32_t size = 1;

    sky_thread_t *thread = sky_malloc(sizeof(sky_thread_t) * (sky_usize_t) size);
    for (sky_i32_t i = 0; i < size; ++i) {
        sky_thread_attr_t attr;

        sky_thread_attr_init(&attr);
        sky_thread_attr_set_scope(&attr, SKY_THREAD_SCOPE_SYSTEM);
//        sky_thread_attr_set_stack_size(&attr, 4096);

        sky_thread_create(&thread[i], &attr, server_start, null);

        sky_thread_attr_destroy(&attr);

//        sky_thread_set_cpu(thread[i], 7);
    }
    for (sky_i32_t i = 0; i < size; ++i) {
        sky_thread_join(thread[i], null);
    }
    sky_free(thread);

    return 0;
}


static sky_bool_t
mqtt_listener(sky_tcp_r_t *reader, void *data) {
    (void) data;

    sky_uchar_t buff[512];


    sky_log_info("start read");
    sky_tcp_listener_read(reader, data, 512);

    sky_log_info("read: %s", buff);

    return true;
}

static void *
server_start(void *args) {
    (void) args;

    sky_pool_t *pool;
    sky_event_loop_t *loop;

    pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);

    loop = sky_event_loop_create(pool);


    sky_uchar_t mq_ip[] = {192, 168, 0, 15};
    struct sockaddr_in mqtt_address = {
            .sin_family = AF_INET,
            .sin_addr.s_addr = *(sky_u32_t *) mq_ip,
            .sin_port = sky_htons(1883)
    };
    const sky_tcp_listener_conf_t listener_conf = {
            .listener = mqtt_listener,
            .address = (sky_inet_address_t *) &mqtt_address,
            .address_len = sizeof(struct sockaddr_in)
    };

    sky_tcp_listener_create(loop, &listener_conf);

    sky_event_loop_run(loop);
    sky_event_loop_shutdown(loop);

    return null;
}
