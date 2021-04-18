//
// Created by weijing on 18-9-2.
//

#include "mem_pool.h"
#include "memory.h"

typedef struct sky_mem_block_s  sky_mem_block_t;
typedef struct sky_mem_entry_s  sky_mem_entry_t;

struct sky_mem_block_s {
    sky_mem_block_t *prev;
    sky_mem_block_t *next;
};

struct sky_mem_entry_s {
    sky_mem_entry_t *prev;
    sky_mem_entry_t *next;
};

struct sky_mem_pool_s {
    sky_size_t      block_size;
    sky_size_t      entry_size;
    sky_size_t      entry_array_size;
    sky_uintptr_t   tmp_pos;
    sky_uintptr_t   tmp_end;
    sky_mem_block_t blocks;
    sky_mem_entry_t entries;
};


static void sky_mem_pool_block_create(sky_mem_pool_t *pool);

sky_mem_pool_t*
sky_mem_pool_create(sky_size_t size, sky_size_t num) {
    sky_mem_pool_t *pool;

    if (num < 16) {
        num = 16;
    }
    pool = sky_memalign(0x10, sizeof(sky_mem_pool_t) + (sizeof(sky_mem_entry_t) + size) * num);
    pool->entry_size = sizeof(sky_mem_entry_t) + size;
    pool->entry_array_size = pool->entry_size * num;
    pool->block_size = sizeof(sky_mem_block_t) + pool->entry_array_size;
    pool->blocks.next = pool->blocks.prev = &pool->blocks;
    pool->entries.next = pool->entries.prev = &pool->entries;

    pool->tmp_pos = (sky_uintptr_t)pool + sizeof(sky_mem_pool_t);
    pool->tmp_end = pool->tmp_pos + pool->entry_array_size;

    return pool;
}


void*
sky_mem_pool_get(sky_mem_pool_t *pool) {
    sky_mem_entry_t *entry;

    entry = pool->entries.next;
    if (entry == &pool->entries) {
        if (sky_unlikely(pool->tmp_pos == pool->tmp_end)) {
            sky_mem_pool_block_create(pool);
        }
        entry = (sky_mem_entry_t *) pool->tmp_pos;
        pool->tmp_pos += pool->entry_size;
    } else {
        entry->prev->next = entry->next;
        entry->next->prev = entry->prev;
    }
    return (void *) ((sky_uintptr_t)entry + sizeof(sky_mem_entry_t));
}


void
sky_mem_pool_put(sky_mem_pool_t *pool, void *ptr) {
    sky_mem_entry_t *entry;

    entry = (sky_mem_entry_t *) ((sky_uintptr_t) ptr - sizeof(sky_mem_entry_t));
    entry->next = &pool->entries;
    entry->prev = entry->next->prev;
    entry->next->prev = entry->prev->next = entry;
}


void sky_mem_pool_reset(sky_mem_pool_t *pool) {
    sky_mem_block_t *block;

    if ((block = pool->blocks.next) != &pool->blocks) {
        do {
            block->prev->next = block->next;
            block->next->prev = block->prev;
            sky_free(block);
        } while ((block = pool->blocks.next) != &pool->blocks);

        pool->entries.next = pool->entries.prev = &pool->entries;
        pool->tmp_pos = (sky_uintptr_t) pool + sizeof(sky_mem_pool_t);
        pool->tmp_end = pool->tmp_pos + pool->entry_array_size;
    }
}


void
sky_mem_pool_destroy(sky_mem_pool_t *pool) {
    sky_mem_block_t *block;

    while ((block = pool->blocks.next) != &pool->blocks) {
        block->prev->next = block->next;
        block->next->prev = block->prev;
        sky_free(block);
    }
    sky_free(pool);
}

static sky_inline void
sky_mem_pool_block_create(sky_mem_pool_t *pool) {
    sky_mem_block_t *block;

    block = sky_memalign(0x10, pool->block_size);
    block->next = &pool->blocks;
    block->prev = block->next->prev;
    block->prev->next = block->next->prev = block;

    pool->tmp_pos = (sky_uintptr_t) block + sizeof(sky_mem_block_t);
    pool->tmp_end = pool->tmp_pos + pool->entry_array_size;
}