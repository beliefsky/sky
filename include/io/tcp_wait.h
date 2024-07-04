//
// Created by weijing on 2024/7/4.
//

#ifndef SKY_TCP_WAIT_H
#define SKY_TCP_WAIT_H

#include "./tcp.h"
#include "./sync_wait.h"

#if defined(__cplusplus)
extern "C" {
#endif

sky_bool_t sky_tcp_wait_accept(sky_tcp_ser_t *ser, sky_tcp_cli_t *cli, sky_sync_wait_t *wait);

sky_bool_t sky_tcp_ser_wait_close(sky_tcp_ser_t *ser, sky_sync_wait_t *wait);

sky_bool_t sky_tcp_wait_connect(
        sky_tcp_cli_t *cli,
        const sky_inet_address_t *address,
        sky_sync_wait_t *wait
);

sky_usize_t sky_tcp_wait_skip(sky_tcp_cli_t *cli, sky_usize_t size, sky_sync_wait_t *wait);

sky_usize_t sky_tcp_wait_read(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_sync_wait_t *wait
);

sky_usize_t sky_tcp_wait_read_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_sync_wait_t *wait
);

sky_usize_t sky_tcp_wait_write(
        sky_tcp_cli_t *cli,
        sky_uchar_t *buf,
        sky_usize_t size,
        sky_sync_wait_t *wait
);

sky_usize_t sky_tcp_wait_write_vec(
        sky_tcp_cli_t *cli,
        sky_io_vec_t *vec,
        sky_u32_t num,
        sky_sync_wait_t *wait
);

sky_usize_t sky_tcp_wait_send_fs(
        sky_tcp_cli_t *cli,
        const sky_tcp_fs_data_t *packet,
        sky_sync_wait_t *wait
);


sky_bool_t sky_tcp_cli_wait_close(sky_tcp_cli_t *cli, sky_sync_wait_t *wait);


#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_TCP_WAIT_H
