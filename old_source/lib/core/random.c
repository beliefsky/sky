//
// Created by edz on 2021/5/12.
//

#include "random.h"

#include <fcntl.h>
#include <unistd.h>

sky_bool_t
sky_random_bytes(sky_uchar_t *in, sky_u32_t size) {
#ifdef O_CLOEXEC
    sky_i32_t fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
#else
    sky_i32_t fd = open("/dev/urandom", O_RDONLY);
#endif
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    if (sky_unlikely(read(fd, in, size) < 0)) {
        close(fd);
        return false;
    }
    close(fd);

    return true;
}
