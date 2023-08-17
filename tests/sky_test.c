//
// Created by weijing on 18-2-8.
//
#include <io/event_loop.h>
#include <core/log.h>
#include <io/http/http_client_wait.h>


static void
test_sync(sky_sync_wait_t *wait, void *data) {
    sky_event_loop_t *const loop = data;

    sky_http_client_t *const client = sky_http_client_create(loop, null);

    sky_pool_t *const pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);

    sky_http_client_req_t *const req = sky_http_client_req_create(client, pool);
    sky_http_client_res_t *const res = sky_http_client_wait_req(req, wait);
    sky_str_t *const body = sky_http_client_res_body_wait_str(res, wait);

    if (body) {
        sky_log_info("body:(%lu)%s", body->len, body->data);
    } else {
        sky_log_error("body read error");
    }

    sky_pool_destroy(pool);
    sky_http_client_destroy(client);
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *const loop = sky_event_loop_create();


    sky_sync_wait_create_with_stack(test_sync, loop, 2048);


    sky_event_loop_run(loop);
    sky_event_loop_destroy(loop);

    return 0;
}

