//
// Created by weijing on 2024/5/31.
//
#if defined(__ppc__) && defined(__ELF__)

asm(
".text\n\t"
".globl sky_context_make\n\t"
".align 2\n\t"
".type sky_context_make,@function\n\t"
"sky_context_make:\n\t"
// save return address into R6
"   mflr  %r6\n\t"

// first arg of sky_context_make() == top address of context-function
// shift address in R3 to lower 16 byte boundary
"   clrrwi  %r3, %r3, 4\n\t"

// reserve space on context-stack, including 16 bytes of linkage
// and parameter area + 240 bytes of context-data (R1 % 16 == 0)
"   subi  %r3, %r3, 16 + 240\n\t"

// third arg of sky_context_make() == address of context-function
#ifdef __linux__
// save context-function as PC
"   stw  %r5, 16(%r3)\n\t"
#else
// save context-function for trampoline
"   stw  %r5, 248(%r3)\n\t"
#endif

// set back-chain to zero
"   li   %r0, 0\n\t"
"   stw  %r0, 240(%r3)\n\t"

// copy FPSCR to new context
"   mffs  %f0\n\t"
"   stfd  %f0, 8(%r3)\n\t"

#ifdef __linux__
// set hidden pointer for returning transfer_t
"   la    %r0, 248(%r3)\n\t"
"   stw   %r0, 4(%r3)\n\t"
#endif

// load address of label 1 into R4
"   bl  1f\n\t"
"1:  mflr  %r4\n\t"
#ifndef __linux__
// compute abs address of trampoline, use as PC
"   addi  %r7, %r4, trampoline - 1b\n\t"
"   stw   %r7, 16(%r3)\n\t"
#endif
// compute abs address of label finish
"   addi  %r4, %r4, finish - 1b\n\t"
// save address of finish as return-address for context-function
// will be entered after context-function returns
"   stw  %r4, 244(%r3)\n\t"

// restore return address from R6
"   mtlr  %r6\n\t"

"   blr\n\t"  // return pointer to context-data

#ifndef __linux__
"trampoline:\n\t"
// On systems other than Linux, sky_context_jump is returning the
// transfer_t in R3:R4, but we need to pass transfer_t * R3 to
// our context-function.
"   lwz   %r0, 8(%r1)\n\t"   // address of context-function
"   mtctr %r0\n\t"
"   stw   %r3, 8(%r1)\n\t"
"   stw   %r4, 12(%r1)\n\t"
"   la    %r3, 8(%r1)\n\t"   // address of transfer_t
"   bctr\n\t"
#endif

"finish:\n\t"
// Use the secure PLT for _exit(0).  If we use the insecure BSS PLT
// here, then the linker may use the insecure BSS PLT even if the
// C++ compiler wanted the secure PLT.

// set R30 for secure PLT, large model
"   bl     2f\n\t"
"2:  mflr   %r30\n\t"
"   addis  %r30, %r30, .Ltoc - 2b@ha\n\t"
"   addi   %r30, %r30, .Ltoc - 2b@l\n\t"

// call _exit(0) with special addend 0x8000 for large model
"   li  %r3, 0\n\t"
"   bl  _exit + 0x8000@plt\n\t"
"   .size sky_context_make, .-sky_context_make\n\t"

/* Provide the GOT pointer for secure PLT, large model. */
".section .got2,\"aw\"\n\t"
".Ltoc = . + 0x8000\n\t"

/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,\"\",%progbits\n\t"
);

asm(
".text\n\t"
".globl sky_context_jump\n\t"
".align 2\n\t"
".type sky_context_jump,@function\n\t"
"sky_context_jump:\n\t"
// Linux: sky_context_jump( hidden transfer_t * R3, R4, R5)
// Other: transfer_t R3:R4 = sky_context_jump( R3, R4)

"   mflr  %r0\n\t"  // return address from LR
"   mffs  %f0\n\t"  // FPSCR
"   mfcr  %r8\n\t"  // condition register

"   stwu  %r1, -240(%r1)\n\t"  // allocate stack space, R1 % 16 == 0
"   stw   %r0, 244(%r1)\n\t"   // save LR in caller's frame

#ifdef __linux__
"   stw   %r3, 4(%r1)\n\t"   // hidden pointer
#endif

"   stfd  %f0, 8(%r1)\n\t"   // FPSCR
"   stw   %r0, 16(%r1)\n\t"  // LR as PC
"   stw   %r8, 20(%r1)\n\t"  // CR

// Save registers R14 to R31.
// Don't change R2, the thread-local storage pointer.
// Don't change R13, the small data pointer.
"   stw   %r14, 24(%r1)\n\t"
"   stw   %r15, 28(%r1)\n\t"
"   stw   %r16, 32(%r1)\n\t"
"   stw   %r17, 36(%r1)\n\t"
"   stw   %r18, 40(%r1)\n\t"
"   stw   %r19, 44(%r1)\n\t"
"   stw   %r20, 48(%r1)\n\t"
"   stw   %r21, 52(%r1)\n\t"
"   stw   %r22, 56(%r1)\n\t"
"   stw   %r23, 60(%r1)\n\t"
"   stw   %r24, 64(%r1)\n\t"
"   stw   %r25, 68(%r1)\n\t"
"   stw   %r26, 72(%r1)\n\t"
"   stw   %r27, 76(%r1)\n\t"
"   stw   %r28, 80(%r1)\n\t"
"   stw   %r29, 84(%r1)\n\t"
"   stw   %r30, 88(%r1)\n\t"
"   stw   %r31, 92(%r1)\n\t"

// Save registers F14 to F31 in slots with 8-byte alignment.
// 4-byte alignment may stall the pipeline of some processors.
// Less than 4 may cause alignment traps.
"   stfd  %f14, 96(%r1)\n\t"
"   stfd  %f15, 104(%r1)\n\t"
"   stfd  %f16, 112(%r1)\n\t"
"   stfd  %f17, 120(%r1)\n\t"
"   stfd  %f18, 128(%r1)\n\t"
"   stfd  %f19, 136(%r1)\n\t"
"   stfd  %f20, 144(%r1)\n\t"
"   stfd  %f21, 152(%r1)\n\t"
"   stfd  %f22, 160(%r1)\n\t"
"   stfd  %f23, 168(%r1)\n\t"
"   stfd  %f24, 176(%r1)\n\t"
"   stfd  %f25, 184(%r1)\n\t"
"   stfd  %f26, 192(%r1)\n\t"
"   stfd  %f27, 200(%r1)\n\t"
"   stfd  %f28, 208(%r1)\n\t"
"   stfd  %f29, 216(%r1)\n\t"
"   stfd  %f30, 224(%r1)\n\t"
"   stfd  %f31, 232(%r1)\n\t"

// store RSP (pointing to context-data) in R7/R6
// restore RSP (pointing to context-data) from R4/R3
#ifdef __linux__
"   mr   %r7, %r1\n\t"
"   mr   %r1, %r4\n\t"
"   lwz  %r3, 4(%r1)\n\t"   // hidden pointer
#else
"   mr   %r6, %r1\n\t"
"   mr   %r1, %r3\n\t"
#endif

"   lfd  %f0, 8(%r1)\n\t"   // FPSCR
"   lwz  %r0, 16(%r1)\n\t"  // PC
"   lwz  %r8, 20(%r1)\n\t"  // CR

"   mtfsf  0xff, %f0\n\t"   // restore FPSCR
"   mtctr  %r0\n\t"         // load CTR with PC
"   mtcr   %r8\n\t"         // restore CR

// restore R14 to R31
"   lwz  %r14, 24(%r1)\n\t"
"   lwz  %r15, 28(%r1)\n\t"
"   lwz  %r16, 32(%r1)\n\t"
"   lwz  %r17, 36(%r1)\n\t"
"   lwz  %r18, 40(%r1)\n\t"
"   lwz  %r19, 44(%r1)\n\t"
"   lwz  %r20, 48(%r1)\n\t"
"   lwz  %r21, 52(%r1)\n\t"
"   lwz  %r22, 56(%r1)\n\t"
"   lwz  %r23, 60(%r1)\n\t"
"   lwz  %r24, 64(%r1)\n\t"
"   lwz  %r25, 68(%r1)\n\t"
"   lwz  %r26, 72(%r1)\n\t"
"   lwz  %r27, 76(%r1)\n\t"
"   lwz  %r28, 80(%r1)\n\t"
"   lwz  %r29, 84(%r1)\n\t"
"   lwz  %r30, 88(%r1)\n\t"
"   lwz  %r31, 92(%r1)\n\t"

// restore F14 to F31
"   lfd  %f14, 96(%r1)\n\t"
"   lfd  %f15, 104(%r1)\n\t"
"   lfd  %f16, 112(%r1)\n\t"
"   lfd  %f17, 120(%r1)\n\t"
"   lfd  %f18, 128(%r1)\n\t"
"   lfd  %f19, 136(%r1)\n\t"
"   lfd  %f20, 144(%r1)\n\t"
"   lfd  %f21, 152(%r1)\n\t"
"   lfd  %f22, 160(%r1)\n\t"
"   lfd  %f23, 168(%r1)\n\t"
"   lfd  %f24, 176(%r1)\n\t"
"   lfd  %f25, 184(%r1)\n\t"
"   lfd  %f26, 192(%r1)\n\t"
"   lfd  %f27, 200(%r1)\n\t"
"   lfd  %f28, 208(%r1)\n\t"
"   lfd  %f29, 216(%r1)\n\t"
"   lfd  %f30, 224(%r1)\n\t"
"   lfd  %f31, 232(%r1)\n\t"

// restore LR from caller's frame
"   lwz   %r0, 244(%r1)\n\t"
"   mtlr  %r0\n\t"

// adjust stack
"   addi  %r1, %r1, 240\n\t"

// return transfer_t
#ifdef __linux__
"   stw  %r7, 0(%r3)\n\t"
"   stw  %r5, 4(%r3)\n\t"
#else
"   mr   %r3, %r6\n\t"
//    %r4, %r4
#endif

// jump to context
"    bctr\n\t"
".size sky_context_jump, .-sky_context_jump\n\t"

/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,\"\",%progbits\n\t"
);

#endif

