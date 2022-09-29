//
// Created by weijing on 18-2-8.
//
#include <core/types.h>
#include <core/log.h>

int
main() {
    setvbuf(stdout, null, _IOLBF, 0);
    setvbuf(stderr, null, _IOLBF, 0);


    return 0;
}
