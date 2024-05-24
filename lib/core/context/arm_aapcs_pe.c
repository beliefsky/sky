//
// Created by weijing on 2024/5/23.
//

#if defined(__arm__) && defined(__WINNT__)

asm(
"   AREA |.text|, CODE\n\t"
"   ALIGN 4\n\t"
"   EXPORT sky_context_make\n\t"
"   IMPORT _exit\n\t"
"sky_context_make proc\n\t"
// first arg of sky_context_make()  == top of context-stack
// save top of context-stack (base) A4
"   mov  a4, a1\n\t"

// shift address in A1 to lower 16 byte boundary
"   bic  a1, a1, #0x0f\n\t"

// reserve space for context-data on context-stack
"   sub  a1, a1, #0x48\n\t"

// save top address of context_stack as 'base'
"   str  a4, [a1, #0x8]\n\t"
// second arg of sky_context_make()  == size of context-stack
// compute bottom address of context-stack (limit)
"   sub  a4, a4, a2\n\t"
// save bottom address of context-stack as 'limit'
"   str  a4, [a1, #0x4]\n\t"
// save bottom address of context-stack as 'dealloction stack'
"   str  a4, [a1, #0x0]\n\t"

// third arg of sky_context_make()  == address of context-function
"   str  a3, [a1, #0x34]\n\t"

// compute address of returned transfer_t
"   add  a2, a1, #0x38\n\t"
"   mov  a3, a2\n\t"
"   str  a3, [a1, #0xc]\n\t"

// compute abs address of label finish
"   adr  a2, finish\n\t"
// save address of finish as return-address for context-function
// will be entered after context-function returns
"   str  a2, [a1, #0x30]\n\t"

"   bx  lr\n\t" // return pointer to context-data

"finish\n\t"
// exit code is zero 
"   mov  a1, #0\n\t"
// exit application 
"   bl  _exit\n\t"
"   ENDP\n\t"
"   END\n\t"
);


asm(
"   AREA |.text|, CODE\n\t"
"   ALIGN 4\n\t"
"   EXPORT sky_context_jump\n\t"
"   IMPORT _exit\n\t"
"sky_context_jump proc\n\t"
// save LR as PC
"   push {lr}\n\t"
// save hidden,V1-V8,LR
"   push {a1,v1-v8,lr}\n\t"

// load TIB to save/restore thread size and limit.
// we do not need preserve CPU flag and can use it's arg register
"   mrc     p15, #0, v1, c13, c0, #2\n\t"

// save current stack base
"   ldr  a5, [v1, #0x04]\n\t"
"   push {a5}\n\t"
// save current stack limit
"   ldr  a5, [v1, #0x08]\n\t"
"   push {a5}\n\t"
// save current deallocation stack
"   ldr  a5, [v1, #0xe0c]\n\t"
"   push {a5}\n\t"

// store RSP (pointing to context-data) in A1
"   mov  a1, sp\n\t"

// restore RSP (pointing to context-data) from A2
"   mov  sp, a2\n\t"

// restore deallocation stack
"   pop  {a5}\n\t"
"   str  a5, [v1, #0xe0c]\n\t"
// restore stack limit
"   pop  {a5}\n\t"
"   str  a5, [v1, #0x08]\n\t"
// restore stack base
"   pop  {a5}\n\t"
"   str  a5, [v1, #0x04]\n\t"

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
"   ENDP\n\t"
"   END\n\t"
);

#endif