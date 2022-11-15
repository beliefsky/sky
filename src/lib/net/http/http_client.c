//
// Created by beliefsky on 2022/11/15.
//

#include "http_client.h"
#include "../tcp_client.h"
#include "../../core/memory.h"

struct sky_http_client_s {
    sky_tcp_client_t *client;
    sky_coro_t *coro;
    sky_defer_t *defer;
};

static void http_client_defer(sky_http_client_t *client);

sky_http_client_t *
sky_http_client_create(sky_event_t *event, sky_coro_t *coro) {
    sky_http_client_t *client = sky_malloc(sizeof(sky_http_client_t));

    const sky_tcp_client_conf_t conf = {
            .keep_alive = 60,
            .nodelay = true,
            .timeout = 5
    };

    client->client = sky_tcp_client_create(event, coro, &conf);
    client->coro = coro;
    client->defer = sky_defer_add(coro, (sky_defer_func_t) http_client_defer, client);

    return client;
}


void
sky_http_client_destroy(sky_http_client_t *client) {
    sky_defer_cancel(client->coro, client->defer);
    http_client_defer(client);
}

static void
http_client_defer(sky_http_client_t *client) {
    sky_tcp_client_destroy(client->client);
    client->client = null;
    sky_free(client);
}
