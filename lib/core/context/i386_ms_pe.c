//
// Created by weijing on 2024/5/23.
//

#if defined(__i386__) && defined(__WINNT__)

#define USE_TSX

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
/* first arg of sky_context_make()  == top of context-stack */
"   movl  0x04(%esp), %eax\n\t"

/* reserve space for first argument of context-function */
/* EAX might already point to a 16byte border */
"   leal  -0x8(%eax), %eax\n\t"

/* shift address in EAX to lower 16 byte boundary */
"   andl  $-16, %eax\n\t"

/* reserve space for context-data on context-stack */
/* size for fc_mxcsr .. EIP + return-address for context-function */
/* on context-function entry: (ESP -0x4) % 8 == 0 */
/* additional space is required for SEH */
"   leal  -0x40(%eax), %eax\n\t"

/* save MMX control- and status-word */
"   stmxcsr  (%eax)\n\t"
/* save x87 control-word */
"   fnstcw  0x4(%eax)\n\t"

/* first arg of sky_context_make()  == top of context-stack */
"   movl  0x4(%esp), %ecx\n\t"
/* save top address of context stack as 'base' */
"   movl  %ecx, 0x14(%eax)\n\t"
/* second arg of sky_context_make()  == size of context-stack */
"   movl  0x8(%esp), %edx\n\t"
/* negate stack size for LEA instruction (== substraction) */
"   negl  %edx\n\t"
/* compute bottom address of context stack (limit) */
"   leal  (%ecx,%edx), %ecx\n\t"
/* save bottom address of context-stack as 'limit' */
"   movl  %ecx, 0x10(%eax)\n\t"
/* save bottom address of context-stack as 'dealloction stack' */
"   movl  %ecx, 0xc(%eax)\n\t"
/* set fiber-storage to zero */
"   xorl  %ecx, %ecx\n\t"
"   movl  %ecx, 0x8(%eax)\n\t"

/* third arg of sky_context_make()  == address of context-function */
/* stored in EBX */
"   movl  0xc(%esp), %ecx\n\t"
"   movl  %ecx, 0x24(%eax)\n\t"

/* compute abs address of label trampoline */
"   movl  $trampoline, %ecx\n\t"
/* save address of trampoline as return-address for context-function */
/* will be entered after calling sky_context_jump()  first time */
"   movl  %ecx, 0x2c(%eax)\n\t"

/* compute abs address of label finish */
"   movl  $finish, %ecx\n\t"
/* save address of finish as return-address for context-function */
/* will be entered after context-function returns */
"   movl  %ecx, 0x28(%eax)\n\t"

/* traverse current seh chain to get the last exception handler installed by Windows */
/* note that on Windows Server 2008 and 2008 R2, SEHOP is activated by default */
/* the exception handler chain is tested for the presence of ntdll.dll!FinalExceptionHandler */
/* at its end by RaiseException all seh andlers are disregarded if not present and the */
/* program is aborted */
/* load NT_TIB into ECX */
"   movl  %fs:(0x0), %ecx\n\t"

"walk:\n\t"
/* load 'next' member of current SEH into EDX */
"   movl  (%ecx), %edx\n\t"
/* test if 'next' of current SEH is last (== 0xffffffff) */
"   incl  %edx\n\t"
"   jz  found\n\t"
"   decl  %edx\n\t"
/* exchange content; ECX contains address of next SEH */
"   xchgl  %ecx, %edx\n\t"
/* inspect next SEH */
"   jmp  walk\n\t"

"found:\n\t"
/* load 'handler' member of SEH == address of last SEH handler installed by Windows */
"   movl  0x04(%ecx), %ecx\n\t"
/* save address in ECX as SEH handler for context */
"   movl  %ecx, 0x3c(%eax)\n\t"
/* set ECX to -1 */
"   movl  $0xffffffff, %ecx\n\t"
/* save ECX as next SEH item */
"   movl  %ecx, 0x38(%eax)\n\t"
/* load address of next SEH item */
"   leal  0x38(%eax), %ecx\n\t"
/* save next SEH */
"   movl  %ecx, 0x18(%eax)\n\t"

/* return pointer to context-data */
"   ret\n\t"

"trampoline:\n\t"
/* move transport_t for entering context-function */
/* FCTX == EAX, DATA == EDX */
"   movl  %eax, (%esp)\n\t"
"   movl  %edx, 0x4(%esp)\n\t"
/* label finish as return-address */
"   pushl %ebp\n\t"
/* jump to context-function */
"   jmp  *%ebx\n\t"

"   finish:\n\t"
/* ESP points to same address as ESP on entry of context function + 0x4 */
"   xorl  %eax, %eax\n\t"
/* exit code is zero */
"   movl  %eax, (%esp)\n\t"
/* exit application */
"   call  __exit\n\t"
"   hlt\n\t"

".def   _exit;  .scl    2;  .type   32; .endef\n\t" /* standard C library function */
".section .drectve\n\t"
".ascii \" -export:\\\"_sky_context_make\\\"\"\n\t"
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
/* prepare stack */
"   leal  -0x2c(%esp), %esp\n\t"

#ifndef USE_TSX
/* save MMX control- and status-word */
"   stmxcsr  (%esp)\n\t"
/* save x87 control-word */
"   fnstcw  0x4(%esp)\n\t"
#endif

/* load NT_TIB */
"   movl  %fs:(0x18), %edx\n\t"
/* load fiber local storage */
"   movl  0x10(%edx), %eax\n\t"
"   movl  %eax, 0x8(%esp)\n\t"
/* load current dealloction stack */
"   movl  0xe0c(%edx), %eax\n\t"
"   movl  %eax, 0xc(%esp)\n\t"
/* load current stack limit */
"   movl  0x8(%edx), %eax\n\t"
"   movl  %eax, 0x10(%esp)\n\t"
/* load current stack base */
"   movl  0x4(%edx), %eax\n\t"
"   movl  %eax, 0x14(%esp)\n\t"
/* load current SEH exception list */
"   movl  (%edx), %eax\n\t"
"   movl  %eax, 0x18(%esp)\n\t"

"   movl  %edi, 0x1c(%esp)\n\t"  /* save EDI */
"   movl  %esi, 0x20(%esp)\n\t"  /* save ESI */
"   movl  %ebx, 0x24(%esp)\n\t"  /* save EBX */
"   movl  %ebp, 0x28(%esp)\n\t"  /* save EBP */

/* store ESP (pointing to context-data) in EAX */
"   movl  %esp, %eax\n\t"

/* firstarg of sky_context_jump()  == sky_context to jump to */
"   movl  0x30(%esp), %ecx\n\t"

/* restore ESP (pointing to context-data) from ECX */
"   movl  %ecx, %esp\n\t"

#ifndef USE_TSX
/* restore MMX control- and status-word */
"   ldmxcsr  (%esp)\n\t"
/* restore x87 control-word */
"   fldcw  0x4(%esp)\n\t"
#endif

/* restore NT_TIB into EDX */
"   movl  %fs:(0x18), %edx\n\t"
/* restore fiber local storage */
"   movl  0x8(%esp), %ecx\n\t"
"   movl  %ecx, 0x10(%edx)\n\t"
/* restore current deallocation stack */
"   movl  0xc(%esp), %ecx\n\t"
"   movl  %ecx, 0xe0c(%edx)\n\t"
/* restore current stack limit */
"   movl  0x10(%esp), %ecx\n\t"
"   movl  %ecx, 0x8(%edx)\n\t"
/* restore current stack base */
"   movl  0x14(%esp), %ecx\n\t"
"   movl  %ecx, 0x4(%edx)\n\t"
/* restore current SEH exception list */
"   movl  0x18(%esp), %ecx\n\t"
"   movl  %ecx, (%edx)\n\t"

"   movl  0x2c(%esp), %ecx\n\t"  /* restore EIP */

"   movl  0x1c(%esp), %edi\n\t"  /* restore EDI */
"   movl  0x20(%esp), %esi\n\t"  /* restore ESI */
"   movl  0x24(%esp), %ebx\n\t"  /* restore EBX */
"   movl  0x28(%esp), %ebp\n\t"  /* restore EBP */

/* prepare stack */
"   leal  0x30(%esp), %esp\n\t"

/* return transfer_t */
/* FCTX == EAX, DATA == EDX */
"   movl  0x34(%eax), %edx\n\t"

/* jump to context */
"   jmp *%ecx\n\t"

".section .drectve\n\t"
".ascii \" -export:\\\"_sky_context_jump\\\"\"\n\t"
);

asm(
".text\n\t"
".p2align 4,,15\n\t"
/* mark as using no unregistered SEH handlers */
".globl	@feat.00\n\t"
".def	@feat.00;	.scl	3;	.type	0;	.endef\n\t"
".set   @feat.00,   1\n\t"
".globl _sky_context_ontop\n\t"
".def _sky_context_ontop; .scl    2;  .type 32;   .endef\n\t"
"_sky_context_ontop:\n\t"
/* prepare stack */
"   leal  -0x2c(%esp), %esp

#ifndef USE_TSX
/* save MMX control- and status-word */
"   stmxcsr  (%esp)
/* save x87 control-word */
"   fnstcw  0x4(%esp)
#endif

/* load NT_TIB */
"   movl  %fs:(0x18), %edx\n\t"
/* load fiber local storage */
"   movl  0x10(%edx), %eax\n\t"
"   movl  %eax, 0x8(%esp)\n\t"
/* load current dealloction stack */
"   movl  0xe0c(%edx), %eax\n\t"
"   movl  %eax, 0xc(%esp)\n\t"
/* load current stack limit */
"   movl  0x8(%edx), %eax\n\t"
"   movl  %eax, 0x10(%esp)\n\t"
/* load current stack base */
"   movl  0x4(%edx), %eax\n\t"
"   movl  %eax, 0x14(%esp)\n\t"
/* load current SEH exception list */
"   movl  (%edx), %eax\n\t"
"   movl  %eax, 0x18(%esp)\n\t"

"   movl  %edi, 0x1c(%esp)\n\t"  /* save EDI */
"   movl  %esi, 0x20(%esp)\n\t"  /* save ESI */
"   movl  %ebx, 0x24(%esp)\n\t"  /* save EBX */
"   movl  %ebp, 0x28(%esp)\n\t"  /* save EBP */

/* store ESP (pointing to context-data) in ECX */
"   movl  %esp, %ecx\n\t"

/* first arg of sky_context_ontop()  == sky_context to jump to */
"   movl  0x30(%esp), %eax\n\t"

/* pass parent sky_context_t */
"   movl  %ecx, 0x30(%eax)\n\t"

/* second arg of sky_context_ontop()  == data to be transferred */
"   movl  0x34(%esp), %ecx\n\t"

/* pass data */
"   movl  %ecx, 0x34(%eax)\n\t"

/* third arg of sky_context_ontop()  == ontop-function */
"   movl  0x38(%esp), %ecx\n\t"

/* restore ESP (pointing to context-data) from EDX */
"   movl  %eax, %esp\n\t"

#ifndef USE_TSX
/* restore MMX control- and status-word */
"   ldmxcsr  (%esp)\n\t"
/* restore x87 control-word */
"   fldcw  0x4(%esp)\n\t"
#endif

/* restore NT_TIB into EDX */
"   movl  %fs:(0x18), %edx\n\t"
/* restore fiber local storage */
"   movl  0x8(%esp), %eax\n\t"
"   movl  %eax, 0x10(%edx)\n\t"
/* restore current deallocation stack */
"   movl  0xc(%esp), %eax\n\t"
"   movl  %eax, 0xe0c(%edx)\n\t"
/* restore current stack limit */
"   movl  0x10(%esp), %eax\n\t"
"   movl  %eax, 0x08(%edx)\n\t"
/* restore current stack base */
"   movl  0x14(%esp), %eax\n\t"
"   movl  %eax, 0x04(%edx)\n\t"
/* restore current SEH exception list */
"   movl  0x18(%esp), %eax\n\t"
"   movl  %eax, (%edx)\n\t"

"   movl  0x1c(%esp), %edi\n\t"  /* restore EDI */
"   movl  0x20(%esp), %esi\n\t"  /* restore ESI */
"   movl  0x24(%esp), %ebx\n\t"  /* restore EBX */
"   movl  0x28(%esp), %ebp\n\t"  /* restore EBP */

/* prepare stack */
"   leal  0x2c(%esp), %esp\n\t"

/* keep return-address on stack */

/* jump to context */
"   jmp  *%ecx\n\t"

".section .drectve\n\t"
".ascii \" -export:\\\"_sky_context_ontop\\\"\"\n\t"
);

#endif