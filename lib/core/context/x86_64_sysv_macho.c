//
// Created by weijing on 2024/5/23.
//
//
// Created by weijing on 2024/5/23.
//

#if defined(__x86_64__) && defined(__APPLE__)

asm(
        ".text\n\t"
        ".globl _sky_context_make\n\t"
        ".align 8\n\t"
        "_sky_context_make:\n\t"

        "   movq %rdi, %rax\n\t"

        // 16-align for the stack top address
        "   movabs $-16, %r8\n\t"
        "   andq %r8, %rax\n\t"

        // reserve space for context-data on context-stack
        "   leaq -56(%rax), %rax\n\t"

        // context.rbx = func
        "   movq %rdx, 32(%rax)\n\t"

        // context.rip = the address of label __entry
        "   leaq __entry(%rip), %rcx\n\t"
        "   movq %rcx, 48(%rax)\n\t"

        // context.end = the address of label __end
        "   leaq __end(%rip), %rcx\n\t"
        "   movq %rcx, 40(%rax)\n\t"

        // return the context pointer
        "   ret\n\t"

        "__entry:\n\t"

        // pass old-context(context: rdi, priv: rsi) argument to the context function
        "   movq %rax, %rdi\n\t"

        // patch __end
        "   push %rbp\n\t"

        /* jump to the context function entry(rip)
         *
         *             -------------------------------
         * context: .. |   end   | args | padding ... |
         *             -------------------------------
         *             0         8
         *             | 16-align for macosx
         *            rsp
         */
        "   jmp *%rbx\n\t"

        "__end:\n\t"
        // exit(0)
        "   xorq %rdi, %rdi\n\t"
        "   call _exit\n\t"
        "   hlt\n\t"
        );

asm(
        ".text\n\t"
        ".globl _sky_context_jump\n\t"
        ".align 8\n\t"
        "_sky_context_jump:\n\t"
        // save registers and construct the current context
        "   pushq %rbp\n\t"
        "   pushq %rbx\n\t"
        "   pushq %r15\n\t"
        "   pushq %r14\n\t"
        "   pushq %r13\n\t"
        "   pushq %r12\n\t"

        // save the old context(rsp) to rax
        "   movq %rsp, %rax\n\t"

        // switch to the new context(rsp) and stack
        "   movq %rdi, %rsp\n\t"

        // restore registers of the new context
        "   popq %r12\n\t"
        "   popq %r13\n\t"
        "   popq %r14\n\t"
        "   popq %r15\n\t"
        "   popq %rbx\n\t"
        "   popq %rbp\n\t"

        // restore the return or function address(rip)
        "   popq %r8\n\t"

        // return from-context(context: rax, data: rdx) from jump
        "   movq %rsi, %rdx\n\t"

        /* jump to the return or function address(rip)
         *
         *              ---------------------
         * context: .. |  args | padding ... |
         *              ---------------------
         *             0       8
         *             | 16-align for macosx
         *            rsp
         */
        "   jmp *%r8\n\t"
        );

#endif

