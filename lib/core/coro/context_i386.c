//
// Created by weijing on 2024/5/6.
//

#if defined(__i386__)

#include "./coro_common.h"

sky_inline void
sky_coro_set(
        sky_coro_t *coro,
        sky_coro_func_t func,
        void *data
) {
    sky_usize_t stack = (sky_usize_t) coro->stack + coro->stack_size;
    /* Make room for 3 args */
    stack -= sizeof(sky_usize_t) * 3;
    /* Ensure 4-byte alignment */
    stack = stack & SKY_USIZE(0x3);

    sky_usize_t *args = (sky_usize_t *) stack;

    args[0] = 0;
    args[1] = (sky_usize_t) coro;
    args[2] = (sky_usize_t) func;
    args[3] = (sky_usize_t) data;

    coro->context[5 /* EIP */] = (sky_usize_t) coro_entry_point;
    coro->context[6] = stack;
}


asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(coro_swap_context)
    "movl   0x4(%esp),%eax\n\t"
    "movl   %ecx,0x1c(%eax)\n\t" /* ECX */
    "movl   %ebx,0x0(%eax)\n\t"  /* EBX */
    "movl   %esi,0x4(%eax)\n\t"  /* ESI */
    "movl   %edi,0x8(%eax)\n\t"  /* EDI */
    "movl   %ebp,0xc(%eax)\n\t"  /* EBP */
    "movl   (%esp),%ecx\n\t"
    "movl   %ecx,0x14(%eax)\n\t" /* EIP */
    "leal   0x4(%esp),%ecx\n\t"
    "movl   %ecx,0x18(%eax)\n\t" /* ESP */
    "movl   8(%esp),%eax\n\t"
    "movl   0x14(%eax),%ecx\n\t" /* EIP (1) */
    "movl   0x18(%eax),%esp\n\t" /* ESP */
    "pushl  %ecx\n\t"            /* EIP (2) */
    "movl   0x0(%eax),%ebx\n\t"  /* EBX */
    "movl   0x4(%eax),%esi\n\t"  /* ESI */
    "movl   0x8(%eax),%edi\n\t"  /* EDI */
    "movl   0xc(%eax),%ebp\n\t"  /* EBP */
    "movl   0x1c(%eax),%ecx\n\t" /* ECX */
    "ret\n\t"
);

#endif
