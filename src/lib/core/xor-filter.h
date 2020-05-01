//
// Created by weijing on 2020/4/29.
//

#ifndef SKY_XOR_FILTER_H
#define SKY_XOR_FILTER_H

#include "types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    sky_uint64_t seed;
    sky_uint64_t blockLength;
    sky_uint8_t *fingerprints; // after xor8_allocate, will point to 3*blockLength values
} sky_xor8_t;

typedef struct {
    sky_uint64_t seed;
    sky_uint64_t blockLength;
    uint16_t *fingerprints; // after xor16_allocate, will point to 3*blockLength values
} sky_xor16_t;

sky_bool_t sky_xor8_init(sky_xor8_t *filter, sky_uint32_t size);

sky_bool_t sky_xor16_init(sky_xor16_t *filter, sky_uint32_t size);

sky_bool_t sky_xor8_populate(sky_xor8_t *filter, sky_uint64_t *keys, sky_uint32_t size);

sky_bool_t sky_xor16_populate(sky_xor16_t *filter, sky_uint64_t *keys, sky_uint32_t size);

sky_bool_t sky_xor8_buffered_populate(sky_xor8_t *filter, sky_uint64_t *keys, sky_uint32_t size);

sky_bool_t sky_xor16_buffered_populate(sky_xor16_t *filter, sky_uint64_t *keys, sky_uint32_t size);

sky_bool_t sky_xor8_contain(sky_xor8_t *filter, sky_uint64_t key);

sky_bool_t sky_xor16_contain(sky_xor16_t *filter, sky_uint64_t key);

// report memory usage
static inline size_t sky_xor8_size_in_bytes(const sky_xor8_t *filter) {
    return 3 * filter->blockLength * sizeof(sky_uint8_t) + sizeof(sky_xor8_t);
}

// report memory usage
static inline size_t sky_xor16_size_in_bytes(const sky_xor16_t *filter) {
    return 3 * filter->blockLength * sizeof(uint16_t) + sizeof(sky_xor16_t);
}

#if defined(__cplusplus)
} /* extern "C" { */
#endif
#endif //SKY_XOR_FILTER_H
