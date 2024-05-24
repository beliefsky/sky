//
// Created by weijing on 2024/5/23.
//

#if defined(__arm__) && defined(__ELF__)

asm(
".text\n\t"
".globl sky_context_make\n\t"
".align 2\n\t"
".type sky_context_make,//function\n\t"
".syntax unified\n\t"
"sky_context_make:\n\t"
// shift address in A1 to lower 16 byte boundary
"   bic  a1, a1, #15\n\t"

// reserve space for context-data on context-stack
"   sub  a1, a1, #124\n\t"

// third arg of sky_context_make()  == address of context-function
"   str  a3, [a1, #104]\n\t"

// compute address of returned transfer_t
"   add  a2, a1, #108\n\t"
"   mov  a3, a2\n\t"
"   str  a3, [a1, #64]\n\t"

// compute abs address of label finish
"   adr  a2, finish\n\t"
// save address of finish as return-address for context-function
// will be entered after context-function returns
"   str  a2, [a1, #100]\n\t"

"   bx  lr\n\t" // return pointer to context-data

"finish:\n\t"
// exit code is zero
"   mov  a1, #0\n\t"
// exit application
"   bl  _exit\n\t" //PLT
".size sky_context_make,.-sky_context_make\n\t"
".section .note.GNU-stack,\"\",%progbits\n\t"
);

asm(
".text\n\t"
".globl sky_context_jump\n\t"
".align 2\n\t"
".type sky_context_jump,//function\n\t"
".syntax unified\n\t"
"sky_context_jump:\n\t"
// save LR as PC
"   push {lr}\n\t"
// save hidden,V1-V8,LR
"   push {a1,v1-v8,lr}\n\t"

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
".size sky_context_jump,.-sky_context_jump\n\t"
".section .note.GNU-stack,\"\",%progbits\n\t"
);

#endif