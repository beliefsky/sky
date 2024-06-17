//
// Created by weijing on 2024/6/14.
//

#ifndef SKY_PGSQL_SCRAM_H
#define SKY_PGSQL_SCRAM_H

#include <core/string.h>
#include <core/palloc.h>

typedef struct {
    sky_str_t username;
    sky_str_t password;
    sky_pool_t *pool;
    sky_str_t first_client_msg;
    sky_str_t slat;
    sky_str_t client_server_nonce;
    sky_usize_t client_nonce_offset;


    sky_u32_t rounds;
} pgsql_scram_t;


const sky_uchar_t *
pgsql_scram_first_client_msg(
        pgsql_scram_t *scram,
        const sky_uchar_t *data,
        sky_usize_t data_size,
        sky_usize_t *out_size
);

sky_uchar_t *
pgsql_scram_sha256_first_server_msg(
        pgsql_scram_t *scram,
        const sky_uchar_t *data,
        sky_usize_t size,
        sky_usize_t *out_size
);


static sky_inline void
pgsql_scram_init(
        pgsql_scram_t *scram,
        sky_str_t *username,
        sky_str_t *password,
        sky_pool_t *pool
) {
    scram->username = *username;
    scram->password = *password;
    scram->pool = pool;
}

#endif //SKY_PGSQL_SCRAM_H
