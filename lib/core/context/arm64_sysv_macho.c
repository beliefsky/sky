//
// Created by weijing on 2024/5/23.
//

#if defined(__aarch64__) && defined(__APPLE__)

asm(
".text\n\t"
".globl _sky_context_make\n\t"
".balign 16\n\t"
"_sky_context_make:\n\t"
/* shift address in x0 (allocated stack) to lower 16 byte boundary */
"   and x0, x0, ~0xF\n\t"

/* reserve space for context-data on context-stack  */
"   sub  x0, x0, #0xb0\n\t"

/* third arg of sky_context_make()  == address of context-function  */
/* store address as a PC to jump in  */
"   str  x2, [x0, #0xa0]\n\t"

"   adr  x1, finish\n\t"

/* save address of finish as return-address for context-function  */
/* will be entered after context-function returns (LR register)  */
"   str  x1, [x0, #0x98]\n\t"

"   ret  lr\n\t" /* return pointer to context-data (x0)  */

"finish:\n\t"
/* exit code is zero  */
"   mov  x0, #0\n\t"
/* exit application  */
"   bl  __exit\n\t"
);

asm(
".text\n\t"
".globl _sky_context_jump\n\t"
".balign 16\n\t"
"_sky_context_jump:\n\t"
/* prepare stack for GP + FPU */
"   sub  sp, sp, #0xb0\n\t"

/* save d8 - d15 */
"   stp  d8,  d9,  [sp, #0x00]\n\t"
"   stp  d10, d11, [sp, #0x10]\n\t"
"   stp  d12, d13, [sp, #0x20]\n\t"
"   stp  d14, d15, [sp, #0x30]\n\t"

/* save x19-x30 */
"   stp  x19, x20, [sp, #0x40]\n\t"
"   stp  x21, x22, [sp, #0x50]\n\t"
"   stp  x23, x24, [sp, #0x60]\n\t"
"   stp  x25, x26, [sp, #0x70]\n\t"
"   stp  x27, x28, [sp, #0x80]\n\t"
"   stp  fp,  lr,  [sp, #0x90]\n\t"

/* save LR as PC */
"   str  lr, [sp, #0xa0]\n\t"

/* store RSP (pointing to context-data) in X0 */
"   mov  x4, sp\n\t"

/* restore RSP (pointing to context-data) from X1 */
"   mov  sp, x0\n\t"

/* load d8 - d15 */
"   ldp  d8,  d9,  [sp, #0x00]\n\t"
"   ldp  d10, d11, [sp, #0x10]\n\t"
"   ldp  d12, d13, [sp, #0x20]\n\t"
"   ldp  d14, d15, [sp, #0x30]\n\t"

/* load x19-x30 */
"   ldp  x19, x20, [sp, #0x40]\n\t"
"   ldp  x21, x22, [sp, #0x50]\n\t"
"   ldp  x23, x24, [sp, #0x60]\n\t"
"   ldp  x25, x26, [sp, #0x70]\n\t"
"   ldp  x27, x28, [sp, #0x80]\n\t"
"   ldp  fp,  lr,  [sp, #0x90]\n\t"

/* return transfer_t from jump */
/* pass transfer_t as first arg in context function */
/* X0 == FCTX, X1 == DATA */
"   mov x0, x4\n\t"

/* load pc */
"   ldr  x4, [sp, #0xa0]\n\t"

/* restore stack from GP + FPU */
"   add  sp, sp, #0xb0\n\t"

"   ret x4\n\t"
);

#endif

