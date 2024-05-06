//
// Created by weijing on 2024/5/6.
//
#include "./coro_common.h"
#include <core/memory.h>
#include <core/log.h>



typedef struct {
    sky_coro_context_t caller;
    sky_coro_t *current;
} coro_switcher_t;

struct coro_block_s {
    coro_block_t *next;
};


static sky_usize_t coro_resume(sky_coro_t *coro);

static sky_usize_t coro_yield(sky_coro_t *coro, sky_usize_t value);

static void mem_block_add(sky_coro_t *coro);


static sky_thread coro_switcher_t thread_switcher = {
        .current = null
};


sky_api sky_coro_t *
sky_coro_create(const sky_coro_func_t func, void *const data) {
    return sky_coro_create_with_stack(func, data, CORO_DEFAULT_STACK_SIZE);
}


sky_api sky_coro_t *
sky_coro_create_with_stack(sky_coro_func_t func, void *data, const sky_usize_t stack_size) {
    sky_coro_t *const coro = sky_coro_new_with_stack(stack_size);
    if (sky_unlikely(!coro)) {
        return null;
    }

    sky_coro_set(coro, func, data);

    return coro;
}

sky_api sky_coro_t *
sky_coro_new() {
    return sky_coro_new_with_stack(CORO_DEFAULT_STACK_SIZE);
}

sky_api sky_coro_t *
sky_coro_new_with_stack(sky_usize_t stack_size) {
    stack_size = (stack_size + SKY_USIZE(63)) & ~SKY_USIZE(63); // 64字节对齐
    sky_coro_t *const coro = sky_malloc(sizeof(sky_coro_t) + PAGE_SIZE + stack_size);
    if (sky_unlikely(!coro)) {
        return null;
    }
    coro->block = null;
    coro->stack_size = stack_size;
    coro->ptr_size = PAGE_SIZE;
    coro->ptr = ((sky_uchar_t *) coro) + sizeof(sky_coro_t) + stack_size;


    return coro;
}

sky_api sky_usize_t
sky_coro_resume(sky_coro_t *const coro) {
    return coro_resume(coro);
}

sky_api sky_usize_t
sky_coro_resume_value(sky_coro_t *const coro, const sky_usize_t value) {
    coro->yield_value = value;
    return coro_resume(coro);
}



sky_api sky_usize_t
sky_coro_yield(const sky_usize_t value) {
    sky_coro_t *const current = thread_switcher.current;
    if (sky_unlikely(!current)) {
        sky_log_error("coro not run");
        __builtin_unreachable();
    }

    return coro_yield(current, value);
}

sky_api sky_coro_t *
sky_coro_current() {
    return thread_switcher.current;
}

sky_api void
sky_coro_destroy(sky_coro_t *const coro) {
    for (coro_block_t *block = coro->block; block; block = block->next) {
        sky_free(block);
    }
    sky_free(coro);
}


sky_api void *
sky_coro_malloc(sky_coro_t *const coro, const sky_u32_t size) {
    if (sky_unlikely(coro->ptr_size < size)) {
        if (sky_unlikely(size > 512)) {
            coro_block_t *const block = sky_malloc(size + sizeof(coro_block_t));
            block->next = coro->block;
            coro->block = block;

            return (sky_uchar_t *) (block + 1);
        }
        mem_block_add(coro);
    }
    sky_uchar_t *const ptr = coro->ptr;
    coro->ptr += size;
    coro->ptr_size -= size;

    return ptr;
}

void
coro_entry_point(sky_coro_t *const coro, const sky_coro_func_t func, void *const data) {
    return (void) coro_yield(coro, func(coro, data));
}

static sky_inline sky_usize_t
coro_resume(sky_coro_t *const coro) {
    coro_switcher_t *const switcher = &thread_switcher;

    coro->parent = switcher->current;
    switcher->current = coro;
    coro_swap_context(
            (!!coro->parent) ? &coro->parent->context : &switcher->caller,
            &coro->context
    );
    switcher->current = coro->parent;

    return coro->yield_value;
}

static sky_inline sky_usize_t
coro_yield(sky_coro_t *const coro, const sky_usize_t value) {
    coro->yield_value = value;
    coro_swap_context(
            &coro->context,
            (!!coro->parent) ? &coro->parent->context : &thread_switcher.caller
    );

    return coro->yield_value;
}

static sky_inline void
mem_block_add(sky_coro_t *const coro) {
    coro_block_t *const block = sky_malloc(PAGE_SIZE);
    block->next = coro->block;
    coro->block = block;

    coro->ptr = (sky_uchar_t *) (block + 1);
    coro->ptr_size = PAGE_SIZE - sizeof(coro_block_t);
}

