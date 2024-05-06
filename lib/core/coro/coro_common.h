//
// Created by weijing on 2024/5/6.
//

#ifndef SKY_CORO_COMMON_H
#define SKY_CORO_COMMON_H

#include <core/coro.h>

#define CORO_DEFAULT_STACK_SIZE 14336
#define PAGE_SIZE 2048


#if defined(__MACH__)
#define ASM_SYMBOL(name_) "_" #name_
#else
#define ASM_SYMBOL(name_) #name_
#endif

#define ASM_ROUTINE(name_) \
    ".globl " ASM_SYMBOL(name_) "\n\t" \
    ASM_SYMBOL(name_) ":\n\t"


#if defined(__x86_64__)

typedef sky_usize_t coro_context_t[10];

#elif defined(__i386__)

typedef sky_usize_t coro_context_t[7];

#elif defined(__aarch64__)

typedef sky_usize_t coro_context_t[22];

#endif


typedef struct coro_block_s coro_block_t;

struct sky_coro_s {
    coro_context_t context;
    sky_usize_t yield_value;
//===================================
    sky_coro_t *parent;
    coro_block_t *block;
    sky_usize_t stack_size;
    sky_usize_t ptr_size;
    sky_uchar_t *ptr;
    sky_uchar_t stack[];
};

void coro_swap_context(coro_context_t *current, coro_context_t *other);

void coro_entry_point(sky_coro_t *coro, sky_coro_func_t func, void *data);

#endif //SKY_CORO_COMMON_H
