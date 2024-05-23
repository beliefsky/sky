//
// Created by weijing on 2024/5/23.
//
//
// Created by weijing on 2024/5/23.
//

#if defined(__x86_64__) && defined(__APPLE__)

#include "./context_common.h"

#define USE_TSX
#undef CONTEXT_TLS_STACK_PROTECTOR

asm(
".text\n\t"
".globl _sky_context_make\n\t"
".align 8\n\t"
"_sky_context_make:\n\t"
/* first arg of make_fcontext() == top of context-stack */
"   movq  %rdi, %rax\n\t"

/* shift address in RAX to lower 16 byte boundary */
"   andq  $-16, %rax\n\t"

/* reserve space for context-data on context-stack */
/* on context-function entry: (RSP -0x8) % 16 == 0 */
"   leaq  -0x40(%rax), %rax\n\t"

/* third arg of sky_context_make() == address of context-function */
/* stored in RBX */
"   movq  %rdx, 0x28(%rax)\n\t"

/* save MMX control- and status-word */
"   stmxcsr  (%rax)\n\t"
/* save x87 control-word */
"   fnstcw   0x4(%rax)\n\t"

/* compute abs address of label trampoline */
"   leaq  trampoline(%rip), %rcx\n\t"
/* save address of trampoline as return-address for context-function */
/* will be entered after calling sky_context_make() first time */
"   movq  %rcx, 0x38(%rax)\n\t"

/* compute abs address of label finish */
"   leaq  finish(%rip), %rcx\n\t"
/* save address of finish as return-address for context-function */
/* will be entered after context-function returns */
"   movq  %rcx, 0x30(%rax)\n\t"

"   ret\t\n" /* return pointer to context-data */

"trampoline:\n\t"
/* store return address on stack */
/* fix stack alignment */
"   push %rbp\n\t"
/* jump to context-function */
"   jmp *%rbx\n\t"

"finish:\n\t"
/* exit code is zero */
"   xorq  %rdi, %rdi\n\t"
/* exit application */
"   call  _exit@PLT\n\t"
"   hlt\n\t"
);

asm(
".text\n\t"
".globl _sky_context_jump\n\t"
".align 8\n\t"
"_sky_context_jump:\n\t"
"   leaq  -0x38(%rsp), %rsp\n\t" /* prepare stack */

#ifndef USE_TSX
"   stmxcsr  (%rsp)\n\t" /* save MMX control- and status-word */
"   fnstcw   0x4(%rsp)\n\t" /* save x87 control-word */
#endif

"   movq  %r12, 0x8(%rsp)\n\t" /* save R12 */
"   movq  %r13, 0x10(%rsp)\n\t" /* save R13 */
"   movq  %r14, 0x18(%rsp)\n\t" /* save R14 */
"   movq  %r15, 0x20(%rsp)\n\t" /* save R15 */
"   movq  %rbx, 0x28(%rsp)\n\t" /* save RBX */
"   movq  %rbp, 0x30(%rsp)\n\t" /* save RBP */

/* store RSP (pointing to context-data) in RAX */
"   movq  %rsp, %rax\n\t"

/* restore RSP (pointing to context-data) from RDI */
"   movq  %rdi, %rsp\n\t"

"   movq  0x38(%rsp), %r8\n\t" /* restore return-address */

#ifndef USE_TSX
"   ldmxcsr  (%rsp)\n\t" /* restore MMX control- and status-word */
"   fldcw    0x4(%rsp)\n\t" /* restore x87 control-word */
#endif

"   movq  0x8(%rsp), %r12\n\t" /* restore R12 */
"   movq  0x10(%rsp), %r13\n\t" /* restore R13 */
"   movq  0x18(%rsp), %r14\n\t" /* restore R14 */
"   movq  0x20(%rsp), %r15\n\t" /* restore R15 */
"   movq  0x28(%rsp), %rbx\n\t" /* restore RBX */
"   movq  0x30(%rsp), %rbp\n\t" /* restore RBP */

"   leaq  0x40(%rsp), %rsp\n\t" /* prepare stack */

/* return transfer_t from jump */
/* RAX == fctx, RDX == data */
"   movq  %rsi, %rdx\n\t"
/* RDI == fctx, RSI == data */
"   movq  %rax, %rdi\n\t"

/* indirect jump to context */
"   jmp  *%r8\n\t"
);

asm(
".text\n\t"
".globl _sky_context_ontop\n\t"
".align 8\n\t"
"_sky_context_ontop:\n\t"
/* preserve ontop-function in R8 */
"   movq  %rdx, %r8\n\t"

"   leaq  -0x38(%rsp), %rsp\n\t" /* prepare stack */

#ifndef USE_TSX
"   stmxcsr  (%rsp)\n\t" /* save MMX control- and status-word */
"   fnstcw   0x4(%rsp)\n\t" /* save x87 control-word */
#endif

#ifdef CONTEXT_TLS_STACK_PROTECTOR
"   movq  %fs:0x28, %rcx\n\t" /* read stack guard from TLS record */
"   movq  %rcx, 0x8(%rsp)\n\t" /* save stack guard */
#endif

"   movq  %r12, 0x8(%rsp)\n\t" /* save R12 */
"   movq  %r13, 0x10(%rsp)\n\t" /* save R13 */
"   movq  %r14, 0x18(%rsp)\n\t" /* save R14 */
"   movq  %r15, 0x20(%rsp)\n\t" /* save R15 */
"   movq  %rbx, 0x28(%rsp)\n\t" /* save RBX */
"   movq  %rbp, 0x30(%rsp)\n\t" /* save RBP */

/* store RSP (pointing to context-data) in RAX */
"   movq  %rsp, %rax\n\t"

/* restore RSP (pointing to context-data) from RDI */
"   movq  %rdi, %rsp\n\t"

#ifndef USE_TSX
"   ldmxcsr  (%rsp)\n\t" /* restore MMX control- and status-word */
"   fldcw    0x4(%rsp)\n\t" /* restore x87 control-word */
#endif

#ifdef CONTEXT_TLS_STACK_PROTECTOR
"   movq  0x8(%rsp), %rdx\n\t"  /* load stack guard */
"   movq  %rdx, %fs:0x28\n\t"   /* restore stack guard to TLS record */
#endif

"   movq  0x8(%rsp), %r12\n\t" /* restore R12 */
"   movq  0x10(%rsp), %r13\n\t" /* restore R13 */
"   movq  0x18(%rsp), %r14\n\t" /* restore R14 */
"   movq  0x20(%rsp), %r15\n\t" /* restore R15 */
"   movq  0x28(%rsp), %rbx\n\t" /* restore RBX */
"   movq  0x30(%rsp), %rbp\n\t" /* restore RBP */

"   leaq  0x38(%rsp), %rsp\n\t" /* prepare stack */

/* return transfer_t from jump */
/* RAX == fctx, RDX == data */
"   movq  %rsi, %rdx\n\t"
/* RDI == fctx, RSI == data */
"   movq  %rax, %rdi\n\t"

/* keep return-address on stack */

/* indirect jump to context */
"   jmp  *%r8\n\t"
);

#endif

