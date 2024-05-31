//
// Created by weijing on 2024/5/31.
//

#if defined(__mips__) && !defined(__LP64__) && defined(__ELF__)

asm(
".text\n\t"
".globl sky_context_make\n\t"
".align 2\n\t"
".type sky_context_make,@function\n\t"
".ent sky_context_make\n\t"
"sky_context_make:\n\t"
#ifdef __PIC__
".set    noreorder\n\t"
".cpload $t9\n\t"
".set    reorder\n\t"
#endif
// shift address in A0 to lower 16 byte boundary
"   li $v1, -16 // 0xfffffffffffffff0\n\t"
"   and $v0, $v1, $a0\n\t"

// reserve space for context-data on context-stack
//  includes an extra 32 bytes for:
//  - 16-byte incoming argument area required by mips ABI used when
//    jump_context calls the initial function
//  - 4 bytes to save our GP register used in finish
//  - 8 bytes to as space for transfer_t returned to finish
//  - 4 bytes for alignment
"   addiu $v0, $v0, -128\n\t"

// third arg of sky_context_make() == address of context-function
"   sw  $a2, 92($v0)\n\t"
// save global pointer in context-data
"   sw  $gp, 112($v0)\n\t"

// compute address of returned transfer_t
"   addiu $t0, $v0, 116\n\t"
"   sw  $t0, 84($v0)\n\t"

// compute abs address of label finish
"   la  $t9, finish\n\t"
// save address of finish as return-address for context-function
// will be entered after context-function returns
"   sw  $t9, 88($v0)\n\t"

"   jr  $ra\n\t" // return pointer to context-data

"finish:\n\t"
// reload our gp register (needed for la)
"   lw $gp, 16($sp)\n\t"

// call _exit(0)
//  the previous function should have left the 16 bytes incoming argument
//  area on the stack which we reuse for calling _exit
"   la $t9, _exit\n\t"
"   move $a0, $zero\n\t"
"   jr $t9\n\t"
".end sky_context_make\n\t"
".size sky_context_make, .-sky_context_make\n\t"

/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,"",%progbits\n\t"
);


asm(
".text\n\t"
".globl sky_context_jump\n\t"
".align 2\n\t"
".type sky_context_jump,@function\n\t"
".ent sky_context_jump\n\t"
"sky_context_jump:\n\t"
// reserve space on stack
"   addiu $sp, $sp, -96\n\t"

"   sw  $s0, 48($sp)\n\t"  // save S0
"   sw  $s1, 52($sp)\n\t"  // save S1
"   sw  $s2, 56($sp)\n\t"  // save S2
"   sw  $s3, 60($sp)\n\t"  // save S3
"   sw  $s4, 64($sp)\n\t"  // save S4
"   sw  $s5, 68($sp)\n\t"  // save S5
"   sw  $s6, 72($sp)\n\t"  // save S6
"   sw  $s7, 76($sp)\n\t"  // save S7
"   sw  $fp, 80($sp)\n\t"  // save FP
"   sw  $a0, 84($sp)\n\t"  // save hidden, address of returned transfer_t
"   sw  $ra, 88($sp)\n\t"  // save RA
"   sw  $ra, 92($sp)\n\t"  // save RA as PC

#if defined(__mips_hard_float)
"   s.d  $f20, ($sp)\n\t"  // save F20
"   s.d  $f22, 8($sp)\n\t"  // save F22
"   s.d  $f24, 16($sp)\n\t"  // save F24
"   s.d  $f26, 24($sp)\n\t"  // save F26
"   s.d  $f28, 32($sp)\n\t"  // save F28
"   s.d  $f30, 40($sp)\n\t"  // save F30
#endif

// store SP (pointing to context-data) in A0
"   move  $a0, $sp\n\t"

// restore SP (pointing to context-data) from A1
"   move  $sp, $a1\n\t"

#if defined(__mips_hard_float)
"   l.d  $f20, ($sp)\n\t"  // restore F20
"   l.d  $f22, 8($sp)\n\t"  // restore F22
"   l.d  $f24, 16($sp)\n\t"  // restore F24
"   l.d  $f26, 24($sp)\n\t"  // restore F26
"   l.d  $f28, 32($sp)\n\t"  // restore F28
"   l.d  $f30, 40($sp)\n\t"  // restore F30
#endif

"   lw  $s0, 48($sp)\n\t"  // restore S0
"   lw  $s1, 52($sp)\n\t"  // restore S1
"   lw  $s2, 56($sp)\n\t"  // restore S2
"   lw  $s3, 60($sp)\n\t"  // restore S3
"   lw  $s4, 64($sp)\n\t"  // restore S4
"   lw  $s5, 68($sp)\n\t"  // restore S5
"   lw  $s6, 72($sp)\n\t"  // restore S6
"   lw  $s7, 76($sp)\n\t"  // restore S7
"   lw  $fp, 80($sp)\n\t"  // restore FP
"   lw  $v0, 84($sp)\n\t"  // restore hidden, address of returned transfer_t
"   lw  $ra, 88($sp)\n\t"  // restore RA

// load PC
"   lw  $t9, 92($sp)\n\t"

// adjust stack
"   addiu $sp, $sp, 96\n\t"

// return transfer_t from jump
"   sw  $a0, ($v0)\n\t"  // fctx of transfer_t
"   sw  $a2, 4($v0)\n\t" // data of transfer_t
// pass transfer_t as first arg in context function
// A0 == fctx, A1 == data
"   move  $a1, $a2\n\t"

// jump to context
"   jr  $t9\n\t"
".end sky_context_jump\n\t"
".size sky_context_jump, .-sky_context_jump\n\t"

/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,"",%progbits\n\t"
);

#endif