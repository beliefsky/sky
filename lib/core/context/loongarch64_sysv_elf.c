//
// Created by weijing on 2024/5/31.
//

#if defined(__loongarch__) && defined(__ELF__)

asm(
".text\n\t"
".globl sky_context_make\n\t"
".align 2\n\t"
".type sky_context_make,@function\n\t"
"sky_context_make:\n\t"
// shift address in A0 to lower 16 byte boundary
"   bstrins.d $a0, $zero, 3, 0\n\t"

// reserve space for context-data on context-stack
"   addi.d  $a0, $a0, -160\n\t"

// third arg of sky_context_make() == address of context-function
"   st.d  $a2, $a0, 152\n\t"

// save address of finish as return-address for context-function
// will be entered after context-function returns
"   la.local  $a4, finish\n\t"
"   st.d  $a4, $a0, 144\n\t"

// return pointer to context-data
"   jr  $ra\n\t"

"finish:\n\t"
// exit code is zero
"   li.d  $a0, 0\n\t"
// call _exit(0)
"   b  %plt(_exit)\n\t"

".size sky_context_make, .-sky_context_make\n\t"
/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,"",%progbits\n\t"
);


asm(
".text\n\t"
".globl sky_context_jump\n\t"
".align 2\n\t"
".type sky_context_jump,@function\n\t"
"sky_context_jump:\n\t"
// reserve space on stack
"   addi.d  $sp, $sp, -160\n\t"

// save fs0 - fs7
"   fst.d  $fs0, $sp, 0\n\t"
"   fst.d  $fs1, $sp, 8\n\t"
"   fst.d  $fs2, $sp, 16\n\t"
"   fst.d  $fs3, $sp, 24\n\t"
"   fst.d  $fs4, $sp, 32\n\t"
"   fst.d  $fs5, $sp, 40\n\t"
"   fst.d  $fs6, $sp, 48\n\t"
"   fst.d  $fs7, $sp, 56\n\t"

// save s0 - s8, fp, ra
"   st.d  $s0, $sp, 64\n\t"
"   st.d  $s1, $sp, 72\n\t"
"   st.d  $s2, $sp, 80\n\t"
"   st.d  $s3, $sp, 88\n\t"
"   st.d  $s4, $sp, 96\n\t"
"   st.d  $s5, $sp, 104\n\t"
"   st.d  $s6, $sp, 112\n\t"
"   st.d  $s7, $sp, 120\n\t"
"   st.d  $s8, $sp, 128\n\t"
"   st.d  $fp, $sp, 136\n\t"
"   st.d  $ra, $sp, 144\n\t"

// save RA as PC
"   st.d  $ra, $sp, 152\n\t"

// store SP (pointing to context-data) in A2
"   move  $a2, $sp\n\t"

// restore SP (pointing to context-data) from A0
"   move  $sp, $a0\n\t"

// load fs0 - fs7
"   fld.d  $fs0, $sp, 0\n\t"
"   fld.d  $fs1, $sp, 8\n\t"
"   fld.d  $fs2, $sp, 16\n\t"
"   fld.d  $fs3, $sp, 24\n\t"
"   fld.d  $fs4, $sp, 32\n\t"
"   fld.d  $fs5, $sp, 40\n\t"
"   fld.d  $fs6, $sp, 48\n\t"
"   fld.d  $fs7, $sp, 56\n\t"

// load s0 - s7
"   ld.d  $s0, $sp, 64\n\t"
"   ld.d  $s1, $sp, 72\n\t"
"   ld.d  $s2, $sp, 80\n\t"
"   ld.d  $s3, $sp, 88\n\t"
"   ld.d  $s4, $sp, 96\n\t"
"   ld.d  $s5, $sp, 104\n\t"
"   ld.d  $s6, $sp, 112\n\t"
"   ld.d  $s7, $sp, 120\n\t"
"   ld.d  $s8, $sp, 128\n\t"
"   ld.d  $fp, $sp, 136\n\t"
"   ld.d  $ra, $sp, 144\n\t"

// return transfer_t from jump
// pass transfer_t as first arg in context function
// a0 == FCTX, a1 == DATA
"   move  $a0, $a2\n\t"

// load PC
"   ld.d  $a2, $sp, 152\n\t"

// restore stack
"   addi.d  $sp, $sp, 160\n\t"

// jump to context
"   jr  $a2\n\t"
".size sky_context_jump, .-sky_context_jump\n\t"

/* Mark that we don't need executable stack.  */
".section .note.GNU-stack,"",%progbits\n\t"
);

#endif

