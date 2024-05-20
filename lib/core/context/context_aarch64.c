//
// Created by weijing on 2024/5/20.
//
#ifdef __aarch64__

#include "./context_common.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_make)

    // save the stack top to x0
    "add x0, x0, x1\n\t"

    // 16-align of the stack top address
    "and x0, x0, ~0xf\n\t"

    /* reserve space for context-data on context-stack
     *
     * 112 = align16(13 * 8)
     */
    "sub x0, x0, #112\n\t"

    // context.pc = func
    "str x2, [x0, #96]\n\t"

    // get the address of label __end
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    /* numeric offset since llvm still does not support labels in adr
     *
     * 0x0c = 3 instructions * size (4) before label '__end'
     *
     * new version llvm have already fix this issues.
     */
     "adr x1, 0x0c\n\t"
#else
    "adr x1, __end\n\t"
#endif

    // context.lr = the address of label __end
    "str x1, [x0, #88]\n\t"

    // return pointer to context-data (x0)
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    "ret lr\n\t"
#else
    "ret x30\n\t"
#endif

    "__end:\n\t"

    // exit(0)
    "mov x0, #0\n\t"
#ifdef ARCH_ELF
    "bl _exit\n\t"
#else
    "bl __exit\n\t"
#endif
);


asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_jump)
    /* prepare stack space first
    *
    * 0x70 = align16(13 * 8)
    */
    "sub sp, sp, #0x70\n\t"

    // save x19 - x30
    "stp x19, x20, [sp, #0x00]\n\t"
    "stp x21, x22, [sp, #0x10]\n\t"
    "stp x23, x24, [sp, #0x20]\n\t"
    "stp x25, x26, [sp, #0x30]\n\t"
    "stp x27, x28, [sp, #0x40]\n\t"
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    "stp fp,  lr,  [sp, #0x50]\n\t"
#else
    "stp x29, x30, [sp, #0x50]\n\t"
#endif

    // save lr as pc
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    "str lr, [sp, #0x60]\n\t"
#else
    "str x30, [sp, #0x60]\n\t"
#endif

    // save the old context(sp) to x4
    "mov x4, sp\n\t"

    // switch to the new context(sp) and stack
    "mov sp, x0\n\t"

    // restore x19 - x30
    "ldp x19, x20, [sp, #0x00]\n\t"
    "ldp x21, x22, [sp, #0x10]\n\t"
    "ldp x23, x24, [sp, #0x20]\n\t"
    "ldp x25, x26, [sp, #0x30]\n\t"
    "ldp x27, x28, [sp, #0x40]\n\t"
#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    "ldp fp,  lr,  [sp, #0x50]\n\t"
#else
    "ldp x29, x30, [sp, #0x50]\n\t"
#endif

    /* pass old-context(context: x0, data: x1) arguments to the context function
     *
     * and return from-context: retval(context: x0, data: x1) from jump
     */
    "mov x0, x4\n\t"

    // load pc
    "ldr x4, [sp, #0x60]\n\t"

    // restore stack space
    "add sp, sp, #0x70\n\t"

    // jump to the return or entry address(pc)
    "ret x4\n\t"
);

#endif

