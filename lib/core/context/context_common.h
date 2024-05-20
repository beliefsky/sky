//
// Created by weijing on 2024/5/6.
//

#ifndef SKY_CONTEXT_COMMON_H
#define SKY_CONTEXT_COMMON_H

#include <core/context.h>

#if defined(__MACH__)
#define ASM_SYMBOL(name_) "_" #name_
#else
#define ASM_SYMBOL(name_) #name_
#endif

#define ASM_ROUTINE(name_) \
    ".globl " ASM_SYMBOL(name_) "\n\t" \
    ASM_SYMBOL(name_) ":\n\t"


#if defined(__ELF__) || (defined(TB_COMPILER_IS_TINYC) && !defined(TCC_ARM_PE))
#define ARCH_ELF
#endif

#endif //SKY_CONTEXT_COMMON_H
