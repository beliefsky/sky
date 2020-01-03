#ifndef SKY_PALLOC_H
#define SKY_PALLOC_H
#if defined(__cplusplus)
extern "C" {
#endif

#include "types.h"

#define SKY_PAGESIZE            0x1000              /*4096*/
#define SKY_DEFAULT_POOL_SIZE   0x4000              /*(16 * 1024) = 16384*/

typedef void (*sky_pool_cleanup_pt)(void *data);

typedef struct sky_pool_cleanup_s sky_pool_cleanup_t;

struct sky_pool_cleanup_s {
    sky_pool_cleanup_pt handler;
    void *data;
    sky_pool_cleanup_t *next;
};
typedef struct sky_pool_large_s sky_pool_large_t;

struct sky_pool_large_s {
    sky_pool_large_t *next;
    void *alloc;
};
typedef struct sky_pool_s sky_pool_t;

typedef struct {
    sky_uchar_t *last;
    sky_uchar_t *end;
    sky_pool_t *next;
    sky_int_t failed;
} sky_pool_data_t;

struct sky_pool_s {
    sky_pool_data_t d;
    sky_size_t max;
    sky_pool_t *current;
    sky_pool_large_t *large;
    sky_pool_cleanup_t *cleanup;
};

sky_pool_t *sky_create_pool(sky_size_t size);

void sky_destroy_pool(sky_pool_t *pool);

void sky_reset_pool(sky_pool_t *pool);

void *sky_palloc(sky_pool_t *pool, sky_size_t size);

void *sky_pnalloc(sky_pool_t *pool, sky_size_t size);

void *sky_pcalloc(sky_pool_t *pool, sky_size_t size);

void *sky_pmemalign(sky_pool_t *pool, sky_size_t size, sky_size_t alignment);

sky_bool_t sky_pfree(sky_pool_t *pool, void *p);

sky_pool_cleanup_t *sky_pool_cleanup_add(sky_pool_t *p, sky_size_t size);

#if defined(__cplusplus)
} /* extern "C" { */
#endif

#endif //SKY_PALLOC_H
