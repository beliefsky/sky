//
// Created by edz on 2021/5/12.
//

#include "random.h"

#if defined(__WINNT__)
#include "../core/memory.h"
#include <stdlib.h>

#else
#include <fcntl.h>
#include <unistd.h>

#endif

sky_bool_t
sky_random_bytes(sky_uchar_t *in, sky_u32_t size) {
    sky_i32_t value;

    while (size >= 4) {
        value = rand();
        sky_memcpy4(in, &value);

        size -= 4;
        in += 4;
    }
    if (size > 0) {
        value = rand();
        sky_memcpy(in, &value, size);
    }

    return true;
}

#if defined(__WINNT__)

#else

sky_bool_t
sky_random_bytes(sky_uchar_t *in, sky_u32_t size) {
    sky_i32_t fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
    if (sky_unlikely(fd < 0)) {
        return false;
    }
    rand()

    if (sky_unlikely(read(fd, in, size) < 0)) {
        close(fd);
        return false;
    }
    close(fd);

    return true;
}

#endif
