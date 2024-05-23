//
// Created by weijing on 2024/5/23.
//
//
// Created by weijing on 2024/5/23.
//

#if defined(__i386__) && defined(__APPLE__)

#include "./context_common.h"

#define USE_TSX

asm(
".text\n\t"
".globl _sky_context_make\n\t"
".align 2\n\t"
"_sky_context_make:\n\t"
/* first arg of sky_context_make()  == top of context-stack */
"   movl  0x4(%esp), %eax\n\t"

/* reserve space for first argument of context-function
   eax might already point to a 16byte border */
"   leal  -0x8(%eax), %eax\n\t"

/* shift address in EAX to lower 16 byte boundary */
"   andl  $-16, %eax\n\t"

/* reserve space for context-data on context-stack, and align the stack */
"   leal  -0x34(%eax), %eax\n\t"

/* third arg of sky_context_make()  == address of context-function */
/* stored in EBX */
"   movl  0xc(%esp), %ecx\n\t"
"   movl  %ecx, 0x10(%eax)\n\t"

/* save MMX control- and status-word */
"   stmxcsr (%eax)\n\t"
/* save x87 control-word */
"   fnstcw  0x4(%eax)\n\t"

/* compute abs address of label trampoline */
"   call  1f\n\t"
/* address of trampoline 1 */
"   1:  popl  %ecx\n\t"
/* compute abs address of label trampoline */
"   addl  $trampoline-1b, %ecx\n\t"
/* save address of trampoline as return address */
/* will be entered after calling sky_context_jump()  first time */
"   movl  %ecx, 0x18(%eax)\n\t"

/* compute abs address of label finish */
"   call  2f\n\t"
/* address of label 2 */
"   2:  popl  %ecx\n\t"
/* compute abs address of label finish */
"   addl  $finish-2b, %ecx\n\t"
/* save address of finish as return-address for context-function */
/* will be entered after context-function returns */
"   movl  %ecx, 0x14(%eax)\n\t"

"   ret\n\t" /* return pointer to context-data */

"trampoline:\n\t"
/* move transport_t for entering context-function */
"   movl  %eax, (%esp)\n\t"
"   movl  %edx, 0x4(%esp)\n\t"
"   pushl %ebp\n\t"
/* jump to context-function */
"   jmp *%ebx\n\t"

"finish:\n\t"
/* exit code is zero */
"   xorl  %eax, %eax\n\t"
"   movl  %eax, (%esp)\n\t"
/* exit application */
"   call  __exit\n\t"
"   hlt\n\t"
);

asm(
".text\n\t"
".globl _sky_context_jump\n\t"
".align 2\n\t"
"_sky_context_jump:\n\t"
"   leal  -0x18(%esp), %esp\n\t"  /* prepare stack */

#ifndef USE_TSX
"   stmxcsr  (%esp)\n\t"     /* save MMX control- and status-word */
"   fnstcw   0x4(%esp)\n\t"  /* save x87 control-word */
#endif

"   movl  %edi, 0x8(%esp)\n\t"  /* save EDI */
"   movl  %esi, 0xc(%esp)\n\t"  /* save ESI */
"   movl  %ebx, 0x10(%esp)\n\t"  /* save EBX */
"   movl  %ebp, 0x14(%esp)\n\t"  /* save EBP */

/* store ESP (pointing to context-data) in ECX */
"   movl  %esp, %ecx\n\t"

/* first arg of sky_context_jump()  == sky_context to jump to */
"   movl  0x1c(%esp), %eax\n\t"

/* second arg of sky_context_jump()  == data to be transferred */
"   movl  0x20(%esp), %edx\n\t"

/* restore ESP (pointing to context-data) from EAX */
"   movl  %eax, %esp\n\t"

/* return parent sky_context_t */
"   movl  %ecx, %eax\n\t"
/* returned data is stored in EDX */

"   movl  0x18(%esp), %ecx\n\t"  /* restore EIP */

#ifndef USE_TSX
"   ldmxcsr  (%esp)\n\t"     /* restore MMX control- and status-word */
"   fldcw    0x4(%esp)\n\t"  /* restore x87 control-word */
#endif

"   movl  0x8(%esp), %edi\n\t"  /* restore EDI */
"   movl  0xc(%esp), %esi\n\t"  /* restore ESI */
"   movl  0x10(%esp), %ebx\n\t"  /* restore EBX */
"   movl  0x14(%esp), %ebp\n\t"  /* restore EBP */

"   leal  0x1c(%esp), %esp\n\t"  /* prepare stack */

/* jump to context */
"   jmp *%ecx\n\t"
);

asm(
".text\n\t"
".globl _sky_context_ontop\n\t"
".align 2\n\t"
"_sky_context_ontop:\n\t"
"   leal  -0x18(%esp), %esp\n\t"  /* prepare stack */

#ifndef USE_TSX
"   stmxcsr  (%esp)\n\t"     /* save MMX control- and status-word */
"   fnstcw   0x4(%esp)\n\t"  /* save x87 control-word */
#endif

"   movl  %edi, 0x8(%esp)\n\t"  /* save EDI */
"   movl  %esi, 0xc(%esp)\n\t"  /* save ESI */
"   movl  %ebx, 0x10(%esp)\n\t"  /* save EBX */
"   movl  %ebp, 0x14(%esp)\n\t"  /* save EBP */

/* store ESP (pointing to context-data) in ECX */
"   movl  %esp, %ecx\n\t"

/* first arg of sky_context_ontop()  == sky_context to jump to */
"   movl  0x1c(%esp), %eax\n\t"

/* pass parent sky_context_t */
"   movl  %ecx, 0x1c(%eax)\n\t"

/* second arg of sky_context_ontop()  == data to be transferred */
"   movl  0x20(%esp), %ecx\n\t"

/* pass data */
"   movl %ecx, 0x20(%eax)\n\t"

/* third arg of sky_context_ontop()  == ontop-function */
"   movl  0x24(%esp), %ecx\n\t"

/* restore ESP (pointing to context-data) from EAX */
"   movl  %eax, %esp\n\t"

/* return parent sky_context_t */
"   movl  %ecx, %eax\n\t"
/* returned data is stored in EDX */

#ifndef USE_TSX
"   ldmxcsr  (%esp)\n\t"     /* restore MMX control- and status-word */
"   fldcw    0x4(%esp)\n\t"  /* restore x87 control-word */
#endif

"   movl  0x8(%esp), %edi\n\t"  /* restore EDI */
"   movl  0xc(%esp), %esi\n\t" /* restore ESI */
"   movl  0x10(%esp), %ebx\n\t"  /* restore EBX */
"   movl  0x14(%esp), %ebp\n\t"  /* restore EBP */

"   leal  0x18(%esp), %esp\n\t"  /* prepare stack */

/* jump to context */
"   jmp *%ecx\n\t"
);

#endif

