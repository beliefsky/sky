//
// Created by weijing on 2024/5/31.
//

#if defined(__ppc64__) && defined(__ELF__)

asm(
".globl sky_context_make\n\t"
#if _CALL_ELF == 2
".text\n\t"
".align 2\n\t"
"sky_context_make:\n\t"
"   addis	%r2, %r12, .TOC.-sky_context_make@ha\n\t"
"   addi	%r2, %r2, .TOC.-sky_context_make@l\n\t"
"   .localentry sky_context_make, . - sky_context_make\n\t"
#else
".section \".opd\",\"aw\"\n\t"
".align 3\n\t"
"sky_context_make:\n\t"
# ifdef _CALL_LINUX
".quad	.L.sky_context_make,.TOC.@tocbase,0\n\t"
".type	sky_context_make,@function\n\t"
".text\n\t"
".align 2\n\t"
".L.sky_context_make:\n\t"
# else
".hidden	.sky_context_make\n\t"
".globl	.sky_context_make\n\t"
".quad	.sky_context_make,.TOC.@tocbase,0\n\t"
".size	sky_context_make,24\n\t"
".type	.sky_context_make,@function\n\t"
".text\n\t"
".align 2\n\t"
".sky_context_make:\n\t"
# endif
#endif
// save return address into R6
"   mflr  %r6\n\t"

// first arg of sky_context_make() == top address of context-stack
// shift address in R3 to lower 16 byte boundary
"   clrrdi  %r3, %r3, 4\n\t"

// reserve space for context-data on context-stack
// including 64 byte of linkage + parameter area (R1 % 16 == 0)
"   subi  %r3, %r3, 248\n\t"

// third arg of sky_context_make() == address of context-function
// entry point (ELFv2) or descriptor (ELFv1)
#if _CALL_ELF == 2
// save address of context-function entry point
"   std  %r5, 176(%r3)\n\t"
#else
// save address of context-function entry point
"   ld   %r4, 0(%r5)\n\t"
"   std  %r4, 176(%r3)\n\t"
// save TOC of context-function
"   ld   %r4, 8(%r5)\n\t"
"   std  %r4, 0(%r3)\n\t"
#endif

// set back-chain to zero
"   li   %r0, 0\n\t"
"   std  %r0, 184(%r3)\n\t"

#if _CALL_ELF != 2
// zero in r3 indicates first jump to context-function
"   std  %r0, 152(%r3)\n\t"
#endif

// load LR
"   mflr  %r0\n\t"
// jump to label 1
"   bl  1f\n\t"
"1:\n\t"
// load LR into R4
"   mflr  %r4\n\t"
// compute abs address of label finish
"   addi  %r4, %r4, finish - 1b\n\t"
// restore LR
"   mtlr  %r0\n\t"
// save address of finish as return-address for context-function
// will be entered after context-function returns
"   std  %r4, 168(%r3)\n\t"

// restore return address from R6
"   mtlr  %r6\n\t"

"   blr\n\t"  // return pointer to context-data

"finish:\n\t"
// save return address into R0
"   mflr  %r0\n\t"
// save return address on stack, set up stack frame
"   std  %r0, 8(%r1)\n\t"
// allocate stack space, R1 % 16 == 0
"   stdu  %r1, -32(%r1)\n\t"

// exit code is zero
"   li  %r3, 0\n\t"
// exit application
"   bl  _exit\n\t"
"   nop\n\t"
#if _CALL_ELF == 2
".size sky_context_make, .-sky_context_make\n\t"
#else
#ifdef _CALL_LINUX
".size .sky_context_make, .-.L.sky_context_make\n\t"
#else
".size .sky_context_make, .-.sky_context_make\n\t"
#endif
#endif

/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,\"\",%progbits\n\t"

);


asm(
".globl sky_context_jump\n\t"
#if _CALL_ELF == 2
".text\n\t"
".align 2\n\t"
"sky_context_jump:\n\t"
"   addis   %r2, %r12, .TOC.-sky_context_jump@ha\n\t"
"   addi    %r2, %r2, .TOC.-sky_context_jump@l\n\t"
"   .localentry sky_context_jump, . - sky_context_jump\n\t"
#else
".section \".opd\",\"aw\"\n\t"
".align 3\n\t"
"sky_context_jump:\n\t"
#ifdef _CALL_LINUX
".quad   .L.sky_context_jump,.TOC.@tocbase,0\n\t"
".type   sky_context_jump,@function\n\t"
".text\n\t"
".align 2\n\t"
".L.sky_context_jump:\n\t"
#else
".hidden .sky_context_jump\n\t"
".globl  .sky_context_jump\n\t"
".quad   .sky_context_jump,.TOC.@tocbase,0\n\t"
".size   sky_context_jump,24\n\t"
".type   .sky_context_jump,@function\n\t"
".text\n\t"
".align 2\n\t"
".sky_context_jump:\n\t"
#endif
#endif
// reserve space on stack
"   subi  %r1, %r1, 184\n\t"

#if _CALL_ELF != 2
"   std  %r2,  0(%r1)\n\t"  // save TOC
#endif
"   std  %r14, 8(%r1)\n\t"  // save R14
"   std  %r15, 16(%r1)\n\t"  // save R15
"   std  %r16, 24(%r1)\n\t"  // save R16
"   std  %r17, 32(%r1)\n\t"  // save R17
"   std  %r18, 40(%r1)\n\t"  // save R18
"   std  %r19, 48(%r1)\n\t"  // save R19
"   std  %r20, 56(%r1)\n\t"  // save R20
"   std  %r21, 64(%r1)\n\t"  // save R21
"   std  %r22, 72(%r1)\n\t"  // save R22
"   std  %r23, 80(%r1)\n\t"  // save R23
"   std  %r24, 88(%r1)\n\t"  // save R24
"   std  %r25, 96(%r1)\n\t"  // save R25
"   std  %r26, 104(%r1)\n\t"  // save R26
"   std  %r27, 112(%r1)\n\t"  // save R27
"   std  %r28, 120(%r1)\n\t"  // save R28
"   std  %r29, 128(%r1)\n\t"  // save R29
"   std  %r30, 136(%r1)\n\t"  // save R30
"   std  %r31, 144(%r1)\n\t"  // save R31
#if _CALL_ELF != 2
"   std  %r3,  152(%r1)\n\t"  // save hidden
#endif

// save CR
"   mfcr  %r0\n\t"
"   std   %r0, 160(%r1)\n\t"
// save LR
"   mflr  %r0\n\t"
"   std   %r0, 168(%r1)\n\t"
// save LR as PC
"   std   %r0, 176(%r1)\n\t"

// store RSP (pointing to context-data) in R6
"   mr  %r6, %r1\n\t"

#if _CALL_ELF == 2
// restore RSP (pointing to context-data) from R3
"   mr  %r1, %r3\n\t"
#else
// restore RSP (pointing to context-data) from R4
"   mr  %r1, %r4\n\t"

"   ld  %r2,  0(%r1)\n\t"  // restore TOC
#endif
"   ld  %r14, 8(%r1)\n\t"  // restore R14
"   ld  %r15, 16(%r1)\n\t"  // restore R15
"   ld  %r16, 24(%r1)\n\t"  // restore R16
"   ld  %r17, 32(%r1)\n\t"  // restore R17
"   ld  %r18, 40(%r1)\n\t"  // restore R18
"   ld  %r19, 48(%r1)\n\t"  // restore R19
"   ld  %r20, 56(%r1)\n\t"  // restore R20
"   ld  %r21, 64(%r1)\n\t"  // restore R21
"   ld  %r22, 72(%r1)\n\t"  // restore R22
"   ld  %r23, 80(%r1)\n\t"  // restore R23
"   ld  %r24, 88(%r1)\n\t"  // restore R24
"   ld  %r25, 96(%r1)\n\t"  // restore R25
"   ld  %r26, 104(%r1)\n\t"  // restore R26
"   ld  %r27, 112(%r1)\n\t"  // restore R27
"   ld  %r28, 120(%r1)\n\t"  // restore R28
"   ld  %r29, 128(%r1)\n\t"  // restore R29
"   ld  %r30, 136(%r1)\n\t"  // restore R30
"   ld  %r31, 144(%r1)\n\t"  // restore R31
#if _CALL_ELF != 2
"   ld  %r3,  152(%r1)\n\t"  // restore hidden
#endif

// restore CR
"   ld  %r0, 160(%r1)\n\t"
"   mtcr  %r0\n\t"
// restore LR
"   ld  %r0, 168(%r1)\n\t"
"   mtlr  %r0\n\t"

// load PC
"   ld  %r12, 176(%r1)\n\t"
// restore CTR
"   mtctr  %r12\n\t"

// adjust stack
"   addi  %r1, %r1, 184\n\t"

#if _CALL_ELF == 2
// copy transfer_t into transfer_fn arg registers
"   mr  %r3, %r6\n\t"
    // arg pointer already in %r4

    // jump to context
"   bctr\n\t"
".size sky_context_jump, .-sky_context_jump\n\t"
#else
// zero in r3 indicates first jump to context-function
"   cmpdi %r3, 0\n\t"
"   beq use_entry_arg\n\t"

// return transfer_t
"   std  %r6, 0(%r3)\n\t"
"   std  %r5, 8(%r3)\n\t"

// jump to context
"   bctr\n\t"

"use_entry_arg:\n\t"
// copy transfer_t into transfer_fn arg registers
"   mr  %r3, %r6\n\t"
"   mr  %r4, %r5\n\t"

// jump to context
"   bctr\n\t"
# ifdef _CALL_LINUX
".size .sky_context_jump, .-.L.sky_context_jump\n\t"
# else
".size .sky_context_jump, .-.sky_context_jump\n\t"
# endif
#endif


/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,\"\",%progbits\n\t"

);

#endif