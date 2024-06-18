//
// Created by weijing on 2024/5/23.
//

#if defined(__i386__) && defined(__ELF__)

asm(
        ".text\n\t"
        ".globl sky_context_make\n\t"
        ".align 2\n\t"
        ".type sky_context_make,@function\n\t"
        "sky_context_make:\n\t"

        // save the stack top to eax
        "   movl 4(%esp), %eax\n\t"

        // reserve space for first argument(from) of context-function
        "   leal -8(%eax), %eax\n\t"

        // 16-align of the stack top address for macosx
        "   andl $-16, %eax\n\t"

        // reserve space for context-data on context-stack
        "   leal -24(%eax), %eax\n\t"

        /* context.ebx = func
         *
         * @note ebp will be affected only when enter into the context function first
         */
        "   movl 12(%esp), %edx\n\t"
        "   movl %edx, 8(%eax)\n\t"

        /* init retval = a writeable space (context)
         *
         * it will write context.edi and context.esi (unused) when jump to a new context function entry first
         */
        "   movl %eax, 20(%eax)\n\t"

        // context.eip = the address of label __entry
        "   call 1f\n\t"
        "1:  popl %ecx\n\t"
        "   addl $__entry - 1b, %ecx\n\t"
        "   movl %ecx, 16(%eax)\n\t"

        /* context.ebp = the address of label __end
         *
         * @note ebp will be affected only when enter into the context function first
         */
        "   call 2f\n\t"
        "2:  popl %ecx\n\t"
        "   addl $__end - 2b, %ecx\n\t"
        "   movl %ecx, 12(%eax)\n\t"

        // return the context pointer
        "   ret\n\t"

        " __entry:\n\t"

        /* pass arguments(context: eax, data: edx) to the context function
         *
         *              patch __end
         *                  |
         *                  |        old-context
         *              ----|------------------------------------
         * context: .. | retval | context |   data   |  padding  |
         *              -----------------------------------------
         *             0        4     arguments
         *             |        |
         *            esp    16-align
         *           (now)
         */
        "   movl %eax, (%esp)\n\t"
        "   movl %edx, 0x4(%esp)\n\t"

        // retval = the address of label __end
        "   pushl %ebp\n\t"

        /* jump to the context function entry
         *
         * @note need not adjust stack pointer(+4) using 'ret $4' when enter into function first
         */
        "   jmp *%ebx\n\t"

        "__end:\n\t"
        // exit(0)
        "   xorl  %eax, %eax\n\t"
        "   movl  %eax, (%esp)\n\t"
        "   call  _exit@PLT\n\t"
        "   hlt\n\t"
        ".size sky_context_make,.-sky_context_make\n\t"
        ".section .note.GNU-stack,\"\",%progbits\n\t"
        );

asm(
        ".text\n\t"
        ".globl sky_context_jump\n\t"
        ".align 2\n\t"
        ".type sky_context_jump,@function\n\t"
        "sky_context_jump:\n\t"
        // save registers and construct the current context
        "   pushl %ebp\n\t"
        "   pushl %ebx\n\t"
        "   pushl %esi\n\t"
        "   pushl %edi\n\t"

        // save the old context(esp) to eax
        "   movl %esp, %eax\n\t"

        // ecx = argument(context)
        "   movl 24(%esp), %ecx\n\t"

        // edx = argument(data)
        "   movl 28(%esp), %edx\n\t"

        // switch to the new context(esp) and stack
        "   movl %ecx, %esp\n\t"

        // restore registers of the new context
        "   popl %edi\n\t"
        "   popl %esi\n\t"
        "   popl %ebx\n\t"
        "   popl %ebp\n\t"

        /* return from-context(retval: [to_esp + 4](context: eax, data: edx)) from jump
         *
         * it will write context.edi and context.esi (unused) when jump to a new context function entry first
         */
        "   movl 4(%esp), %ecx\n\t"
        "   movl %eax, (%ecx)\n\t"
        "   movl %edx, 4(%ecx)\n\t"

        /* jump to the return or entry address(eip)
         *
         * @note need adjust stack pointer(+4) when return from sky_context_jump()
         *
         *                           old-context
         *              ---------------------------------------------------
         * context: .. |   eip   | retval | context |   data   |  padding  |
         *              ---------------------------------------------------
         *             0         4        8     arguments
         *             |                  |
         *            esp              16-align
         *           (now)
         */
        "   ret $4\n\t"
        ".size sky_context_jump,.-sky_context_jump\n\t"
        ".section .note.GNU-stack,\"\",%progbits\n\t"
        );

#endif