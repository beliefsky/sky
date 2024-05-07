//
// Created by weijing on 2024/5/6.
//
#if defined(__x86_64__)

#include "./coro_common.h"

void __attribute__((visibility("internal"))) coro_entry_point_x86_64();

sky_inline void
sky_coro_set(
        sky_coro_t *coro,
        sky_coro_func_t func,
        void *data
) {
    const sky_usize_t stack = (sky_usize_t) coro->stack + coro->stack_size;

    coro->context[5 /* R15 */] = (sky_usize_t) data;
    coro->context[6 /* RDI */] = (sky_usize_t) coro;
    coro->context[7 /* RSI */] = (sky_usize_t) func;
    coro->context[8 /* RIP */] = (sky_usize_t) coro_entry_point_x86_64;
    coro->context[9 /* RSP */] = (stack & ~SKY_USIZE(0xF)) - SKY_USIZE(0x8);
}

asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(coro_swap_context)
    "movq   %rbx,0(%rdi)\n\t"
    "movq   %rbp,8(%rdi)\n\t"
    "movq   %r12,16(%rdi)\n\t"
    "movq   %r13,24(%rdi)\n\t"
    "movq   %r14,32(%rdi)\n\t"
    "movq   %r15,40(%rdi)\n\t"
    "movq   %rdi,48(%rdi)\n\t"
    "movq   %rsi,56(%rdi)\n\t"
    "movq   (%rsp),%rcx\n\t"
    "movq   %rcx,64(%rdi)\n\t"
    "leaq   8(%rsp),%rcx\n\t"
    "movq   %rcx,72(%rdi)\n\t"
    "movq   72(%rsi),%rsp\n\t"
    "movq   0(%rsi),%rbx\n\t"
    "movq   8(%rsi),%rbp\n\t"
    "movq   16(%rsi),%r12\n\t"
    "movq   24(%rsi),%r13\n\t"
    "movq   32(%rsi),%r14\n\t"
    "movq   40(%rsi),%r15\n\t"
    "movq   48(%rsi),%rdi\n\t"
    "movq   64(%rsi),%rcx\n\t"
    "movq   56(%rsi),%rsi\n\t"
    "jmpq   *%rcx\n\t"
);


asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(coro_entry_point_x86_64)
    "movq %r15, %rdx\n\t"
    "jmp " ASM_SYMBOL(coro_entry_point) "\n\t"
);


#endif

