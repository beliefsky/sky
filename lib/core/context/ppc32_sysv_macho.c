//
// Created by weijing on 2024/5/31.
//

#if defined(__ppc__) && defined(__APPLE__)

asm(
".text\n\t"
".globl _sky_context_make\n\t"
".align 2\n\t"
"_sky_context_make:\n\t"
// save return address into R6
"   mflr  r6\n\t"

// first arg of sky_context_make() == top address of context-function
// shift address in R3 to lower 16 byte boundary
"   clrrwi  r3, r3, 4\n\t"

// reserve space for context-data on context-stack
// including 64 byte of linkage + parameter area (R1 % 16 == 0)
"   subi  r3, r3, 336\n\t"

// third arg of sky_context_make() == address of context-function
// store as trampoline's R31
"   stw  r5, 224(r3)\n\t"

// set back-chain to zero
"   li   r0, 0\n\t"
"   stw  r0, 244(r3)\n\t"

"   mffs  f0\n\t"  // load FPSCR
"   stfd  f0, 144(r3)\n\t"  // save FPSCR

// compute address of returned transfer_t
"   addi  r0, r3, 252\n\t"
"   mr    r4, r0\n\t"
"   stw   r4, 228(r3)\n\t"

// load LR
"   mflr  r0\n\t"
// jump to label 1
"   bcl  20, 31, L1\n\t"
"L1:\n\t"
// load LR into R4
"   mflr  r4\n\t"
// compute abs address of trampoline, use as PC
"   addi  r5, r4, lo16(Ltrampoline - L1)\n\t"
"   stw  r5, 240(r3)\n\t"
// compute abs address of label finish
"   addi  r4, r4, lo16(Lfinish - L1)\n\t"
// restore LR
"   mtlr  r0\n\t"
// save address of finish as return-address for context-function
// will be entered after context-function returns
"   stw  r4, 236(r3)\n\t"

// restore return address from R6
"   mtlr  r6\n\t"

"   blr\n\t"  // return pointer to context-data

"Ltrampoline:\n\t"
// We get R31 = context-function, R3 = address of transfer_t,
// but we need to pass R3:R4 = transfer_t.
"   mtctr  r31\n\t"
"   lwz  r4, 4(r3)\n\t"
"   lwz  r3, 0(r3)\n\t"
"   bctr\n\t"

"Lfinish:\n\t"
// load address of _exit into CTR
"   bcl  20, 31, L2\n\t"
"L2:\n\t"
"   mflr  r4\n\t"
"   addis  r4, r4, ha16(Lexitp - L2)\n\t"
"   lwz  r4, lo16(Lexitp - L2)(r4)\n\t"
"   mtctr  r4\n\t"
// exit code is zero
"   li  r3, 0\n\t"
// exit application
"   bctr\n\t"

".const_data\n\t"
".align 2\n\t"
"Lexitp:\n\t"
"   .long  __exit\n\t"

);


asm(
".text\n\t"
".globl _sky_context_jump\n\t"
".align 2\n\t"
"_sky_context_jump:\n\t"
// reserve space on stack
"   subi  r1, r1, 244\n\t"

"   stfd  f14, 0(r1)\n\t"  // save F14
"   stfd  f15, 8(r1)\n\t"  // save F15
"   stfd  f16, 16(r1)\n\t"  // save F16
"   stfd  f17, 24(r1)\n\t"  // save F17
"   stfd  f18, 32(r1)\n\t"  // save F18
"   stfd  f19, 40(r1)\n\t"  // save F19
"   stfd  f20, 48(r1)\n\t"  // save F20
"   stfd  f21, 56(r1)\n\t"  // save F21
"   stfd  f22, 64(r1)\n\t"  // save F22
"   stfd  f23, 72(r1)\n\t"  // save F23
"   stfd  f24, 80(r1)\n\t"  // save F24
"   stfd  f25, 88(r1)\n\t"  // save F25
"   stfd  f26, 96(r1)\n\t"  // save F26
"   stfd  f27, 104(r1)\n\t"  // save F27
"   stfd  f28, 112(r1)\n\t"  // save F28
"   stfd  f29, 120(r1)\n\t"  // save F29
"   stfd  f30, 128(r1)\n\t"  // save F30
"   stfd  f31, 136(r1)\n\t"  // save F31
"   mffs  f0\n\t"  // load FPSCR
"   stfd  f0, 144(r1)\n\t"  // save FPSCR

"   stw  r13, 152(r1)\n\t"  // save R13
"   stw  r14, 156(r1)\n\t"  // save R14
"   stw  r15, 160(r1)\n\t"  // save R15
"   stw  r16, 164(r1)\n\t"  // save R16
"   stw  r17, 168(r1)\n\t"  // save R17
"   stw  r18, 172(r1)\n\t"  // save R18
"   stw  r19, 176(r1)\n\t"  // save R19
"   stw  r20, 180(r1)\n\t"  // save R20
"   stw  r21, 184(r1)\n\t"  // save R21
"   stw  r22, 188(r1)\n\t"  // save R22
"   stw  r23, 192(r1)\n\t"  // save R23
"   stw  r24, 196(r1)\n\t"  // save R24
"   stw  r25, 200(r1)\n\t"  // save R25
"   stw  r26, 204(r1)\n\t"  // save R26
"   stw  r27, 208(r1)\n\t"  // save R27
"   stw  r28, 212(r1)\n\t"  // save R28
"   stw  r29, 216(r1)\n\t"  // save R29
"   stw  r30, 220(r1)\n\t"  // save R30
"   stw  r31, 224(r1)\n\t"  // save R31
"   stw  r3,  228(r1)\n\t"  // save hidden

// save CR
"   mfcr  r0\n\t"
"   stw   r0, 232(r1)\n\t"
// save LR
"   mflr  r0\n\t"
"   stw   r0, 236(r1)\n\t"
// save LR as PC
"   stw   r0, 240(r1)\n\t"

// store RSP (pointing to context-data) in R6
"   mr  r6, r1\n\t"

// restore RSP (pointing to context-data) from R4
"   mr  r1, r4\n\t"

"   lfd  f14, 0(r1)\n\t"  // restore F14
"   lfd  f15, 8(r1)\n\t"  // restore F15
"   lfd  f16, 16(r1)\n\t"  // restore F16
"   lfd  f17, 24(r1)\n\t"  // restore F17
"   lfd  f18, 32(r1)\n\t"  // restore F18
"   lfd  f19, 40(r1)\n\t"  // restore F19
"   lfd  f20, 48(r1)\n\t"  // restore F20
"   lfd  f21, 56(r1)\n\t"  // restore F21
"   lfd  f22, 64(r1)\n\t"  // restore F22
"   lfd  f23, 72(r1)\n\t"  // restore F23
"   lfd  f24, 80(r1)\n\t"  // restore F24
"   lfd  f25, 88(r1)\n\t"  // restore F25
"   lfd  f26, 96(r1)\n\t"  // restore F26
"   lfd  f27, 104(r1)\n\t"  // restore F27
"   lfd  f28, 112(r1)\n\t"  // restore F28
"   lfd  f29, 120(r1)\n\t"  // restore F29
"   lfd  f30, 128(r1)\n\t"  // restore F30
"   lfd  f31, 136(r1)\n\t"  // restore F31
"   lfd  f0,  144(r1)\n\t"  // load FPSCR
"   mtfsf  0xff, f0\n\t"  // restore FPSCR

"   lwz  r13, 152(r1)\n\t"  // restore R13
"   lwz  r14, 156(r1)\n\t"  // restore R14
"   lwz  r15, 160(r1)\n\t"  // restore R15
"   lwz  r16, 164(r1)\n\t"  // restore R16
"   lwz  r17, 168(r1)\n\t"  // restore R17
"   lwz  r18, 172(r1)\n\t"  // restore R18
"   lwz  r19, 176(r1)\n\t"  // restore R19
"   lwz  r20, 180(r1)\n\t"  // restore R20
"   lwz  r21, 184(r1)\n\t"  // restore R21
"   lwz  r22, 188(r1)\n\t"  // restore R22
"   lwz  r23, 192(r1)\n\t"  // restore R23
"   lwz  r24, 196(r1)\n\t"  // restore R24
"   lwz  r25, 200(r1)\n\t"  // restore R25
"   lwz  r26, 204(r1)\n\t"  // restore R26
"   lwz  r27, 208(r1)\n\t"  // restore R27
"   lwz  r28, 212(r1)\n\t"  // restore R28
"   lwz  r29, 216(r1)\n\t"  // restore R29
"   lwz  r30, 220(r1)\n\t"  // restore R30
"   lwz  r31, 224(r1)\n\t"  // restore R31
"   lwz  r3,  228(r1)\n\t"  // restore hidden

// restore CR
"   lwz   r0, 232(r1)\n\t"
"   mtcr  r0\n\t"
// restore LR
"   lwz   r0, 236(r1)\n\t"
"   mtlr  r0\n\t"
// load PC
"   lwz   r0, 240(r1)\n\t"
// restore CTR
"   mtctr r0\n\t"

// adjust stack
"   addi  r1, r1, 244\n\t"

// return transfer_t
"   stw  r6, 0(r3)\n\t"
"   stw  r5, 4(r3)\n\t"

// jump to context
"   bctr\n\t"

);

#endif