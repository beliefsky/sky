//
// Created by weijing on 2024/5/23.
//

#if defined(__arm__) && defined(__APPLE__)

asm(
".text\n\t"
".globl _sky_context_make\n\t"
".align 2\n\t"
"_sky_context_make:\n\t"
// shift address in A1 to lower 16 byte boundary
"   bic  a1, a1, #15\n\t"

// reserve space for context-data on context-stack
"   sub  a1, a1, #124\n\t"

// third arg of sky_context_make()  == address of context-function
"   str  a3, [a1, #108]\n\t"

// compute address of returned transfer_t
"   add  a2, a1, #112\n\t"
"   mov  a3, a2\n\t"
"   str  a3, [a1, #68]\n\t"

// compute abs address of label finish
"   adr  a2, finish\n\t"
// save address of finish as return-address for context-function
// will be entered after context-function returns
"   str  a2, [a1, #104]\n\t"

"   bx  lr\n\t" // return pointer to context-data
"finish:\n\t"
/* exit code is zero  */
"   mov  a1, #0\n\t"
/* exit application  */
"   bl  __exit\n\t"
);

asm(
".text\n\t"
".globl _sky_context_jump\n\t"
".align 2\n\t"
"_sky_context_jump:\n\t"
// save LR as PC
"   push {lr}\n\t"
// save hidden,V1-V8,LR
"   push {a1,v1-v8,lr}\n\t"

// locate TLS to save/restore SjLj handler
"   mrc  p15, 0, v2, c13, c0, #3\n\t"
"   bic  v2, v2, #3\n\t"

// load TLS[__PTK_LIBC_DYLD_Unwind_SjLj_Key]
"   ldr  v1, [v2, #72]\n\t"
// save SjLj handler
"   push  {v1}\n\t"

// prepare stack for FPU
"   sub  sp, sp, #64\n\t"
#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
// save S16-S31
"   vstmia sp, {d8-d15}\n\t"
#endif

// store RSP (pointing to context-data) in A1
"   mov  a1, sp\n\t"

// restore RSP (pointing to context-data) from A2
"   mov  sp, a2\n\t"

#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
// restore S16-S31
"   vldmia  sp, {d8-d15}\n\t"
#endif
// prepare stack for FPU
"   add  sp, sp, #64\n\t"

// r#estore SjLj handler
"   pop  {v1}\n\t"
// store SjLj handler in TLS
"   str  v1, [v2, #72]\n\t"

// restore hidden,V1-V8,LR
"   pop {a4,v1-v8,lr}\n\t"

// return transfer_t from jump
"   str  a1, [a4, #0]\n\t"
"   str  a3, [a4, #4]\n\t"
// pass transfer_t as first arg in context function
// A1 == FCTX, A2 == DATA
"   mov  a2, a3\n\t"

// restore PC
"   pop {pc}\n\t"
);

asm(
".text\n\t"
".globl _sky_context_ontop\n\t"
".align 2\n\t"
"_sky_context_ontop:\n\t"
// save LR as PC
"   push {lr}\n\t"
// save hidden,V1-V8,LR
"   push {a1,v1-v8,lr}\n\t"

// locate TLS to save/restore SjLj handler
"   mrc  p15, 0, v2, c13, c0, #3\n\t"
"   bic  v2, v2, #3\n\t"

// load TLS[__PTK_LIBC_DYLD_Unwind_SjLj_Key]
"   ldr  v1, [v2, #72]\n\t"
// save SjLj handler
"   push  {v1}\n\t"

// prepare stack for FPU
"   sub  sp, sp, #64\n\t"
#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
// save S16-S31
 "   vstmia sp, {d8-d15}\n\t"
#endif

// store RSP (pointing to context-data) in A1
"   mov  a1, sp\n\t"

// restore RSP (pointing to context-data) from A2
"   mov  sp, a2\n\t"

#if (defined(__VFP_FP__) && !defined(__SOFTFP__))
// restore S16-S31
"   vldmia  sp, {d8-d15}\n\t"
#endif
// prepare stack for FPU
"   add  sp, sp, #64\n\t"

// restore SjLj handler
"   pop  {v1}\n\t"
// store SjLj handler in TLS
"   str  v1, [v2, #72]\n\t"

// store parent context in A2
"   mov  a2, a1\n\t"

// restore hidden,V1-V8,LR
"   pop {a1,v1-v8,lr}\n\t"

// return transfer_t from jump
"   str  a2, [a1, #0]\n\t"
"   str  a3, [a1, #4]\n\t"
// pass transfer_t as first arg in context function
// A1 == hidden, A2 == FCTX, A3 == DATA

// skip PC
"   add  sp, sp, #4\n\t"

// jump to ontop-function
"   bx  a4\n\t"
);

#endif

