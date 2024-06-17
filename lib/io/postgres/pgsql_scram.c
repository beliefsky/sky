//
// Created by weijing on 2024/6/14.
//
#include "./pgsql_scram.h"
#include <core/memory.h>
#include <core/string.h>
#include <core/number.h>
#include <crypto/base64.h>
#include <crypto/pbkdf2.h>
#include <crypto/hmac_sha256.h>

#include <core/log.h>

#define FIRST_CLIENT_MSG_BARE_OFFSET    3


static sky_bool_t first_server_msg_parse(
        pgsql_scram_t *scram,
        const sky_uchar_t *data,
        sky_usize_t size
);

const sky_uchar_t *
pgsql_scram_first_client_msg(
        pgsql_scram_t *scram,
        const sky_uchar_t *data,
        sky_usize_t data_size,
        sky_usize_t *out_size
) {
    const sky_usize_t size = scram->username.len + (data_size << 1) + 8;
    sky_uchar_t *const start = sky_palloc(scram->pool, size);
    sky_uchar_t *ptr = start;
    sky_memcpy4(ptr, "n,,n");
    ptr += 4;
    *(ptr++) = '=';
    sky_memcpy(ptr, scram->username.data, scram->username.len);
    ptr += scram->username.len;
    sky_memcpy2(ptr, ",r");
    ptr += 2;
    *(ptr++) = '=';

    for (sky_usize_t i = 0; i < data_size; ++i) { // 生成 33~126除去44(,)的值均可
        *(ptr++) = (sky_uchar_t) ((data[i] & 63) + 45);
        *(ptr++) = (sky_uchar_t) ((data[i] >> 6) + 45);
    }

    scram->first_client_msg.data = start;
    scram->first_client_msg.len = size;
    scram->client_nonce_offset = scram->username.len + 8;

    *out_size = size;

    return start;
}

sky_uchar_t *
pgsql_scram_sha256_first_server_msg(
        pgsql_scram_t *scram,
        const sky_uchar_t *data,
        sky_usize_t size,
        sky_usize_t *out_size
) {
    if (!first_server_msg_parse(scram, data, size)) {
        return null;
    }

    sky_uchar_t slat_password[32];
    sky_pbkdf2_hmac_sha256(
            scram->password.data,
            scram->password.len,
            scram->slat.data,
            scram->slat.len,
            slat_password,
            32,
            scram->rounds
    );

    sky_uchar_t *const start = sky_palloc(scram->pool, 76 + scram->client_server_nonce.len);
    sky_uchar_t *ptr = start;
    sky_memcpy8(ptr, "c=biws,r");
    ptr += 8;
    *(ptr++) = '=';
    sky_memcpy(ptr, scram->client_server_nonce.data, scram->client_server_nonce.len);
    ptr += scram->client_server_nonce.len;
    sky_memcpy4(ptr, ",p=\0");
    ptr += 3;


    sky_uchar_t client_key[SKY_HMAC_SHA256_DIGEST_SIZE];

    sky_hmac_sha256(
            slat_password,
            32,
            sky_str_line("Client Key"),
            client_key
    );

    sky_uchar_t stored_key[SKY_SHA256_DIGEST_SIZE];
    sky_sha256(client_key, SKY_SHA256_DIGEST_SIZE, stored_key); // stored key

    sky_hmac_sha256_t ctx;
    sky_hmac_sha256_init(&ctx, stored_key, SKY_SHA256_DIGEST_SIZE);
    sky_hmac_sha256_update(
            &ctx,
            scram->first_client_msg.data + FIRST_CLIENT_MSG_BARE_OFFSET,
            scram->first_client_msg.len - FIRST_CLIENT_MSG_BARE_OFFSET
    );
    sky_hmac_sha256_update(&ctx, sky_str_line(","));
    sky_hmac_sha256_update(&ctx, data, size);
    sky_hmac_sha256_update(&ctx, sky_str_line(","));
    sky_hmac_sha256_update(&ctx, start, 9 + scram->client_server_nonce.len);

    sky_uchar_t client_sign[SKY_HMAC_SHA256_DIGEST_SIZE];
    sky_hmac_sha256_final(&ctx, client_sign);

    for (sky_u32_t i = 0; i < SKY_HMAC_SHA256_DIGEST_SIZE; ++i) {
        client_sign[i] ^= client_key[i];
    }
    const sky_usize_t proof_size = sky_base64_encode(ptr, client_sign, SKY_HMAC_SHA256_DIGEST_SIZE);

    *out_size = 12 + scram->client_server_nonce.len + proof_size;

    return start;
}

static sky_bool_t
first_server_msg_parse(
        pgsql_scram_t *scram,
        const sky_uchar_t *data,
        sky_usize_t size
) {
    sky_u32_t n = 0;
    sky_isize_t index;
    sky_usize_t tmp;

    for (; size && size != SKY_USIZE_MAX;) {
        index = sky_str_len_index_char(data, size, ',');
        if (index == -1) {
            index = (sky_isize_t) size;
        }
        if (index < 2) {
            return null;
        }

        switch (sky_str2_switch(data)) {
            case sky_str2_num('r', '='): {
                data += 2;
                size -= 2;
                index -= 2;

                tmp = scram->first_client_msg.len - scram->client_nonce_offset;

                if (!sky_str_len_starts_with(
                        data,
                        (sky_usize_t) index,
                        scram->first_client_msg.data + scram->client_nonce_offset,
                        tmp
                )) {
                    return false;
                }
                scram->client_server_nonce.data = (sky_uchar_t *) data;
                scram->client_server_nonce.len = (sky_usize_t) index;

                break;
            }
            case sky_str2_num('s', '='): {
                data += 2;
                size -= 2;
                index -= 2;

                tmp = (sky_usize_t) index;
                scram->slat.data = sky_palloc(scram->pool, sky_base64_decoded_length(tmp));
                scram->slat.len = sky_base64_decode(scram->slat.data, data, tmp);
                if (!sky_base64_decode_success(scram->slat.len)) {
                    sky_pfree(scram->pool, scram->slat.data, tmp);
                    return false;
                }
                break;
            }
            case sky_str2_num('i', '='): {
                data += 2;
                size -= 2;
                index -= 2;

                if (!sky_str_len_to_u32(data, (sky_usize_t) index, &scram->rounds)) {
                    return false;
                }
                break;
            }
            default:
                return false;
        }
        ++index;
        data += index;
        size -= (sky_usize_t) index;
        ++n;
    }
    return n == 3;
}
