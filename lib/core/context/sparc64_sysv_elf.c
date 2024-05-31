//
// Created by weijing on 2024/5/31.
//
#if defined(__sparc__) && defined(__ELF__)

#define CC64FSZ "176"
#define BIAS    "2047"
#define FP      "112"
#define SP      "128"
#define I7      "136"

asm(
".text\n\t"
".align  4\n\t"
".global sky_context_make\n\t"
".type   sky_context_make, %function\n\t"
"sky_context_make:\n\t"
"   save	%sp, -"CC64FSZ", %sp\n\t"

// shift address in %i0 (allocated stack) to lower 16 byte boundary
"   and	%i0, -0xf, %i0\n\t"

// reserve space for two frames on the stack
// the first frame is for the call the second one holds the data
// for sky_context_jump
"   sub	%i0, 2 * "CC64FSZ", %i0\n\t"

// third argument of sky_context_make() is the context-function to call
// store it in the first stack frame, also clear %fp there to indicate
// the end of the stack.
"   stx	%i2, [%i0 + "CC64FSZ" + "I7"]\n\t"
"   stx	%g0, [%i0 + "CC64FSZ" + "FP"]\n\t"

// On OpenBSD stackghost prevents overriding the return address on
// a stack frame. So this code uses an extra trampoline to load
// to call the context-function and then do the _exit(0) dance.
// Extract the full address of the trampoline via pc relative addressing
"1:\n\t"
"   rd	%pc, %l0\n\t"
"   add	%l0, (trampoline - 1b - 8), %l0\n\t"
"   stx	%l0, [%i0 + "I7"]\n\t"

// Save framepointer to first stack frame but first substract the BIAS
"   add	%i0, "CC64FSZ" - "BIAS", %l0\n\t"
"   stx	%l0, [%i0 + "SP"]\n\t"

// Return context-data which is also includes the BIAS
"   ret\n\t"
"   restore %i0, -"BIAS", %o0\n\t"

"trampoline:\n\t"
"   ldx	[%sp + "BIAS" + "I7"], %l0\n\t"

// no need to setup transfer_t, already in %o0 and %o1
"   jmpl	%l0, %o7\n\t"
"   nop\n\t"

"   call	_exit\n\t"
"   clr	%o0\n\t"
"   unimp\n\t"
"   .size	sky_context_make,.-sky_context_make\n\t"
// Mark that we don't need executable stack.
".section .note.GNU-stack,\"\",%progbits\n\t"

);

asm(
".text\n\t"
".align  4\n\t"
".global sky_context_jump\n\t"
".type   sky_context_jump, %function\n\t"
"sky_context_jump:\n\t"
// prepare stack
"   save	%sp, -"CC64FSZ", %sp\n\t"

// store framepointer and return address in slots reserved
// for arguments
"   stx %fp, [%sp + "BIAS" + "SP"]\n\t"
"   stx %i7, [%sp + "BIAS" + "I7"]\n\t"
"   mov %sp, %o0\n\t"
// force flush register windows to stack and with that save context
"   flushw\n\t"
// get SP (pointing to new context-data) from %i0 param
"   mov %i0, %sp\n\t"
// load framepointer and return address from context
"   ldx [%sp + "BIAS" + "SP"], %fp\n\t"
"   ldx [%sp + "BIAS" + "I7"], %i7\n\t"

"   ret\n\t"
"   restore %o0, %g0, %o0\n\t"
// restore old %sp (pointing to old context-data) in %o0
// *data stored in %o1 was not modified
".size	sky_context_jump,.-sky_context_jump\n\t"
// Mark that we don't need executable stack.
".section .note.GNU-stack,\"\",%progbits\n\t"

);

#endif