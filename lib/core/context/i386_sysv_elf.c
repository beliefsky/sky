//
// Created by weijing on 2024/5/23.
//

#if defined(__i386__) && defined(__ELF__)

#include "./context_common.h"

#define USE_TSX
#undef  CONTEXT_TLS_STACK_PROTECTOR

# ifdef __CET__
#include <cet.h>
#else
#  define _CET_ENDBR
#endif

asm(
".text\n\t"
".globl sky_context_make\n\t"
".align 2\n\t"
".type sky_context_make,@function\n\t"
"sky_context_make:\n\t"
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
"   movl  %ecx, 0x14(%eax)\n\t"

/* save MMX control- and status-word */
"   stmxcsr (%eax)\n\t"
/* save x87 control-word */
"   fnstcw  0x4(%eax)\n\t"

#if defined(CONTEXT_TLS_STACK_PROTECTOR)
/* save stack guard */
"   movl  %gs:0x14, %ecx\n\t"    /* read stack guard from TLS record */
"   movl  %ecx, 0x8(%eax)\n\t"   /* save stack guard */
#endif

/* return transport_t */
/* FCTX == EDI, DATA == ESI */
"   leal  0xc(%eax), %ecx\n\t"
"   movl  %ecx, 0x20(%eax)\n\t"

/* compute abs address of label trampoline */
"   call  1f\n\t"
/* address of trampoline 1 */
"1:  popl  %ecx\n\t"
/* compute abs address of label trampoline */
"   addl  $trampoline-1b, %ecx\n\t"
/* save address of trampoline as return address */
/* will be entered after calling sky_context_jump()  first time */
"   movl  %ecx, 0x1c(%eax)\n\t"

/* compute abs address of label finish */
"   call  2f\n\t"
/* address of label 2 */
"2:  popl  %ecx\n\t"
/* compute abs address of label finish */
"   addl  $finish-2b, %ecx\n\t"
/* save address of finish as return-address for context-function */
/* will be entered after context-function returns */
"   movl  %ecx, 0x18(%eax)\n\t"

"   ret\n\t" /* return pointer to context-data */

"trampoline:\n\t"
/* move transport_t for entering context-function */
"   movl  %edi, (%esp)\n\t"
"   movl  %esi, 0x4(%esp)\n\t"
"   pushl %ebp\n\t"
/* jump to context-function */
"   jmp *%ebx\n\t"

"finish:\n\t"
"   call  3f\n\t"
/* address of label 3 */
"3:  popl  %ebx\n\t"
/* compute address of GOT and store it in EBX */
"   addl  $_GLOBAL_OFFSET_TABLE_+[.-3b], %ebx\n\t"

/* exit code is zero */
"   xorl  %eax, %eax\n\t"
"   movl  %eax, (%esp)\n\t"
/* exit application */
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
"   leal  -0x1c(%esp), %esp\n\t"  /* prepare stack */

#ifndef USE_TSX
"   stmxcsr  (%esp)\n\t"     /* save MMX control- and status-word */
"   fnstcw   0x4(%esp)\n\t"  /* save x87 control-word */
#endif

#ifdef CONTEXT_TLS_STACK_PROTECTOR
"   movl  %gs:0x14, %ecx\n\t"    /* read stack guard from TLS record */
"   movl  %ecx, 0x8(%esp)\n\t"   /* save stack guard */
#endif

"   movl  %edi, 0xc(%esp)\n\t"   /* save EDI */
"   movl  %esi, 0x10(%esp)\n\t"  /* save ESI */
"   movl  %ebx, 0x14(%esp)\n\t"  /* save EBX */
"   movl  %ebp, 0x18(%esp)\n\t"  /* save EBP */

/* store ESP (pointing to context-data) in ECX */
"   movl  %esp, %ecx\n\t"

/* first arg of sky_context_jump()  == sky_context to jump to */
"   movl  0x24(%esp), %eax\n\t"

/* second arg of sky_context_jump()  == data to be transferred */
"   movl  0x28(%esp), %edx\n\t"

/* restore ESP (pointing to context-data) from EAX */
"   movl  %eax, %esp\n\t"

/* address of returned transport_t */
"   movl 0x20(%esp), %eax\n\t"
/* return parent sky_context_t */
"   movl  %ecx, (%eax)\n\t"
/* return data */
"   movl %edx, 0x4(%eax)\n\t"

"   movl  0x1c(%esp), %ecx\n\t"  /* restore EIP */

#ifndef USE_TSX
"   ldmxcsr  (%esp)\n\t"     /* restore MMX control- and status-word */
"   fldcw    0x4(%esp)\n\t"  /* restore x87 control-word */
#endif

#ifdef CONTEXT_TLS_STACK_PROTECTOR
"   movl  0x8(%esp), %edx\n\t"  /* load stack guard */
"   movl  %edx, %gs:0x14\n\t"   /* restore stack guard to TLS record */
#endif

"   movl  0xc(%esp), %edi\n\t"  /* restore EDI */
"   movl  0x10(%esp), %esi\n\t"  /* restore ESI */
"   movl  0x14(%esp), %ebx\n\t"  /* restore EBX */
"   movl  0x18(%esp), %ebp\n\t"  /* restore EBP */

"   leal  0x24(%esp), %esp\n\t"  /* prepare stack */

/* jump to context */
"   jmp *%ecx\n\t"

".size sky_context_jump,.-sky_context_jump\n\t"
".section .note.GNU-stack,\"\",%progbits\n\t"
);


asm(
".text\n\t"
".globl sky_context_ontop\n\t"
".align 2\n\t"
".type sky_context_ontop,@function\n\t"
"sky_context_ontop:\n\t"
"   leal  -0x1c(%esp), %esp\n\t"  /* prepare stack */

#ifndef USE_TSX
"   stmxcsr  (%esp)\n\t"     /* save MMX control- and status-word */
"   fnstcw   0x4(%esp)\n\t"  /* save x87 control-word */
#endif

#ifdef CONTEXT_TLS_STACK_PROTECTOR
"   movl  %gs:0x14, %ecx\n\t"    /* read stack guard from TLS record */
"   movl  %ecx, 0x8(%esp)\n\t"   /* save stack guard */
#endif

"   movl  %edi, 0xc(%esp)\n\t"  /* save EDI */
"   movl  %esi, 0x10(%esp)\n\t"  /* save ESI */
"   movl  %ebx, 0x14(%esp)\n\t"  /* save EBX */
"   movl  %ebp, 0x18(%esp)\n\t"  /* save EBP */

/* store ESP (pointing to context-data) in ECX */
"   movl  %esp, %ecx\n\t"

/* first arg of sky_context_ontop()  == sky_context to jump to */
"   movl  0x24(%esp), %eax\n\t"

/* pass parent sky_context_t */
"   movl  %ecx, 0x24(%eax)\n\t"

/* second arg of sky_context_ontop()  == data to be transferred */
"   movl  0x28(%esp), %ecx\n\t"

/* pass data */
"   movl %ecx, 0x28(%eax)\n\t"

/* third arg of sky_context_ontop()  == ontop-function */
"   movl  0x2c(%esp), %ecx\n\t"

/* restore ESP (pointing to context-data) from EAX */
"   movl  %eax, %esp\n\t"

/* address of returned transport_t */
"   movl 0x20(%esp), %eax\n\t"
/* return parent sky_context_t */
"   movl  %ecx, (%eax)\n\t"
/* return data */
"   movl %edx, 0x4(%eax)\n\t"

#ifndef USE_TSX
"   ldmxcsr  (%esp)\n\t"     /* restore MMX control- and status-word */
"   fldcw    0x4(%esp)\n\t"  /* restore x87 control-word */
#endif

#ifdef CONTEXT_TLS_STACK_PROTECTOR
"   movl  0x8(%esp), %edx\n\t"  /* load stack guard */
"   movl  %edx, %gs:0x14\n\t"   /* restore stack guard to TLS record */
#endif

"   movl  0xc(%esp), %edi\n\t"  /* restore EDI */
"   movl  0x10(%esp), %esi\n\t"  /* restore ESI */
"   movl  0x14(%esp), %ebx\n\t"  /* restore EBX */
"   movl  0x18(%esp), %ebp\n\t"  /* restore EBP */

"   leal  0x1c(%esp), %esp\n\t"  /* prepare stack */

/* jump to context */
"   jmp *%ecx\n\t"
".size sky_context_ontop,.-sky_context_ontop\n\t"
".section .note.GNU-stack,\"\",%progbits\n\t"
);

#endif