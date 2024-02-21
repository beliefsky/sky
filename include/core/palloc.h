#ifndef SKY_PALLOC_H
#define SKY_PALLOC_H

#include "./types.h"

#if defined(__cplusplus)
extern "C" {
#endif

#define SKY_POOL_DEFAULT_SIZE   16384

typedef struct sky_pool_large_s sky_pool_large_t;
typedef struct sky_pool_s sky_pool_t;

struct sky_pool_large_s {
    sky_pool_large_t *next;
    void *alloc;
};

typedef struct {
    sky_uchar_t *last;
    sky_uchar_t *end;
    sky_pool_t *next;
    sky_isize_t failed;
} sky_pool_data_t;

struct sky_pool_s {
    sky_pool_data_t d;
    sky_usize_t max;
    sky_pool_t *current;
    sky_pool_large_t *large;
};

sky_pool_t *sky_pool_create(sky_usize_t size);

void sky_pool_destroy(sky_pool_t *pool);

void sky_pool_reset(sky_pool_t *pool);

void *sky_palloc(sky_pool_t *pool, sky_usize_t size);

void *sky_pnalloc(sky_pool_t *pool, sky_usize_t size);

void *sky_pcalloc(sky_pool_t *pool, sky_usize_t size);

void *sky_prealloc(sky_pool_t *pool, void *ptr, sky_usize_t ptr_size, sky_usize_t size);

void sky_pfree(sky_pool_t *pool, const void *ptr, sky_usize_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_PALLOC_H
