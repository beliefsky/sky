//
// Created by weijing on 2024/3/4.
//

/*

 const sky_io_vec_t *item = vec;
    sky_usize_t size = 0;
    sky_u32_t i = num;
    do {
        size += item->size;
        --i;
        ++item;
    } while (i > 0);

    if (sky_unlikely(!size)) {
        return 0;
    }

    struct msghdr msg = {
            .msg_iov = (struct iovec *) vec,
#if defined(__linux__)
            .msg_iovlen = num
#else
            .msg_iovlen = (sky_i32_t) num
#endif
    };

    const sky_isize_t n = recvmsg(sky_ev_get_fd(&tcp->ev), &msg, 0);



 */
