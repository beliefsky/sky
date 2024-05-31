//
// Created by weijing on 2024/5/31.
//
#if defined(__s390x__) && defined(__ELF__)

#define GR_OFFSET          "16"
#define R14_OFFSET         "88"
#define FP_OFFSET          "96"
#define FPC_OFFSET         "160"
#define PC_OFFSET          "168"
#define CONTEXT_SIZE       "176"

#define REG_SAVE_AREA_SIZE "160"

asm(
".text\n\t"
".align	8\n\t"
".global	sky_context_make\n\t"
".type	sky_context_make, @function\n\t"
"sky_context_make:\n\t"
"   .machine \"z10\"\n\t"
/* Align the stack to an 8 byte boundary.  */
"   nill    %r2,0xfff8\n\t"

/* Allocate stack space for the context.  */
"   aghi	%r2,-"CONTEXT_SIZE"\n\t"

/* Set the r2 save slot to zero.  This indicates sky_context_jump
   that this is a special context.  */
"   mvghi	"GR_OFFSET"(%r2),0\n\t"

/* Save the floating point control register.  */
"   stfpc	"FPC_OFFSET"(%r2)\n\t"

/* Store the address of the target function as new pc.  */
"   stg	%r4,"PC_OFFSET"(%r2)\n\t"

/* Store a pointer to the finish routine as r14. If a function
   called via context routines just returns that value will be
   loaded and used as return address.  Hence the program will
   just exit.  */
"   larl	%r1,finish\n\t"
"   stg	%r1,"R14_OFFSET"(%r2)\n\t"

/* Return as usual with the new context returned in r2.  */
"   br	%r14\n\t"

"finish:\n\t"
/* In finish tasks, you load the exit code and exit the
   sky_context_make This is called when the context-function is
   entirely executed.  */
"   lghi	%r2,0\n\t"
"   brasl	%r14,_exit@PLT\n\t"


".size   sky_context_make,.-sky_context_make\n\t"
".section .note.GNU-stack,\"\",%progbits\n\t"
);

asm(
".text\n\t"
".align	8\n\t"
".global	sky_context_jump\n\t"
".type	sky_context_jump, @function\n\t"
"sky_context_jump:\n\t"
"   .machine \"z10\"\n\t"
/* Reserve stack space to store the current context.  */
"   aghi	%r15,-"CONTEXT_SIZE"\n\t"

/* Save the argument register holding the location of the return value.  */
"   stg	%r2,"GR_OFFSET"(%r15)\n\t"

/* Save the call-saved general purpose registers.  */
"   stmg	%r6,%r14,"GR_OFFSET"+8(%r15)\n\t"

/* Save call-saved floating point registers.  */
"   std	%f8,"FP_OFFSET"(%r15)\n\t"
"   std	%f9,"FP_OFFSET"+8(%r15)\n\t"
"   std	%f10,"FP_OFFSET"+16(%r15)\n\t"
"   std	%f11,"FP_OFFSET"+24(%r15)\n\t"
"   std	%f12,"FP_OFFSET"+32(%r15)\n\t"
"   std	%f13,"FP_OFFSET"+40(%r15)\n\t"
"   std	%f14,"FP_OFFSET"+48(%r15)\n\t"
"   std	%f15,"FP_OFFSET"+56(%r15)\n\t"

/* Save the return address as current pc.  */
"   stg	%r14,"PC_OFFSET"(%r15)\n\t"

/* Save the floating point control register.  */
"   stfpc	"FPC_OFFSET"(%r15)\n\t"

/* Backup the stack pointer pointing to the old context-data into r1.  */
"   lgr	 %r1,%r15\n\t"

/* Load the new context pointer as stack pointer.  */
"   lgr	%r15,%r3\n\t"

/* Restore the call-saved GPRs from the new context.  */
"   lmg	%r6,%r14,"GR_OFFSET"+8(%r15)\n\t"

/* Restore call-saved floating point registers.  */
"   ld	%f8,"FP_OFFSET"(%r15)\n\t"
"   ld	%f9,"FP_OFFSET"+8(%r15)\n\t"
"   ld	%f10,"FP_OFFSET"+16(%r15)\n\t"
"   ld	%f11,"FP_OFFSET"+24(%r15)\n\t"
"   ld	%f12,"FP_OFFSET"+32(%r15)\n\t"
"   ld	%f13,"FP_OFFSET"+40(%r15)\n\t"
"   ld	%f14,"FP_OFFSET"+48(%r15)\n\t"
"   ld	%f15,"FP_OFFSET"+56(%r15)\n\t"

/* Load the floating point control register.  */
"   lfpc	"FPC_OFFSET"(%r15)\n\t"

/* Restore PC - the location where we will jump to at the end.  */
"   lg	%r5,"PC_OFFSET"(%r15)\n\t"

"   ltg	%r2,"GR_OFFSET"(%r15)\n\t"
"   jnz	use_return_slot\n\t"

/* We're restoring a context created by sky_context_make.
   This is going to be the argument of the entry point
   of the fiber. We're placing it on top of the ABI
   defined register save area of the fiber's own stack. */
"   la	%r2,"REG_SAVE_AREA_SIZE"(%r15)\n\t"

/* REG_SAVE_AREA_SIZE + sizeof(transfer_t) */
"   aghi	%r15,-("REG_SAVE_AREA_SIZE"+16)\n\t"

"use_return_slot:\n\t"
/* Save the two fields in transfer_t.  When calling a
   sky_context_make function this becomes the function argument of
   the target function, otherwise it will be the return value of
   sky_context_jump.  */
"   stg	%r1,0(%r2)\n\t"
"   stg	%r4,8(%r2)\n\t"

/* Free the restored context.  */
"   aghi	%r15,"CONTEXT_SIZE"\n\t"

/* Jump to the PC loaded from the new context.  */
"   br	%r5\n\t"


".size   sky_context_jump,.-sky_context_jump\n\t"
".section .note.GNU-stack,\"\",%progbits\n\t"
);

#endif