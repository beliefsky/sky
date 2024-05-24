//
// Created by weijing on 2024/5/23.
//

#if defined(__aarch64__) && defined(__WINNT__)

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
// save stack top address to x3 
"   mov x3, x0\n\t"

// shift address in x0 (allocated stack) to lower 16 byte boundary 
"   and x0, x0, ~0xF\n\t"

// reserve space for context-data on context-stack 
"   sub  x0, x0, #0xd0\n\t"

// save top address of context_stack as 'base' 
"   str  x3, [x0, #0xa0]\n\t"
// save bottom address of context-stack as 'limit' and 'dealloction stack' 
"   sub  x3, x3, x1\n\t"
"   stp  x3, x3, [x0, #0xa8]\n\t"
// save 0 as 'fiber data' 
"   str  xzr, [x0, #0xb8]\n\t"

// third arg of sky_context_make()  == address of context-function
// store address as x19 for trampoline 
"   str  x2, [x0, #0x40]\n\t"
// store trampoline address as pc 
"   adr  x2, trampoline\n\t"
"   str  x2, [x0, #0xc0]\n\t"

// save address of finish as return-address for context-function 
// will be entered after context-function returns (LR register) 
"   adr  x1, finish\n\t"
"   str  x1, [x0, #0x98]\n\t"

"   ret  x30\n\t" // return pointer to context-data (x0) 

"trampoline\n\t"
"   stp  fp, lr, [sp, #-0x10]!\n\t"
"   mov  fp, sp\n\t"
"   blr x19\n\t"

"finish:\n\t"
// exit code is zero 
"   mov  x0, #0\n\t"
// exit application 
"   bl  _exit\n\t"

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
// prepare stack for GP + FPU
"   sub  sp, sp, #0xd0\n\t"

// save d8 - d15
"   stp  d8,  d9,  [sp, #0x00]\n\t"
"   stp  d10, d11, [sp, #0x10]\n\t"
"   stp  d12, d13, [sp, #0x20]\n\t"
"   stp  d14, d15, [sp, #0x30]\n\t"

// save x19-x30
"   stp  x19, x20, [sp, #0x40]\n\t"
"   stp  x21, x22, [sp, #0x50]\n\t"
"   stp  x23, x24, [sp, #0x60]\n\t"
"   stp  x25, x26, [sp, #0x70]\n\t"
"   stp  x27, x28, [sp, #0x80]\n\t"
"   stp  x29, x30, [sp, #0x90]\n\t"

// save LR as PC
"   str  x30, [sp, #0xc0]\n\t"

// save current stack base and limit
"   ldp  x5,  x6,  [x18, #0x08]\n\t" // TeStackBase and TeStackLimit at ksarm64.h
"   stp  x5,  x6,  [sp, #0xa0]\n\t"
// save current fiber data and deallocation stack
"   ldr  x5, [x18, #0x1478]\n\t" // TeDeallocationStack at ksarm64.h
"   ldr  x6, [x18, #0x20]\n\t" // TeFiberData at ksarm64.h
"   stp  x5,  x6,  [sp, #0xb0]\n\t"

// store RSP (pointing to context-data) in X0 
"   mov  x4, sp\n\t"

// restore RSP (pointing to context-data) from X1 
"   mov  sp, x0\n\t"

// restore stack base and limit 
"   ldp  x5,  x6,  [sp, #0xa0]\n\t"
"   stp  x5,  x6,  [x18, #0x08]\n\t" // TeStackBase and TeStackLimit at ksarm64.h
// restore fiber data and deallocation stack 
"   ldp  x5,  x6,  [sp, #0xb0]\n\t"
"   str  x5, [x18, #0x1478]\n\t" // TeDeallocationStack at ksarm64.h
"   str  x6, [x18, #0x20]\n\t" // TeFiberData at ksarm64.h

// load d8 - d15 
"   ldp  d8,  d9,  [sp, #0x00]\n\t"
"   ldp  d10, d11, [sp, #0x10]\n\t"
"   ldp  d12, d13, [sp, #0x20]\n\t"
"   ldp  d14, d15, [sp, #0x30]\n\t"

// load x19-x30 
"   ldp  x19, x20, [sp, #0x40]\n\t"
"   ldp  x21, x22, [sp, #0x50]\n\t"
"   ldp  x23, x24, [sp, #0x60]\n\t"
"   ldp  x25, x26, [sp, #0x70]\n\t"
"   ldp  x27, x28, [sp, #0x80]\n\t"
"   ldp  x29, x30, [sp, #0x90]\n\t"

// return transfer_t from jump 
// pass transfer_t as first arg in context function 
// X0 == FCTX, X1 == DATA 
"   mov x0, x4\n\t"

// load pc 
"   ldr  x4, [sp, #0xc0]\n\t"

// restore stack from GP + FPU 
"   add  sp, sp, #0xd0\n\t"

"   ret x4\n\t"

".section .drectve\n\t"
".ascii \" -export:\\\"sky_context_jump\\\"\"\n\t"
);

#endif