//
// Created by weijing on 18-3-12.
//

#include <stdlib.h>
#include <core/coro.h>
#include <core/memory.h>
#include <core/log.h>

#define CORO_DEFAULT_STACK_SIZE 14336
#define PAGE_SIZE 2048

#if defined(__MACH__)
#define ASM_SYMBOL(name_) "_" #name_
#else
#define ASM_SYMBOL(name_) #name_
#endif

#define ASM_ROUTINE(name_)                                      \
    ".globl " ASM_SYMBOL(name_) "\n\t" ASM_SYMBOL(name_) ":\n\t"

#if defined(__x86_64__)

typedef sky_usize_t sky_coro_context_t[10];

#elif defined(__aarch64__)

typedef sky_usize_t coro_context[22];

#elif defined(SKY_HAVE_LIBUCONTEXT)

#include <libucontext/libucontext.h>

#define sky_getcontext libucontext_getcontext
#define sky_makecontext libucontext_makecontext
#define sky_swapcontext libucontext_swapcontext

typedef libucontext_ucontext_t sky_coro_context_t;
#else
#error Unsupported platform.
#endif

typedef struct coro_block_s coro_block_t;

typedef struct {
    sky_coro_context_t caller;
    sky_coro_t *current;
} coro_switcher_t;

struct coro_block_s {
    coro_block_t *next;
};


struct sky_coro_s {
    sky_coro_context_t context;
    sky_usize_t yield_value;
//===================================
    sky_coro_t *parent;
    coro_block_t *block;
    sky_uchar_t *ptr;
    sky_usize_t ptr_size;

    sky_uchar_t *stack;
    sky_usize_t stack_size;
};

static sky_usize_t coro_resume(sky_coro_t *coro);

static sky_usize_t coro_yield(sky_coro_t *coro, sky_usize_t value);

static void mem_block_add(sky_coro_t *coro);


static sky_thread coro_switcher_t thread_switcher = {
        .current = null
};


#if defined(__x86_64__)

void __attribute__((noinline, visibility("internal")))
coro_swapcontext(sky_coro_context_t *current, sky_coro_context_t *other);

asm(
        ".text\n\t"
        ".p2align 5\n\t"
        ASM_ROUTINE(coro_swapcontext)
        "movq    %rbx,0(%rdi)\n\t"
        "movq    %rbp,8(%rdi)\n\t"
        "movq    %r12,16(%rdi)\n\t"
        "movq    %r13,24(%rdi)\n\t"
        "movq    %r14,32(%rdi)\n\t"
        "movq    %r15,40(%rdi)\n\t"
        "movq    %rdi,48(%rdi)\n\t"
        "movq    %rsi,56(%rdi)\n\t"
        "movq    (%rsp),%rcx\n\t"
        "movq    %rcx,64(%rdi)\n\t"
        "leaq    0x8(%rsp),%rcx\n\t"
        "movq    %rcx,72(%rdi)\n\t"
        "movq    72(%rsi),%rsp\n\t"
        "movq    0(%rsi),%rbx\n\t"
        "movq    8(%rsi),%rbp\n\t"
        "movq    16(%rsi),%r12\n\t"
        "movq    24(%rsi),%r13\n\t"
        "movq    32(%rsi),%r14\n\t"
        "movq    40(%rsi),%r15\n\t"
        "movq    48(%rsi),%rdi\n\t"
        "movq    64(%rsi),%rcx\n\t"
        "movq    56(%rsi),%rsi\n\t"
        "jmpq    *%rcx\n\t");

#elif defined(__aarch64__)

void __attribute__((noinline, visibility("internal")))
coro_swapcontext(sky_coro_context_t *current, sky_coro_context_t *other);

asm(".text\n\t"
    ".p2align 5\n\t"
    ASM_ROUTINE(coro_swapcontext)
    "mov x10, sp\n\t"
    "mov x11, x30\n\t"
    "stp x8, x9, [x0, #(1*16)]\n\t"
    "stp x10, x11, [x0, #(2*16)]\n\t"
    "stp x12, x13, [x0, #(3*16)]\n\t"
    "stp x14, x15, [x0, #(4*16)]\n\t"
    "stp x19, x20, [x0, #(5*16)]\n\t"
    "stp x21, x22, [x0, #(6*16)]\n\t"
    "stp x23, x24, [x0, #(7*16)]\n\t"
    "stp x25, x26, [x0, #(8*16)]\n\t"
    "stp x27, x28, [x0, #(9*16)]\n\t"
    "stp x29, x30, [x0, #(10*16)]\n\t"
    "stp x0, x1, [x0, #(0*16)]\n\t"
    "ldp x8, x9, [x1, #(1*16)]\n\t"
    "ldp x10, x11, [x1, #(2*16)]\n\t"
    "ldp x12, x13, [x1, #(3*16)]\n\t"
    "ldp x14, x15, [x1, #(4*16)]\n\t"
    "ldp x19, x20, [x1, #(5*16)]\n\t"
    "ldp x21, x22, [x1, #(6*16)]\n\t"
    "ldp x23, x24, [x1, #(7*16)]\n\t"
    "ldp x25, x26, [x1, #(8*16)]\n\t"
    "ldp x27, x28, [x1, #(9*16)]\n\t"
    "ldp x29, x30, [x1, #(10*16)]\n\t"
    "ldp x0, x1, [x1, #(0*16)]\n\t"
    "mov sp, x10\n\t"
    "br x11\n\t");

#elif defined(SKY_HAVE_LIBUCONTEXT)
#define coro_swapcontext(cur, oth) sky_swapcontext(cur, oth)
#else
#error Unsupported platform.
#endif

__attribute__((used, visibility("internal"))) void
coro_entry_point(sky_coro_t *const coro, const sky_coro_func_t func, void *const data) {
    return (void) coro_yield(coro, func(coro, data));
}

#ifdef __x86_64__

void __attribute__((visibility("internal"))) coro_entry_point_x86_64();

asm(".text\n\t"
    ".p2align 5\n\t"
    ASM_ROUTINE(coro_entry_point_x86_64)
    "mov %r15, %rdx\n\t"
    "jmp " ASM_SYMBOL(coro_entry_point) "\n\t"
        );

#elif defined(__aarch64__)

void __attribute__((visibility("internal"))) coro_entry_point_arm64();

asm(".text\n\t"
    ".p2align 5\n\t"
    ASM_ROUTINE(coro_entry_point_arm64)
    "mov x2, x28\n\t"
    "bl " ASM_SYMBOL(coro_entry_point) "\n\t"
);

#endif


sky_api sky_coro_t *
sky_coro_create(const sky_coro_func_t func, void *const data) {
    return sky_coro_create_with_stack(func, data, CORO_DEFAULT_STACK_SIZE);
}


sky_api sky_coro_t *
sky_coro_create_with_stack(sky_coro_func_t func, void *data, const sky_usize_t stack_size) {
    sky_coro_t *const coro = sky_coro_new_with_stack(stack_size);
    if (sky_unlikely(!coro)) {
        return null;
    }

    sky_coro_set(coro, func, data);

    return coro;
}

sky_api sky_coro_t *
sky_coro_new() {
    return sky_coro_new_with_stack(CORO_DEFAULT_STACK_SIZE);
}

sky_api sky_coro_t *
sky_coro_new_with_stack(sky_usize_t stack_size) {
    stack_size = (stack_size + SKY_USIZE(63)) & ~SKY_USIZE(63); // 64字节对齐

    sky_uchar_t *ptr = sky_malloc(stack_size + PAGE_SIZE);
    if (sky_unlikely(!ptr)) {
        return null;
    }
    sky_coro_t *const coro = (sky_coro_t *) ptr;
    ptr += sizeof(sky_coro_t);
    coro->block = null;
    coro->ptr = ptr;
    coro->ptr_size = PAGE_SIZE - sizeof(sky_coro_t) - 16;
    ptr += coro->ptr_size + 16;
    coro->stack = ptr;
    coro->stack_size = stack_size;


    return coro;
}

sky_api void
sky_coro_set(sky_coro_t *const coro, const sky_coro_func_t func, void *const data) {
    sky_uchar_t *stack = coro->stack;

#if defined(__x86_64__)

    coro->context[5 /* R15 */] = (sky_usize_t) data;
    coro->context[6 /* RDI */] = (sky_usize_t) coro;
    coro->context[7 /* RSI */] = (sky_usize_t) func;
    coro->context[8 /* RIP */] = (sky_usize_t) coro_entry_point_x86_64;

    const sky_usize_t rsp = (sky_usize_t) stack + coro->stack_size;
#define STACK_PTR 9
    coro->context[STACK_PTR /* RSP */] = (rsp & ~SKY_USIZE(0xF)) - SKY_USIZE(0x8);

#elif defined(__aarch64__)
    coro->context[19/* x28 */] = (sky_usize_t)data;
    coro->context[0 /* x0  */] = (sky_usize_t)coro;
    coro->context[1 /* x1  */] = (sky_usize_t)func;
    coro->context[5 /* lr  */] = (sky_usize_t)coro_entry_point_arm64;

    const sky_usize_t rsp = (sky_usize_t)stack + coro->stack_size;
#define STACK_PTR 4
    coro->context[STACK_PTR] = rsp & ~SKY_USIZE(0xF);

#elif defined(SKY_HAVE_LIBUCONTEXT)
    sky_getcontext(&coro->context);
    coro->context.uc_stack.ss_sp = stack;
    coro->context.uc_stack.ss_size = coro->stack_size;
    coro->context.uc_stack.ss_flags = 0;
    coro->context.uc_link = null;

    sky_makecontext(&coro->context, coro_entry_point, 3, coro, func, data);

#endif
}


sky_api sky_usize_t
sky_coro_resume(sky_coro_t *const coro) {
#ifdef STACK_PTR
    if (sky_unlikely(coro->context[STACK_PTR] > (sky_usize_t) (coro->stack + coro->stack_size))) {
        sky_log_error("sky_coro_resume out of stack");
        abort();
    }
#endif
    return coro_resume(coro);
}

sky_api sky_usize_t
sky_coro_resume_value(sky_coro_t *const coro, const sky_usize_t value) {
#ifdef STACK_PTR
    if (sky_unlikely(coro->context[STACK_PTR] > (sky_usize_t) (coro->stack + coro->stack_size))) {
        sky_log_error("sky_coro_resume out of stack");
        abort();
    }
#endif
    coro->yield_value = value;

    return coro_resume(coro);
}

sky_api sky_usize_t
sky_coro_yield(const sky_usize_t value) {
    sky_coro_t *const current = thread_switcher.current;
    if (sky_unlikely(!current)) {
        sky_log_error("coro not run");
        __builtin_unreachable();
    }

    return coro_yield(current, value);
}

sky_api sky_coro_t *
sky_coro_current() {
    return thread_switcher.current;
}

sky_api void
sky_coro_destroy(sky_coro_t *const coro) {
    for (coro_block_t *block = coro->block; block; block = block->next) {
        sky_free(block);
    }
    sky_free(coro);
}


sky_api void *
sky_coro_malloc(sky_coro_t *const coro, const sky_u32_t size) {
    if (sky_unlikely(coro->ptr_size < size)) {
        if (sky_unlikely(size > 512)) {
            coro_block_t *const block = sky_malloc(size + sizeof(coro_block_t));
            block->next = coro->block;
            coro->block = block;

            return (sky_uchar_t *) (block + 1);
        }
        mem_block_add(coro);
    }
    sky_uchar_t *const ptr = coro->ptr;
    coro->ptr += size;
    coro->ptr_size -= size;

    return ptr;
}

static sky_inline sky_usize_t
coro_resume(sky_coro_t *const coro) {
    coro_switcher_t *const switcher = &thread_switcher;

    coro->parent = switcher->current;
    switcher->current = coro;
    if (!coro->parent) {
        coro_swapcontext(&switcher->caller, &coro->context);
        switcher->current = null;
    } else {
        coro_swapcontext(&coro->parent->context, &coro->context);
        switcher->current = coro->parent;
    }

    return coro->yield_value;
}

static sky_inline sky_usize_t
coro_yield(sky_coro_t *const coro, const sky_usize_t value) {
    coro->yield_value = value;

    if (!coro->parent) {
        coro_swapcontext(&coro->context, &thread_switcher.caller);
    } else {
        coro_swapcontext(&coro->context, &coro->parent->context);
    }


    return coro->yield_value;
}

static sky_inline void
mem_block_add(sky_coro_t *const coro) {
    coro_block_t *const block = sky_malloc(PAGE_SIZE);
    block->next = coro->block;
    coro->block = block;

    coro->ptr = (sky_uchar_t *) (block + 1);
    coro->ptr_size = PAGE_SIZE - sizeof(coro_block_t);
}