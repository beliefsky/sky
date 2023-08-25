//
// Created by weijing on 2023/8/25.
//
#include "mqtt_client_common.h"
#include <core/memory.h>

static void mqtt_read_head(sky_tcp_t *tcp);

static void mqtt_read_body(sky_tcp_t *tcp);

void
mqtt_client_read_packet(sky_mqtt_client_t *const client, sky_mqtt_status_pt call) {
    client->next_cb = call;
    sky_tcp_set_cb(&client->tcp, mqtt_read_head);
    mqtt_read_head(&client->tcp);
}

static void
mqtt_read_head(sky_tcp_t *const tcp) {
    sky_mqtt_client_t *const client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);

    sky_u32_t read_size = client->head_copy;
    sky_uchar_t *buf = client->head_tmp + read_size;

    const sky_isize_t n = sky_tcp_read(&client->tcp, buf, 8 - read_size);
    if (n > 0) {
        read_size += (sky_u32_t) n;
        const sky_i8_t flag = mqtt_head_pack(&client->mqtt_head_tmp, client->head_tmp, read_size);
        if (sky_likely(flag > 0)) {
            if (read_size > (sky_u32_t) flag) {
                read_size -= (sky_u32_t) flag;
                client->head_copy = read_size & 0x7;

                sky_u64_t tmp = sky_htonll(*(sky_u64_t *) client->head_tmp);
                tmp <<= flag << 3;
                *((sky_u64_t *) client->head_tmp) = sky_htonll(tmp);
            } else {
                client->head_copy = 0;
            }
            const sky_u32_t body_size = client->mqtt_head_tmp.body_size;
            // body的读取
            if (!body_size) {
                client->next_cb(client);
                return;
            }
            client->body_read_n = 0;
            client->body_tmp = sky_pnalloc(client->reader_pool, body_size);
            if (client->head_copy) {
                buf = client->body_tmp;
                if (sky_unlikely(body_size <= client->head_copy)) {
                    sky_memcpy(buf, client->head_tmp, body_size);
                    client->head_copy -= body_size & 0x7;

                    sky_u64_t tmp = sky_htonll(*(sky_u64_t *) client->head_tmp);
                    tmp <<= body_size << 3;
                    *((sky_u64_t *) client->head_tmp) = sky_htonll(tmp);

                    client->next_cb(client);
                    return;
                }

                if (body_size >= 8) {
                    sky_memcpy8(buf, client->head_tmp);
                } else {
                    sky_memcpy(buf, client->head_tmp, client->head_copy);
                }
                client->body_read_n = client->head_copy;
                client->head_copy = 0;
            }

            sky_tcp_set_cb(tcp, mqtt_read_body);
            mqtt_read_body(tcp);
            return;
        }
        if (sky_unlikely(flag == -1 || read_size >= 8)) {
            // 包读取异常
            goto error;
        }
        client->head_copy = read_size & 0x7;

        if (sky_unlikely(!mqtt_client_write_packet(client))) {
            goto error;
        }

        return;
    }
    if (sky_likely(!n)) {
        if (sky_unlikely(!mqtt_client_write_packet(client))) {
            goto error;
        }
        sky_tcp_try_register(tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    error:
    mqtt_client_close(client);
}

static void
mqtt_read_body(sky_tcp_t *tcp) {
    sky_mqtt_client_t *client = sky_type_convert(tcp, sky_mqtt_client_t, tcp);
    mqtt_head_t *head = &client->mqtt_head_tmp;
    sky_uchar_t *buf = client->body_tmp + client->body_read_n;

    const sky_isize_t n = sky_tcp_read(&client->tcp, buf, head->body_size - client->body_read_n);
    if (n > 0) {
        client->body_read_n += (sky_u32_t) n;
        if (client->body_read_n < head->body_size) {
            if (sky_unlikely(!mqtt_client_write_packet(client))) {
                goto error;
            }
            return;
        }
        client->next_cb(client);
        return;
    }

    if (sky_likely(!n)) {
        if (sky_unlikely(!mqtt_client_write_packet(client))) {
            goto error;
        }
        sky_tcp_try_register(&client->tcp, SKY_EV_READ | SKY_EV_WRITE);
        return;
    }

    error:
    mqtt_client_close(client);
}
