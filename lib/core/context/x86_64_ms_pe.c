//
// Created by weijing on 2024/5/23.
//

#if defined(__x86_64__) && defined(__WINNT__)

#include "./context_common.h"

#define USE_TSX

asm(
".text\n\t"
".p2align 4,,15\n\t"
".globl sky_context_make\n\t"
".def sky_context_make;\t.scl\t2;\t.type 32;\t.endef\n\t"
".seh_proc\tsky_context_make\t\n"
"sky_context_make:\n\t"
".seh_endprologue\n\t"

/* first arg of make_fcontext() == top of context-stack */
"   movq  %rcx, %rax\t\n"

/* shift address in RAX to lower 16 byte boundary */
/* == pointer to context_t and address of context stack */
"   andq  $-16, %rax\t\n"

/* reserve space for context-data on context-stack */
/* on context-function entry: (RSP -0x8) % 16 == 0 */
"   leaq  -0x150(%rax), %rax\t\n"

/* third arg of context_make() == address of context-function */
"   movq  %r8, 0x100(%rax)\t\n"

/* first arg of context_make() == top of context-stack */
/* save top address of context stack as 'base' */
"   movq  %rcx, 0xc8(%rax)\t\n"
/* second arg of context_make() == size of context-stack */
/* negate stack size for LEA instruction (== substraction) */
"   negq  %rdx\t\n"
/* compute bottom address of context stack (limit) */
"   leaq  (%rcx,%rdx), %rcx\t\n"
/* save bottom address of context stack as 'limit' */
"   movq  %rcx, 0xc0(%rax)\t\n"
/* save address of context stack limit as 'dealloction stack' */
"   movq  %rcx, 0xb8(%rax)\t\n"
/* set fiber-storage to zero */
"   xorq  %rcx, %rcx\t\n"
"   movq  %rcx, 0xb0(%rax)\t\n"

/* save MMX control- and status-word */
"   stmxcsr  0xa0(%rax)\t\n"
/* save x87 control-word */
"   fnstcw   0xa4(%rax)\t\n"

/* compute address of transport_t */
"   leaq  0x140(%rax), %rcx\t\n"
/* store address of transport_t in hidden field */
"   movq %rcx, 0x110(%rax)\t\n"

/* compute abs address of label trampoline */
"   leaq  trampoline(%rip), %rcx\t\n"
/* save address of finish as return-address for context-function */
/* will be entered after jump_fcontext() first time */
"   movq  %rcx, 0x118(%rax)\t\n"

/* compute abs address of label finish */
"   leaq  finish(%rip), %rcx\t\n"
/* save address of finish as return-address for context-function */
/* will be entered after context-function returns */
"   movq  %rcx, 0x108(%rax)\t\n"

"   ret\n\t" /* return pointer to context-data */

"trampoline:\t\n"
/* store return address on stack */
/* fix stack alignment */
"   pushq %rbp\t\n"
/* jump to context-function */
"   jmp *%rbx\t\n"

"finish:\t\n"
/* 32byte shadow-space for _exit() */
"   andq  $-32, %rsp\t\n"
/* 32byte shadow-space for _exit() are */
/* already reserved by context_make() */
/* exit code is zero */
"   xorq  %rcx, %rcx\t\n"
/* exit application */
"   call  _exit\t\n"
"   hlt\t\n"
".seh_endproc\n\t"
".def\t_exit;\t.scl\t2;\t.type\t32;\t.endef\n\t" /* standard C library function */
".section .drectve\n\t"
".ascii \" -export:\\\"sky_context_make\\\"\"\n\t"
);

asm(
".text\n\t"
".p2align 4,,15\n\t"
".globl sky_context_jump\n\t"
".def sky_context_jump;\t.scl\t2;\t.type\t32;\t.endef\n\t"
".seh_proc\tsky_context_jump\t\n"
"sky_context_jump:\n\t"
".seh_endprologue\n\t"

"   leaq  -0x118(%rsp), %rsp\n\t" /* prepare stack */

#ifndef USE_TSX
/* save XMM storage */
"   movaps  %xmm6, 0x0(%rsp)\n\t"
"   movaps  %xmm7, 0x10(%rsp)\n\t"
"   movaps  %xmm8, 0x20(%rsp)\n\t"
"   movaps  %xmm9, 0x30(%rsp)\n\t"
"   movaps  %xmm10, 0x40(%rsp)\n\t"
"   movaps  %xmm11, 0x50(%rsp)\n\t"
"   movaps  %xmm12, 0x60(%rsp)\n\t"
"   movaps  %xmm13, 0x70(%rsp)\n\t"
"   movaps  %xmm14, 0x80(%rsp)\n\t"
"   movaps  %xmm15, 0x90(%rsp)\n\t"
"   stmxcsr  0xa0(%rsp)\n\t"  /* save MMX control- and status-word */
"   fnstcw   0xa4(%rsp)\n\t"  /* save x87 control-word */
#endif

/* load NT_TIB */
"   movq  %gs:(0x30), %r10\n\t"
/* save fiber local storage */
"   movq  0x20(%r10), %rax\n\t"
"   movq  %rax, 0xb0(%rsp)\n\t"
/* save current deallocation stack */
"   movq  0x1478(%r10), %rax\n\t"
"   movq  %rax, 0xb8(%rsp)\n\t"
/* save current stack limit */
"   movq  0x10(%r10), %rax\n\t"
"   movq  %rax, 0xc0(%rsp)\n\t"
/* save current stack base */
"   movq  0x08(%r10), %rax\n\t"
"   movq  %rax, 0xc8(%rsp)\n\t"

"   movq  %r12, 0xd0(%rsp)\n\t"  /* save R12 */
"   movq  %r13, 0xd8(%rsp)\n\t"  /* save R13 */
"   movq  %r14, 0xe0(%rsp)\n\t"  /* save R14 */
"   movq  %r15, 0xe8(%rsp)\n\t"  /* save R15 */
"   movq  %rdi, 0xf0(%rsp)\n\t"  /* save RDI */
"   movq  %rsi, 0xf8(%rsp)\n\t"  /* save RSI */
"   movq  %rbx, 0x100(%rsp)\n\t"  /* save RBX */
"   movq  %rbp, 0x108(%rsp)\n\t"  /* save RBP */

"   movq  %rcx, 0x110(%rsp)\n\t"  /* save hidden address of transport_t */

/* preserve RSP (pointing to context-data) in R9 */
"   movq  %rsp, %r9\n\t"

/* restore RSP (pointing to context-data) from RDX */
"   movq  %rdx, %rsp\n\t"

#ifndef USE_TSX
/* restore XMM storage */
"   movaps  0x0(%rsp), %xmm6\n\t"
"   movaps  0x10(%rsp), %xmm7\n\t"
"   movaps  0x20(%rsp), %xmm8\n\t"
"   movaps  0x30(%rsp), %xmm9\n\t"
"   movaps  0x40(%rsp), %xmm10\n\t"
"   movaps  0x50(%rsp), %xmm11\n\t"
"   movaps  0x60(%rsp), %xmm12\n\t"
"   movaps  0x70(%rsp), %xmm13\n\t"
"   movaps  0x80(%rsp), %xmm14\n\t"
"   movaps  0x90(%rsp), %xmm15\n\t"
"   ldmxcsr 0xa0(%rsp)\n\t" /* restore MMX control- and status-word */
"   fldcw   0xa4(%rsp)\n\t" /* restore x87 control-word */
#endif

/* load NT_TIB */
"   movq  %gs:(0x30), %r10\n\t"
/* restore fiber local storage */
"   movq  0xb0(%rsp), %rax\n\t"
"   movq  %rax, 0x20(%r10)\n\t"
/* restore current deallocation stack */
"   movq  0xb8(%rsp), %rax\n\t"
"   movq  %rax, 0x1478(%r10)\n\t"
/* restore current stack limit */
"   movq  0xc0(%rsp), %rax\n\t"
"   movq  %rax, 0x10(%r10)\n\t"
/* restore current stack base */
"   movq  0xc8(%rsp), %rax\n\t"
"   movq  %rax, 0x08(%r10)\n\t"

"   movq  0xd0(%rsp),  %r12\n\t"  /* restore R12 */
"   movq  0xd8(%rsp),  %r13\n\t"  /* restore R13 */
"   movq  0xe0(%rsp),  %r14\n\t"  /* restore R14 */
"   movq  0xe8(%rsp),  %r15\n\t"  /* restore R15 */
"   movq  0xf0(%rsp),  %rdi\n\t"  /* restore RDI */
"   movq  0xf8(%rsp),  %rsi\n\t"  /* restore RSI */
"   movq  0x100(%rsp), %rbx\n\t"  /* restore RBX */
"   movq  0x108(%rsp), %rbp\n\t"  /* restore RBP */

"   movq  0x110(%rsp), %rax\n\t"  /* restore hidden address of transport_t */

"   leaq  0x118(%rsp), %rsp\n\t" /* prepare stack */

/* restore return-address */
"   popq  %r10\n\t"

/* transport_t returned in RAX */
/* return parent fcontext_t */
"   movq  %r9, 0x0(%rax)\n\t"
/* return data */
"   movq  %r8, 0x8(%rax)\n\t"

/* transport_t as 1.arg of context-function */
"   movq  %rax, %rcx\n\t"

/* indirect jump to context */
"   jmp  *%r10\n\t"
".seh_endproc\n\t"
".section .drectve\n\t"
".ascii \" -export:\\\"sky_context_jump\\\"\"\n\t"
);

asm(
".text\n\t"
".p2align 4,,15\n\t"
".globl sky_context_ontop\n\t"
".def sky_context_ontop;\t.scl\t2;\t.type\t32;\t.endef\n\t"
".seh_proc\tsky_context_ontop\t\n"
"sky_context_ontop:\n\t"
".seh_endprologue\n\t"

"   leaq  -0x118(%rsp), %rsp\n\t" /* prepare stack */

#ifndef USE_TSX
/* save XMM storage */
"   movaps  %xmm6, 0x0(%rsp)\n\t"
"   movaps  %xmm7, 0x10(%rsp)\n\t"
"   movaps  %xmm8, 0x20(%rsp)\n\t"
"   movaps  %xmm9, 0x30(%rsp)\n\t"
"   movaps  %xmm10, 0x40(%rsp)\n\t"
"   movaps  %xmm11, 0x50(%rsp)\n\t"
"   movaps  %xmm12, 0x60(%rsp)\n\t"
"   movaps  %xmm13, 0x70(%rsp)\n\t"
"   movaps  %xmm14, 0x80(%rsp)\n\t"
"   movaps  %xmm15, 0x90(%rsp)\n\t"
"   stmxcsr  0xa0(%rsp)\n\t"  /* save MMX control- and status-word */
"   fnstcw   0xa4(%rsp)\n\t"  /* save x87 control-word */
#endif

/* load NT_TIB */
"   movq  %gs:(0x30), %r10\n\t"
/* save fiber local storage */
"   movq  0x20(%r10), %rax\n\t"
"   movq  %rax, 0xb0(%rsp)\n\t"
/* save current deallocation stack */
"   movq  0x1478(%r10), %rax\n\t"
"   movq  %rax, 0xb8(%rsp)\n\t"
/* save current stack limit */
"   movq  0x10(%r10), %rax\n\t"
"   movq  %rax, 0xc0(%rsp)\n\t"
/* save current stack base */
"   movq  0x08(%r10), %rax\n\t"
"   movq  %rax, 0xc8(%rsp)\n\t"

"   movq  %r12, 0xd0(%rsp)\n\t"  /* save R12 */
"   movq  %r13, 0xd8(%rsp)\n\t"  /* save R13 */
"   movq  %r14, 0xe0(%rsp)\n\t"  /* save R14 */
"   movq  %r15, 0xe8(%rsp)\n\t"  /* save R15 */
"   movq  %rdi, 0xf0(%rsp)\n\t"  /* save RDI */
"   movq  %rsi, 0xf8(%rsp)\n\t"  /* save RSI */
"   movq  %rbx, 0x100(%rsp)\n\t"  /* save RBX */
"   movq  %rbp, 0x108(%rsp)\n\t"  /* save RBP */

"   movq  %rcx, 0x110(%rsp)\n\t"  /* save hidden address of transport_t */

/* preserve RSP (pointing to context-data) in RCX */
"   movq  %rsp, %rcx\n\t"

/* restore RSP (pointing to context-data) from RDX */
"   movq  %rdx, %rsp\n\t"

#ifndef USE_TSX
/* restore XMM storage */
"   movaps  0x0(%rsp), %xmm6\n\t"
"   movaps  0x10(%rsp), %xmm7\n\t"
"   movaps  0x20(%rsp), %xmm8\n\t"
"   movaps  0x30(%rsp), %xmm9\n\t"
"   movaps  0x40(%rsp), %xmm10\n\t"
"   movaps  0x50(%rsp), %xmm11\n\t"
"   movaps  0x60(%rsp), %xmm12\n\t"
"   movaps  0x70(%rsp), %xmm13\n\t"
"   movaps  0x80(%rsp), %xmm14\n\t"
"   movaps  0x90(%rsp), %xmm15\n\t"
"   ldmxcsr 0xa0(%rsp)\n\t" /* restore MMX control- and status-word */
"   fldcw   0xa4(%rsp)\n\t" /* restore x87 control-word */
#endif

/* load NT_TIB */
"   movq  %gs:(0x30), %r10\n\t"
/* restore fiber local storage */
"   movq  0xb0(%rsp), %rax\n\t"
"   movq  %rax, 0x20(%r10)\n\t"
/* restore current deallocation stack */
"   movq  0xb8(%rsp), %rax\n\t"
"   movq  %rax, 0x1478(%r10)\n\t"
/* restore current stack limit */
"   movq  0xc0(%rsp), %rax\n\t"
"   movq  %rax, 0x10(%r10)\n\t"
/* restore current stack base */
"   movq  0xc8(%rsp), %rax\n\t"
"   movq  %rax, 0x08(%r10)\n\t"

"   movq  0xd0(%rsp),  %r12\n\t"  /* restore R12 */
"   movq  0xd8(%rsp),  %r13\n\t"  /* restore R13 */
"   movq  0xe0(%rsp),  %r14\n\t"  /* restore R14 */
"   movq  0xe8(%rsp),  %r15\n\t"  /* restore R15 */
"   movq  0xf0(%rsp),  %rdi\n\t"  /* restore RDI */
"   movq  0xf8(%rsp),  %rsi\n\t"  /* restore RSI */
"   movq  0x100(%rsp), %rbx\n\t"  /* restore RBX */
"   movq  0x108(%rsp), %rbp\n\t"  /* restore RBP */

"   movq  0x110(%rsp), %rax\n\t"  /* restore hidden address of transport_t */

"   leaq  0x118(%rsp), %rsp\n\t" /* prepare stack */

/* keep return-address on stack */

/* transport_t returned in RAX */
/* return parent context_t */
"   movq  %rcx, 0x0(%rax)\n\t"
/* return data */
"   movq  %r8, 0x8(%rax)\n\t"

/* transport_t as 1.arg of context-function */
/* RCX contains address of returned (hidden) transfer_t */
"   movq  %rax, %rcx\n\t"

/* RDX contains address of passed transfer_t */
"   movq  %rax, %rdx\n\t"

/* indirect jump to context */
"   jmp  *%r9\n\t"
".seh_endproc\n\t"
".section .drectve\n\t"
".ascii \" -export:\\\"sky_context_ontop\\\"\"\n\t"
);

#endif