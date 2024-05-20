//
// Created by weijing on 2024/5/6.
//
#if defined(__x86_64__)

#include "context_common.h"

#ifdef __WINNT__
asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_make)
    // save the stack top to rax
    "movq %rcx, %rax\n\t"
    "addq %rdx, %rax\n\t"

    // reserve space for first argument(from) and retval(from) item of context-function
    "subq $32, %rax\n\t"

    // 16-align of the stack top address
    "andq $-16, %rax\n\t"
    "subq $112, %rax\n\t"

    // context.rbx = func
    "movq %r8, 80(%rax)\n\t"

    // save bottom address of context stack as 'limit'
    "movq %rcx, 16(%rax)\n\t"

    // save address of context stack limit as 'dealloction stack'
    "movq %rcx, 8(%rax)\n\t"

    // save top address of context stack as 'base'
    "addq %rdx, %rcx\n\t"
    "movq %rcx, 24(%rax)\n\t"

    // init fiber-storage to zero
    "xorq %rcx, %rcx\n\t"
    "movq %rcx, (%rax)\n\t"

    // init context.retval(saved) = a writeable space (unused)
    // it will write context (unused) and data (unused) when jump to a new context function entry first
    "leaq 128(%rax), %rcx\n\t"
    "movq %rcx, 96(%rax)\n\t"

    // context.rip = the address of label __entry
    "leaq __entry(%rip), %rcx\n\t"
    "movq %rcx, 104(%rax)\n\t"

    // context.end = the address of label __end
    "leaq __end(%rip), %rcx\n\t"
    "movq %rcx, 88(%rax)\n\t"

    // return pointer to context-data
    "ret\n\t"
    "__entry:\n\t"

    // patch return address (__end) on stack
    "push %rbp\n\t"

    // jump to the context function entry(rip)
    "jmp *%rbx\n\t"
    "__end:\n\t"
    "xorq %rcx, %rcx\n\t"
    "call _exit\n\t"
    "hlt\n\t"
);


asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_jump)
    // save the hidden argument: retval (from-context)
    "pushq %rcx\n\t"

    // save registers and construct the current context
    "pushq %rbp\n\t"
    "pushq %rbx\n\t"
    "pushq %rsi\n\t"
    "pushq %rdi\n\t"
    "pushq %r15\n\t"
    "pushq %r14\n\t"
    "pushq %r13\n\t"
    "pushq %r12\n\t"

    // load TIB
    "movq %gs:(0x30), %r10\n\t"

    // save current stack base
    "movq 0x08(%r10), %rax\n\t"
    "pushq %rax\n\t"

    // save current stack limit
    "movq 0x10(%r10), %rax\n\t"
    "pushq %rax\n\t"

    // save current deallocation stack
    "movq 0x1478(%r10), %rax\n\t"
    "pushq %rax\n\t"

    // save fiber local storage
    "movq 0x18(%r10), %rax\n\t"
    "pushq %rax\n\t"

    // save the old context(esp) to r9
    "movq %rsp, %r9\n\t"

    // switch to the new context(esp) and stack
    "movq %rdx, %rsp\n\t"
    // load TIB

    "movq %gs:(0x30), %r10\n\t"

    // restore fiber local storage
    "popq %rax\n\t"
    "movq %rax, 0x18(%r10)\n\t"

    // restore deallocation stack
    "popq %rax\n\t"
    "movq %rax, 0x1478(%r10)\n\t"

    // restore stack limit
    "popq %rax\n\t"
    "mov %rax, 0x10(%r10)\n\t"

    // restore stack base
    "popq %rax\n\t"
    "movq %rax, 0x08(%r10)\n\t"

    // restore registers of the new context
    "popq %r12\n\t"
    "popq %r13\n\t"
    "popq %r14\n\t"
    "popq %r15\n\t"
    "popq %rdi\n\t"
    "popq %rsi\n\t"
    "popq %rbx\n\t"
    "popq %rbp\n\t"

    // restore retval (saved) to rax
    "popq %rax\n\t"

    // restore the return or function address(r10)
    "popq %r10\n\t"

    // return from-context(retval: [rcx](context: r9, data: r8)) from jump
    // it will write context (unused) and data (unused) when jump to a new context function entry first
    "movq %r9, (%rax)\n\t"
    "movq %r8, 8(%rax)\n\t"
    "movq %rax, %rcx\n\t"

    // jump to the return or function address(rip)
    "jmp *%r10\n\t"
);

#else

asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_make)
    // save the stack top to rax
    "addq %rsi, %rdi\n\t"
    "movq %rdi, %rax\n\t"

    // 16-align for the stack top address
    "movabs $-16, %r8\n\t"
    "andq %r8, %rax\n\t"

    // reserve space for context-data on context-stack
    "leaq -56(%rax), %rax\n\t"

    // context.rbx = func
    "movq %rdx, 32(%rax)\n\t"

    // context.rip = the address of label __entry
    "leaq __entry(%rip), %rcx\n\t"
    "movq %rcx, 48(%rax)\n\t"

    // context.end = the address of label __end
    "leaq __end(%rip), %rcx\n\t"
    "movq %rcx, 40(%rax)\n\t"

    // return the context pointer
    "ret\n\t"
    "__entry:\n\t"

    // pass old-context(context: rdi, data: rsi) argument to the context function
    "movq %rax, %rdi\n\t"

    // patch __end
    "push %rbp\n\t"

    /* jump to the context function entry(rip)
     *
     *             -------------------------------
     * context: .. |   end   | args | padding ... |
     *             -------------------------------
     *             0         8
     *             | 16-align for macosx
     *            rsp
     */
    "jmp *%rbx\n\t"

    "__end:\n\t"

    // exit(0)
    "xorq %rdi, %rdi\n\t"
    #ifdef ARCH_ELF
    "call _exit@PLT\n\t"
    #else
    "call __exit\n\t"
    #endif
    "hlt\n\t"
);

asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_jump)
    // save registers and construct the current context
    "pushq %rbp\n\t"
    "pushq %rbx\n\t"
    "pushq %r15\n\t"
    "pushq %r14\n\t"
    "pushq %r13\n\t"
    "pushq %r12\n\t"

    // save the old context(rsp) to rax
    "movq %rsp, %rax\n\t"

    // switch to the new context(rsp) and stack
    "movq %rdi, %rsp\n\t"

    // restore registers of the new context
    "popq %r12\n\t"
    "popq %r13\n\t"
    "popq %r14\n\t"
    "popq %r15\n\t"
    "popq %rbx\n\t"
    "popq %rbp\n\t"

    // restore the return or function address(rip)
    "popq %r8\n\t"

    // return from-context(context: rax, data: rdx) from jump
    "movq %rsi, %rdx\n\t"

    /* jump to the return or function address(rip)
     *
     *              ---------------------
     * context: .. |  args | padding ... |
     *              ---------------------
     *             0       8
     *             | 16-align for macosx
     *            rsp
     */
    "jmp *%r8\n\t"
);

#endif

#endif

