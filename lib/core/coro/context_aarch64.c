//
// Created by weijing on 2024/5/6.
//

#if defined(__aarch64__)

#include "./coro_common.h"

sky_inline void
sky_coro_set(
        sky_coro_t *coro,
        sky_coro_func_t func,
        void *data
) {
    const sky_usize_t stack = (sky_usize_t) coro->stack + coro->stack_size;

    coro->context[19/* x28 */] = (sky_usize_t) data;
    coro->context[0 /* x0  */] = (sky_usize_t) coro;
    coro->context[1 /* x1  */] = (sky_usize_t) func;
    coro->context[5 /* lr  */] = (sky_usize_t) coro_entry_point;
    coro->context[4 /* RSP */] = stack & ~SKY_USIZE(0xF);
}

asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(coro_swap_context)
    "mov x10, sp\n\t"
    "mov x11, x30\n\t"
    "stp x8, x9, [x0, #(1*16)]\n\t"
    "stp x10, x11, [x0, #(2*16)]\n\t"
    "stp x12, x13, [x0, #(3*16)]\n\t"
    "stp x14, x15, [x0, #(4*16)]\n\t"
    "stp x19, x20, [x0, #(5*16)]\n\t"
    "stp x21, x22, [x0, #(6*16)]\n\t"
    "stp x23, x24, [x0, #(7*16)]\n\t"
    "stp x25, x26, [x0, #(8*16)]\n\t"
    "stp x27, x28, [x0, #(9*16)]\n\t"
    "stp x29, x30, [x0, #(10*16)]\n\t"
    "stp x0, x1, [x0, #(0*16)]\n\t"
    "ldp x8, x9, [x1, #(1*16)]\n\t"
    "ldp x10, x11, [x1, #(2*16)]\n\t"
    "ldp x12, x13, [x1, #(3*16)]\n\t"
    "ldp x14, x15, [x1, #(4*16)]\n\t"
    "ldp x19, x20, [x1, #(5*16)]\n\t"
    "ldp x21, x22, [x1, #(6*16)]\n\t"
    "ldp x23, x24, [x1, #(7*16)]\n\t"
    "ldp x25, x26, [x1, #(8*16)]\n\t"
    "ldp x27, x28, [x1, #(9*16)]\n\t"
    "ldp x29, x30, [x1, #(10*16)]\n\t"
    "ldp x0, x1, [x1, #(0*16)]\n\t"
    "mov sp, x10\n\t"
    "mov x2, x28\n\t"
    "br x11\n\t");

#endif

