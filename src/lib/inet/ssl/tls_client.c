//
// Created by beliefsky on 2023/4/22.
//

#include "tls_client.h"
#include "tls.h"

sky_bool_t
sky_tls_client_connect(sky_tcp_client_t *client) {
    sky_tls_init(&client->tcp);

    sky_event_timeout_set(client->loop, &client->timer, client->timeout);
    for (;;) {
        const sky_i8_t r = sky_tls_connect(&client->tcp);
        if (r > 0) {
            sky_timer_wheel_unlink(&client->timer);
            return true;
        }

        if (sky_likely(!r)) {
            sky_tcp_try_register(&client->tcp, SKY_EV_READ | SKY_EV_WRITE);
            sky_event_timeout_expired(client->loop, &client->timer, client->timeout);
            sky_coro_yield(SKY_CORO_MAY_RESUME);
            if (sky_unlikely(!sky_tcp_is_open(&client->tcp))) {
                return false;
            }
            continue;
        }

        sky_timer_wheel_unlink(&client->timer);
        sky_tcp_close(&client->tcp);

        return false;
    }
}
