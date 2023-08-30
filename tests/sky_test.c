//
// Created by weijing on 18-2-8.
//
#include <io/event_loop.h>
#include <core/log.h>
#include <io/http/http_client_wait.h>
#include <core/memory.h>


static void
test_sync(sky_sync_wait_t *wait, void *data) {
    sky_http_client_t *const client = data;


    sky_pool_t *const pool = sky_pool_create(SKY_POOL_DEFAULT_SIZE);

    const sky_str_t url = sky_string("http://www.baidu.com");

    sky_http_client_req_t *const req = sky_http_client_req_create(pool, &url);

    sky_http_client_res_t *const res = sky_http_client_wait_req(client, req, wait);
    if (res) {
        sky_str_t *const body = sky_http_client_res_body_wait_str(res, wait);

        if (body) {
            sky_log_info("body:(%lu)%s", body->len, body->data);
        } else {
            sky_log_error("body read error");
        }
    } else {
        sky_log_error("client req error");
    }

    sky_pool_destroy(pool);
}

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_event_loop_t *const loop = sky_event_loop_create();

    sky_http_client_t *const client = sky_http_client_create(loop, null);


    sky_sync_wait_create_with_stack(test_sync, client, 1024 << 4);


    sky_event_loop_run(loop);
    sky_event_loop_destroy(loop);

    return 0;
}

