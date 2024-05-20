//
// Created by weijing on 2024/5/20.
//
#ifdef __mips__

#include "./context_common.h"

asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_make)
#ifdef __PIC__
    ".set    noreorder\n\t"
    ".cpload $t9\n\t"
    ".set    reorder\n\t"
#endif

    // save the stack top to v0
    "addu $v0, $a0, $a1\n\t"

    // reserve space for arguments(a0-a3) of context-function
    "addiu $v0, $v0, -32\n\t"

    // 16-align of the stack top address
    "move $v1, $v0\n\t"
    "li $v0, -16\n\t"
    "and $v0, $v1, $v0\n\t"

    /* reserve space for context-data on context-stack
     *
     * 64 = align8(60)
     */
    "addiu $v0, $v0, -64\n\t"

    // context.pc = func
    "sw $a2, 44($v0)\n\t"

    // context.gp = global pointer
    "sw $gp, 48($v0)\n\t"

    /* init retval = a writeable space (context)
     *
     * it will write retval(context, priv) when jump to a new context function entry first
     */
    "addiu $t0, $v0, 52\n\t"
    "sw $t0, 36($v0)\n\t"

    // context.ra = address of label __end
    "la $t9, __end\n\t"
    "sw $t9, 40($v0)\n\t"

    // return pointer to context-data
    "jr $ra\n\t"

    "__end:\n\t"

    // allocate stack frame space and save return address
    "addiu $sp, $sp, -32\n\t"
    "sw $ra, 28($sp)\n\t"

    // exit(0)
    "move  $a0, $zero\n\t"
    "lw $t9, %call16(_exit)($gp)\n\t"
    "jalr $t9\n\t"
);


asm(".text\n\t"
    ".p2align 4\n\t"
    ASM_ROUTINE(sky_context_jump)
    // reserve stack space first
    "addiu $sp, $sp, -64\n\t"

    // save registers and construct the current context
    "sw $s0, ($sp)\n\t"
    "sw $s1, 4($sp)\n\t"
    "sw $s2, 8($sp)\n\t"
    "sw $s3, 12($sp)\n\t"
    "sw $s4, 16($sp)\n\t"
    "sw $s5, 20($sp)\n\t"
    "sw $s6, 24($sp)\n\t"
    "sw $s7, 28($sp)\n\t"
    "sw $fp, 32($sp)\n\t"
    "sw $a0, 36($sp)\n\t"     // save retval
    "sw $ra, 40($sp)\n\t"
    "sw $ra, 44($sp)\n\t"     // save ra as pc
    "sw $gp, 48($sp)\n\t"     // save gp

    // save the old context(sp) to a0
    "move $a0, $sp\n\t"

    // switch to the new context(sp) and stack
    "move $sp, $a1\n\t"

    // restore registers of the new context
    "lw $s0, ($sp)\n\t"
    "lw $s1, 4($sp)\n\t"
    "lw $s2, 8($sp)\n\t"
    "lw $s3, 12($sp)\n\t"
    "lw $s4, 16($sp)\n\t"
    "lw $s5, 20($sp)\n\t"
    "lw $s6, 24($sp)\n\t"
    "lw $s7, 28($sp)\n\t"
    "lw $fp, 32($sp)\n\t"
    "lw $t0, 36($sp)\n\t"     // load retval
    "lw $ra, 40($sp)\n\t"
    "lw $t9, 44($sp)\n\t"    // load t9 = pc
    "lw $gp, 48($sp)\n\t"     // load gp

    // restore stack space
    "addiu $sp, $sp, 64\n\t"

    // return from-context(context: a0, priv: a1) from jump
    "sw $a0, ($t0)\n\t"
    "sw $a2, 4($t0)\n\t"

    // pass old-context(context: a0, priv: a1) arguments to the context function
    "move $a1, $a2\n\t"

    /* jump to the return or entry address(pc)
     *
     *             ----------------------
     * context:   |   args   |  padding  |
     *             ----------------------
     *            0
     *            |
     *            sp
     */
    "jr $t9\n\t"
);

#endif

