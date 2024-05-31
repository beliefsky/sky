//
// Created by weijing on 2024/5/31.
//
#if defined(__mips64) && defined(__ELF__)

asm(
".text\n\t"
".globl sky_context_make\n\t"
".align 3\n\t"
".type sky_context_make,@function\n\t"
".ent sky_context_make\n\t"
"sky_context_make:\n\t"
#ifdef __PIC__
".set    noreorder\n\t"
".cpload $t9\n\t"
".set    reorder\n\t"
#endif
// shift address in A0 to lower 16 byte boundary
"   li $v1, 0xfffffffffffffff0\n\t"
"   and $v0, $v1, $a0\n\t"

// reserve space for context-data on context-stack
"   daddiu $v0, $v0, -160\n\t"

// third arg of sky_context_make() == address of context-function
"   sd  $a2, 152($v0)\n\t"
// save global pointer in context-data
"   sd  $gp, 136($v0)\n\t"

// psudo instruction compute abs address of label finish based on GP
"   dla  $t9, finish\n\t"

// save address of finish as return-address for context-function
// will be entered after context-function returns
"   sd  $t9, 144($v0)\n\t"

"   jr  $ra\n\t" // return pointer to context-data

"finish:\n\t"
// reload our gp register (needed for la)
"   daddiu $t0, $sp, -160\n\t"
"   ld $gp, 136($t0)\n\t"

// call _exit(0)
//  the previous function should have left the 16 bytes incoming argument
//  area on the stack which we reuse for calling _exit
"   dla $t9, _exit\n\t"
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
".align 3\n\t"
".type sky_context_jump,@function\n\t"
".ent sky_context_jump\n\t"
"sky_context_jump:\n\t"
// reserve space on stack
"   daddiu $sp, $sp, -160\n\t"

"   sd  $s0, 64($sp)\n\t"  // save S0
"   sd  $s1, 72($sp)\n\t"  // save S1
"   sd  $s2, 80($sp)\n\t"  // save S2
"   sd  $s3, 88($sp)\n\t"  // save S3
"   sd  $s4, 96($sp)\n\t"  // save S4
"   sd  $s5, 104($sp)\n\t" // save S5
"   sd  $s6, 112($sp)\n\t" // save S6
"   sd  $s7, 120($sp)\n\t" // save S7
"   sd  $fp, 128($sp)\n\t" // save FP
"   sd  $ra, 144($sp)\n\t" // save RA
"   sd  $ra, 152($sp)\n\t" // save RA as PC

#if defined(__mips_hard_float)
"   s.d  $f24, 0($sp)\n\t"   // save F24
"   s.d  $f25, 8($sp)\n\t"   // save F25
"   s.d  $f26, 16($sp)\n\t"  // save F26
"   s.d  $f27, 24($sp)\n\t"  // save F27
"   s.d  $f28, 32($sp)\n\t"  // save F28
"   s.d  $f29, 40($sp)\n\t"  // save F29
"   s.d  $f30, 48($sp)\n\t"  // save F30
"   s.d  $f31, 56($sp)\n\t"  // save F31
#endif

// store SP (pointing to old context-data) in v0 as return
"   move  $v0, $sp\n\t"

// get SP (pointing to new context-data) from a0 param
"   move  $sp, $a0\n\t"

#if defined(__mips_hard_float)
"   l.d  $f24, 0($sp)\n\t"   // restore F24
"   l.d  $f25, 8($sp)\n\t"   // restore F25
"   l.d  $f26, 16($sp)\n\t"  // restore F26
"   l.d  $f27, 24($sp)\n\t"  // restore F27
"   l.d  $f28, 32($sp)\n\t"  // restore F28
"   l.d  $f29, 40($sp)\n\t"  // restore F29
"   l.d  $f30, 48($sp)\n\t"  // restore F30
"   l.d  $f31, 56($sp)\n\t"  // restore F31
#endif

"   ld  $s0, 64($sp)\n\t"  // restore S0
"   ld  $s1, 72($sp)\n\t"  // restore S1
"   ld  $s2, 80($sp)\n\t"  // restore S2
"   ld  $s3, 88($sp)\n\t"  // restore S3
"   ld  $s4, 96($sp)\n\t"  // restore S4
"   ld  $s5, 104($sp)\n\t" // restore S5
"   ld  $s6, 112($sp)\n\t" // restore S6
"   ld  $s7, 120($sp)\n\t" // restore S7
"   ld  $fp, 128($sp)\n\t" // restore FP
"   ld  $ra, 144($sp)\n\t" // restore RAa

// load PC
"   ld  $t9, 152($sp)\n\t"

// adjust stack
"   daddiu $sp, $sp, 160\n\t"

"   move  $a0, $v0\n\t" // move old sp from v0 to a0 as param
"   move  $v1, $a1\n\t" // move *data from a1 to v1 as return

// jump to context
"   jr  $t9\n\t"
".end sky_context_jump\n\t"
".size sky_context_jump, .-sky_context_jump\n\t"

/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,"",%progbits\n\t"

);

#endif