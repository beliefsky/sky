//
// Created by weijing on 2024/5/31.
//

#if defined (__riscv_xlen) && (__riscv_xlen == 64) && defined(__ELF__)

asm(
".text\n\t"
".align  1\n\t"
".global sky_context_make\n\t"
".type   sky_context_make, %function\n\t"
"sky_context_make:\n\t"
// shift address in a0 (allocated stack) to lower 16 byte boundary
"   andi a0, a0, ~0xF\n\t"

// reserve space for context-data on context-stack
"   addi  a0, a0, -0xd0\n\t"

// third arg of sky_context_make() == address of context-function
// store address as a PC to jump in
"   sd  a2, 0xc8(a0)\n\t"

// save address of finish as return-address for context-function
// will be entered after context-function returns (RA register)
"   lla  a4, finish\n\t"
"   sd  a4, 0xc0(a0)\n\t"

"   ret\n\t" // return pointer to context-data (a0)

"finish:\n\t"
// exit code is zero
"   li  a0, 0\n\t"
// exit application
"   tail  _exit@plt\n\t"

".size   sky_context_make,.-sky_context_make\n\t"
// Mark that we don't need executable stack.
".section .note.GNU-stack,\"\",%progbits\n\t"
);

asm(
".text\n\t"
".align  1\n\t"
".global sky_context_jump\n\t"
".type   sky_context_jump, %function\n\t"
"sky_context_jump:\n\t"
// prepare stack for GP + FPU
"   addi  sp, sp, -0xd0\n\t"

// save fs0 - fs11
"   fsd  fs0, 0x00(sp)\n\t"
"   fsd  fs1, 0x08(sp)\n\t"
"   fsd  fs2, 0x10(sp)\n\t"
"   fsd  fs3, 0x18(sp)\n\t"
"   fsd  fs4, 0x20(sp)\n\t"
"   fsd  fs5, 0x28(sp)\n\t"
"   fsd  fs6, 0x30(sp)\n\t"
"   fsd  fs7, 0x38(sp)\n\t"
"   fsd  fs8, 0x40(sp)\n\t"
"   fsd  fs9, 0x48(sp)\n\t"
"   fsd  fs10, 0x50(sp)\n\t"
"   fsd  fs11, 0x58(sp)\n\t"

// save s0-s11, ra
"   sd  s0, 0x60(sp)\n\t"
"   sd  s1, 0x68(sp)\n\t"
"   sd  s2, 0x70(sp)\n\t"
"   sd  s3, 0x78(sp)\n\t"
"   sd  s4, 0x80(sp)\n\t"
"   sd  s5, 0x88(sp)\n\t"
"   sd  s6, 0x90(sp)\n\t"
"   sd  s7, 0x98(sp)\n\t"
"   sd  s8, 0xa0(sp)\n\t"
"   sd  s9, 0xa8(sp)\n\t"
"   sd  s10, 0xb0(sp)\n\t"
"   sd  s11, 0xb8(sp)\n\t"
"   sd  ra, 0xc0(sp)\n\t"

// save RA as PC
"   sd  ra, 0xc8(sp)\n\t"

// store SP (pointing to context-data) in A2
"   mv  a2, sp\n\t"

// restore SP (pointing to context-data) from A0
"   mv  sp, a0\n\t"

// load fs0 - fs11
"   fld  fs0, 0x00(sp)\n\t"
"   fld  fs1, 0x08(sp)\n\t"
"   fld  fs2, 0x10(sp)\n\t"
"   fld  fs3, 0x18(sp)\n\t"
"   fld  fs4, 0x20(sp)\n\t"
"   fld  fs5, 0x28(sp)\n\t"
"   fld  fs6, 0x30(sp)\n\t"
"   fld  fs7, 0x38(sp)\n\t"
"   fld  fs8, 0x40(sp)\n\t"
"   fld  fs9, 0x48(sp)\n\t"
"   fld  fs10, 0x50(sp)\n\t"
"   fld  fs11, 0x58(sp)\n\t"

// load s0-s11,ra
"   ld  s0, 0x60(sp)\n\t"
"   ld  s1, 0x68(sp)\n\t"
"   ld  s2, 0x70(sp)\n\t"
"   ld  s3, 0x78(sp)\n\t"
"   ld  s4, 0x80(sp)\n\t"
"   ld  s5, 0x88(sp)\n\t"
"   ld  s6, 0x90(sp)\n\t"
"   ld  s7, 0x98(sp)\n\t"
"   ld  s8, 0xa0(sp)\n\t"
"   ld  s9, 0xa8(sp)\n\t"
"   ld  s10, 0xb0(sp)\n\t"
"   ld  s11, 0xb8(sp)\n\t"
"   ld  ra, 0xc0(sp)\n\t"

// return transfer_t from jump
// pass transfer_t as first arg in context function
// a0 == FCTX, a1 == DATA
"   mv a0, a2\n\t"

// load pc
"   ld  a2, 0xc8(sp)\n\t"

// restore stack from GP + FPU
"   addi  sp, sp, 0xd0\n\t"

"   jr a2\n\t"
".size   sky_context_jump,.-sky_context_jump\n\t"
// Mark that we don't need executable stack.
".section .note.GNU-stack,\"\",%progbits\n\t"
);

#endif
