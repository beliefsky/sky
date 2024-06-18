//
// Created by weijing on 2024/5/23.
//
//
// Created by weijing on 2024/5/23.
//

#if defined(__i386__) && defined(__APPLE__)

asm(
        ".text\n\t"
        ".globl _sky_context_make\n\t"
        ".align 2\n\t"
        "_sky_context_make:\n\t"
        // save the stack top to eax
        "   movl 4(%esp), %eax\n\t"

        // reserve space for first argument(from) of context-function
        "   leal -8(%eax), %eax\n\t"

        // 16-align of the stack top address for macosx
        "   andl $-16, %eax\n\t"

        // reserve space for context-data on context-stack
        "   leal -20(%eax), %eax\n\t"

        /* context.ebx = func
         *
         * @note ebp will be affected only when enter into the context function first
         */
        "   movl 12(%esp), %edx\n\t"
        "   movl %edx, 8(%eax)\n\t"

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

        "__entry:\n\t"

        // pass old-arguments(context: eax, data: edx) to the context function
        "   movl %eax, (%esp)\n\t"
        "   movl %edx, 0x4(%esp)\n\t"

        // patch __end, retval = the address of label __end
        "   pushl %ebp\n\t"

        /* jump to the context function entry
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
        "   jmp *%ebx\n\t"

        "__end:\n\t"
        // exit(0)
        "   xorl  %eax, %eax\n\t"
        "   movl  %eax, (%esp)\n\t"
        "   call  __exit\n\t"
        "   hlt\n\t"
        );

asm(
        ".text\n\t"
        ".globl _sky_context_jump\n\t"
        ".align 2\n\t"
        "_sky_context_jump:\n\t"
        // save registers and construct the current context
        "   pushl %ebp\n\t"
        "   pushl %ebx\n\t"
        "   pushl %esi\n\t"
        "   pushl %edi\n\t"

        // save the old context(esp) to eax
        "   movl %esp, %eax\n\t"

        // ecx = argument(context)
        "   movl 20(%esp), %ecx\n\t"

        // edx = argument(data)
        "   movl 24(%esp), %edx\n\t"

        // switch to the new context(esp) and stack
        "   movl %ecx, %esp\n\t"

        // restore registers of the new context
        "   popl %edi\n\t"
        "   popl %esi\n\t"
        "   popl %ebx\n\t"
        "   popl %ebp\n\t"

        // restore the return or function address(ecx)
        "   popl %ecx\n\t"

        // return from-context(context: eax, data: edx) from jump
        // ...

        /* jump to the return or function address(eip)
         *
         *
         *                 old-context
         *              --------------------------------
         * context: .. | context |   data   |  padding  |
         *              --------------------------------
         *             0         4    arguments
         *             |
         *            esp  16-align for macosx
         *           (now)
         */
        "   jmp *%ecx\n\t"
        );

#endif

