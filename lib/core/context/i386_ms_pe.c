//
// Created by weijing on 2024/5/23.
//

#if defined(__i386__) && defined(__WINNT__)

asm(
        ".text\n\t"
        ".p2align 4,,15\n\t"
        /* mark as using no unregistered SEH handlers */
        ".globl	@feat.00\n\t"
        ".def	@feat.00;	.scl	3;	.type	0;	.endef\n\t"
        ".set   @feat.00,   1\n\t"
        ".globl _sky_context_make\n\t"
        ".def _sky_context_make; .scl    2;  .type 32;   .endef\n\t"
        "_sky_context_make:\n\t"
        // save the stack top to eax
        "   movl 4(%esp), %eax\n\t"
        "   movl %eax, %ecx\n\t"

        // reserve space for first argument(from) and seh item of context-function
        "   leal -20(%eax), %eax\n\t"

        // 16-align of the stack top address
        "   andl $-16, %eax\n\t"

        // reserve space for context-data on context-stack
        "   leal -40(%eax), %eax\n\t"

        // save top address of context stack as 'base'
        "   movl %ecx, 12(%eax)\n\t"

        // save bottom address of context-stack as 'limit'
        "   movl 4(%esp), %ecx\n\t"
        "   movl %ecx, 8(%eax)\n\t"

        // save bottom address of context-stack as 'dealloction stack'
        "   movl %ecx, 4(%eax)\n\t"

        // set fiber-storage as zero
        "   xorl %ecx, %ecx\n\t"
        "   movl %ecx, (%eax)\n\t"

        // context.ebx = func
        "   movl 12(%esp), %ecx\n\t"
        "   movl %ecx, 28(%eax)\n\t"

        // context.eip = __entry
        "   movl $__entry, %ecx\n\t"
        "   movl %ecx, 36(%eax)\n\t"

        // context.ebp = the address of label __end
        "   movl $__end, %ecx\n\t"
        "   movl %ecx, 32(%eax)\n\t"

        // load the current seh chain from TIB
        //assume fs:nothing
        "   movl %fs:(0x0), %ecx\n\t"
        //assume fs:error

        "__walkchain:\n\t"

        // if (sehitem.prev == 0xffffffff) (last?) goto __found
        "   movl (%ecx), %edx\n\t"
        "   incl %edx\n\t"
        "   jz __found\n\t"

        // sehitem = sehitem.prev
        "   decl %edx\n\t"
        "   xchgl %ecx, %edx\n\t"
        "   jmp __walkchain\n\t"

        "__found:\n\t"

        // context.seh.handler = sehitem.handler
        "   movl 4(%ecx), %ecx\n\t"
        "   movl %ecx, 60(%eax)\n\t"

        // context.seh.prev = 0xffffffff
        "   movl $0xffffffff, %ecx\n\t"
        "   movl %ecx, 56(%eax)\n\t"

        // context.seh = the address of context.seh.prev
        "   leal 56(%eax), %ecx\n\t"
        "   movl %ecx, 16(%eax)\n\t"

        // return pointer to context-data
        "   ret\n\t"

        "__entry:\n\t"

        // pass old-context(context: eax, data: edx) arguments to the context function
        "   movl %eax, (%esp)\n\t"
        "   movl %edx, 4(%esp)\n\t"

        // patch return address: __end
        "   pushl %ebp\n\t"

        // jump to the context function entry(eip)
        "   jmp *%ebx\n\t"

        "__end:\n\t"

        // exit(0)
        "   xorl %eax, %eax\n\t"
        "   movl %eax, (%esp)\n\t"
        "   call __exit\n\t"
        "   hlt\n\t"

        ".def   _exit;  .scl    2;  .type   32; .endef\n\t" /* standard C library function */
        ".section .drectve\n\t"
        ".ascii \" -export:\\\"sky_context_make\\\"\"\n\t"
        );

asm(
        ".text\n\t"
        ".p2align 4,,15\n\t"
        /* mark as using no unregistered SEH handlers */
        ".globl	@feat.00\n\t"
        ".def	@feat.00;	.scl	3;	.type	0;	.endef\n\t"
        ".set   @feat.00,   1\n\t"
        ".globl _sky_context_jump\n\t"
        ".def _sky_context_jump; .scl    2;  .type 32;   .endef\n\t"
        "_sky_context_jump:\n\t"
        // save registers and construct the current context
        "   pushl %ebp\n\t"
        "   pushl %ebx\n\t"
        "   pushl %esi\n\t"
        "   pushl %edi\n\t"

        // load TIB to edx
        //assume fs:nothing
        "   movl %fs:(0x18), %edx\n\t"
        //assume fs:error

        // load and save current seh exception list
        "   movl (%edx), %eax\n\t"
        "   pushl %eax\n\t"

        // load and save current stack base
        "   movl 0x04(%edx), %eax\n\t"
        "   pushl %eax\n\t"

        // load and save current stack limit
        "   movl 0x08(%edx), %eax\n\t"
        "   pushl %eax\n\t"

        // load and save current deallocation stack
        "   movl 0xe0c(%edx), %eax\n\t"
        "   pushl %eax\n\t"

        // load and save fiber local storage
        "   movl 0x10(%edx), %eax\n\t"
        "   pushl %eax\n\t"

        // save the old context(esp) to eax
        "   movl %esp, %eax\n\t"

        // switch to the new context(esp) and stack
        "   movl 40(%esp), %ecx\n\t"
        "   movl %ecx, %esp\n\t"

        // load TIB to edx
        //assume fs:nothing
        "   movl %fs:(0x18), %edx\n\t"
        //assume fs:error

        // restore fiber local storage (context.fiber)
        "   popl %ecx\n\t"
        "   movl %ecx, 0x10(%edx)\n\t"

        // restore current deallocation stack (context.dealloc)
        "   popl %ecx\n\t"
        "   movl %ecx, 0xe0c(%edx)\n\t"

        // restore current stack limit (context.limit)
        "   popl %ecx\n\t"
        "   movl %ecx, 0x08(%edx)\n\t"

        // restore current stack base (context.base)
        "   popl %ecx\n\t"
        "   movl %ecx, 0x04(%edx)\n\t"

        // restore current seh exception list (context.seh)
        "   popl %ecx\n\t"
        "   movl %ecx, (%edx)\n\t"

        // restore registers of the new context
        "   popl %edi\n\t"
        "   popl %esi\n\t"
        "   popl %ebx\n\t"
        "   popl %ebp\n\t"

        // restore the return or function address(ecx)
        "   popl %ecx\n\t"

        // return from-context(context: eax, data: edx) from jump
        // edx = [eax + 44] = [esp_jump + 44] = jump.argument(data)
        "   movl 44(%eax), %edx\n\t"

        // jump to the return or function address(eip)
        "   jmp *%ecx\n\t"

        ".section .drectve\n\t"
        ".ascii \" -export:\\\"sky_context_jump\\\"\"\n\t"
        );

#endif