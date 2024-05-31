//
// Created by weijing on 2024/5/31.
//
#if defined(__ppc64__) && defined(__APPLE__)

asm(
".text\n\t"
".globl _sky_context_make\n\t"
"_sky_context_make:\n\t"
// save return address into R6
"   mflr  r6\n\t"

// first arg of sky_context_make() == top address of context-function
// shift address in R3 to lower 16 byte boundary
"   clrrwi  r3, r3, 4\n\t"

// reserve space for context-data on context-stack
// including 64 byte of linkage + parameter area (R1  16 == 0)
"   subi  r3, r3, 240\n\t"

// third arg of sky_context_make() == address of context-function
"   stw  r5, 176(r3)\n\t"

// set back-chain to zero
"   li   r0, 0\n\t"
"   std  r0, 184(r3)\n\t"

// compute address of returned transfer_t
"   addi r0, r3, 224\n\t"
"   mr   r4, r0\n\t"
"   std  r4, 152(r3)\n\t"

// load LR
"   mflr  r0\n\t"
// jump to label 1
"   bl  l1\n\t"
"l1:\n\t"
// load LR into R4
"   mflr  r4\n\t"
// compute abs address of label finish
"   addi  r4, r4, lo16((finish - .) + 4)\n\t"
// restore LR
"   mtlr  r0\n\t"
// save address of finish as return-address for context-function
// will be entered after context-function returns
"   std  r4, 168(r3)\n\t"

// restore return address from R6
"   mtlr  r6\n\t"

"   blr\n\t"  // return pointer to context-data

"finish:\n\t"
// save return address into R0
"   mflr  r0\n\t"
// save return address on stack, set up stack frame
"   stw  r0, 8(r1)\n\t"
// allocate stack space, R1  16 == 0
"   stwu  r1, -32(r1)\n\t"

// set return value to zero
"   li  r3, 0\n\t"
// exit application
"   bl  __exit\n\t"
"   nop\n\t"
);


asm(
".text\n\t"
".align 2\n\t"
".globl sky_context_jump\n\t"

"sky_context_jump:\n\t"
// reserve space on stack
"   subi  r1, r1, 184\n\t"

"   std  r14, 8(r1)\n\t"  // save R14
"   std  r15, 16(r1)\n\t"  // save R15
"   std  r16, 24(r1)\n\t"  // save R16
"   std  r17, 32(r1)\n\t"  // save R17
"   std  r18, 40(r1)\n\t"  // save R18
"   std  r19, 48(r1)\n\t"  // save R19
"   std  r20, 56(r1)\n\t"  // save R20
"   std  r21, 64(r1)\n\t"  // save R21
"   std  r22, 72(r1)\n\t"  // save R22
"   std  r23, 80(r1)\n\t"  // save R23
"   std  r24, 88(r1)\n\t"  // save R24
"   std  r25, 96(r1)\n\t"  // save R25
"   std  r26, 104(r1)\n\t"  // save R26
"   std  r27, 112(r1)\n\t"  // save R27
"   std  r28, 120(r1)\n\t"  // save R28
"   std  r29, 128(r1)\n\t"  // save R29
"   std  r30, 136(r1)\n\t"  // save R30
"   std  r31, 144(r1)\n\t"  // save R31
"   std  r3,  152(r1)\n\t"  // save hidden

// save CR
"   mfcr  r0\n\t"
"   std   r0, 160(r1)\n\t"
// save LR
"   mflr  r0\n\t"
"   std   r0, 168(r1)\n\t"
// save LR as PC
"   std   r0, 176(r1)\n\t"

// store RSP (pointing to context-data) in R6
"   mr  r6, r1\n\t"

// restore RSP (pointing to context-data) from R4
"   mr  r1, r4\n\t"

"   ld  r14, 8(r1)\n\t"  // restore R14
"   ld  r15, 16(r1)\n\t"  // restore R15
"   ld  r16, 24(r1)\n\t"  // restore R16
"   ld  r17, 32(r1)\n\t"  // restore R17
"   ld  r18, 40(r1)\n\t"  // restore R18
"   ld  r19, 48(r1)\n\t"  // restore R19
"   ld  r20, 56(r1)\n\t"  // restore R20
"   ld  r21, 64(r1)\n\t"  // restore R21
"   ld  r22, 72(r1)\n\t"  // restore R22
"   ld  r23, 80(r1)\n\t"  // restore R23
"   ld  r24, 88(r1)\n\t"  // restore R24
"   ld  r25, 96(r1)\n\t"  // restore R25
"   ld  r26, 104(r1)\n\t"  // restore R26
"   ld  r27, 112(r1)\n\t"  // restore R27
"   ld  r28, 120(r1)\n\t"  // restore R28
"   ld  r29, 128(r1)\n\t"  // restore R29
"   ld  r30, 136(r1)\n\t"  // restore R30
"   ld  r31, 144(r1)\n\t"  // restore R31
"   ld  r3,  152(r1)\n\t"  // restore hidden

// restore CR
"   ld  r0, 160(r1)\n\t"
"   mtcr  r0\n\t"
// restore LR
"   ld  r0, 168(r1)\n\t"
"   mtlr  r0\n\t"

// load PC
"   ld  r12, 176(r1)\n\t"
// restore CTR
"   mtctr  r12\n\t"

// adjust stack
"   addi  r1, r1, 184\n\t"

// zero in r3 indicates first jump to context-function
"   cmpdi r3, 0\n\t"
"   beq use_entry_arg\n\t"

// return transfer_t
"   std  r6, 0(r3)\n\t"
"   std  r5, 8(r3)\n\t"

// jump to context
"   bctr\n\t"

"use_entry_arg:\n\t"
// copy transfer_t into transfer_fn arg registers
"   mr  r3, r6\n\t"
"   mr  r4, r5\n\t"

// jump to context
"   bctr\n\t"

);

#endif
