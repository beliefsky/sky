//
// Created by weijing on 18-2-8.
//
#include <io/event_loop.h>
#include <core/log.h>
#include <core/number.h>


int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);

    sky_usize_t a = 5;

    sky_str_t in = sky_string("12345678");

    sky_usize_t r;
    for (int i = 0; i < 1000000000; ++i) {
        r = sky_str_to_num(&in, &a);
    }

    sky_log_info("%lu %lu", r, a);

    return 0;
}

