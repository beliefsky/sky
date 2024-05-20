//
// Created by weijing on 2024/5/20.
//
#ifdef __arm__

#include "./context_common.h"

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
#define CONTEXT_SJLJ_BYTES    "4"
#else
#define CONTEXT_SJLJ_BYTES    "0"
#endif

asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_make)
    // save the stack top to r0
    "add r0, r0, r1\n\t"

    // 16-align of the stack top address
    "bic r0, r0, #15\n\t"

    /* reserve space for context-data on context-stack
     *
     * 64 = align8(52 + CONTEXT_SJLJ_BYTES)
     */
    "sub r0, r0, #64\n\t"

    // context.pc = func
    "str r2, [r0, #40 + "CONTEXT_SJLJ_BYTES"]\n\t"

    /* init retval = a writeable space (context)
     *
     * it will write retval(context, priv) when jump to a new context function entry first
     */
    "add r1, r0, #44 + "CONTEXT_SJLJ_BYTES"\n\t"
    "str r1, [r0, #0 + "CONTEXT_SJLJ_BYTES"]\n\t"

    // context.lr = address of label __end
    "adr r1, __end\n\t"
    "str r1, [r0, #36 + "CONTEXT_SJLJ_BYTES"]\n\t"

    // return pointer to context-data
    "bx lr\n\t"

    "__end:\n\t"
    // exit(0)
    "mov r0, #0\n\t"
#ifdef ARCH_ELF
    "bl _exit@PLT\n\t"
#else
    "bl __exit\n\t"
#endif
);


asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_jump)
    // save lr as pc
    "push {lr}\n\t"

    // save retval, r4 - r11, lr
    "push {r0, r4 - r11, lr}\n\t"

#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    // load tls to save or restore sjlj handler
    "mrc p15, 0, r5, c13, c0, #3\n\t"
    "bic r5, r5, #3\n\t"

    // load and save sjlj handler: tls[__PTK_LIBC_DYLD_Unwind_SjLj_Key]
    "ldr r4, [r5, #8]\n\t"
    "push {r4}\n\t"
#endif

    // save the old context(sp) to r0
    "mov r0, sp\n\t"

    // switch to the new context(sp) and stack
    "mov sp, r1\n\t"

#if defined(TARGET_OS_IOS) && TARGET_OS_IOS
    // restore sjlj handler
    "pop {r4}\n\t"
    "str r4, [r5, #8]\n\t"
#endif

    // restore retval, r4 - r11, lr
    "pop {r3, r4 - r11, lr}\n\t"

    // return from-context: retval(context: r0, priv: r2) from jump
    "str r0, [r3, #0]\n\t"
    "str r2, [r3, #4]\n\t"

    // pass old-context(context: r0, priv: r1 = r2) arguments to the context function
    "mov r1, r2\n\t"

    /* jump to the return or entry address(pc)
     *
     *               func      retval(from)
     *             ---------------------------------------
     * context:   |   pc   | context |  priv  |  padding  |
     *             ---------------------------------------
     *            0        4         8
     *            |
     *            sp
     */
    "pop {pc}\n\t"
);

#endif

