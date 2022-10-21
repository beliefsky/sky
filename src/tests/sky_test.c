//
// Created by weijing on 18-2-8.
//
#include <core/types.h>
#include <core/log.h>
#include <core/process.h>
#include <event/event_loop.h>


static void test(void *data) {
    sky_event_loop_t *ev_loop = sky_event_loop_create();
    sky_event_loop_run(ev_loop);
    sky_event_loop_destroy(ev_loop);
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);


    sky_i32_t pid = sky_process_fork();
    if (pid > 0) {
        sky_process_bind_cpu(0);
        test(null);
    } else if (pid == 0) {
        sky_process_bind_cpu(1);
        test(null);
    }



    return 0;
}
