//
// Created by weijing on 18-3-12.
//

#include <assert.h>
#include "coro.h"

#if !defined(SIGSTKSZ)
#define SIGSTKSZ 16384
#endif

#ifndef CORO_STACK_MIN
#define CORO_STACK_MIN  SIGSTKSZ
#endif

#if defined(__x86_64__)
typedef uintptr_t sky_coro_context_t[10];
#elif defined(__i386__)
typedef uintptr_t sky_coro_context_t[7];
#else
#include <ucontext.h>
typedef ucontext_t sky_coro_context_t;
#endif

#if defined(__APPLE__)
#define ASM_SYMBOL(name_) "_" #name_
#else
#define ASM_SYMBOL(name_) #name_
#endif

#define ASM_ROUTINE(name_)                                      \
    ".globl " ASM_SYMBOL(name_) "\n\t" ASM_SYMBOL(name_) ":\n\t"

struct sky_coro_switcher_s {
    sky_coro_context_t caller;
};

struct sky_defer_s {
    union {
        struct {
            sky_defer_func_t func;
        };
        struct {
            sky_defer_func2_t func2;
            sky_uintptr_t data2;
        };
    };
    sky_uintptr_t data;
    sky_defer_t *prev;
    sky_defer_t *next;

    sky_bool_t free:1;
    sky_bool_t one_arg:1;
};

struct sky_coro_s {
    sky_coro_switcher_t *switcher;
    sky_coro_context_t context;
    sky_int32_t yield_value;

    sky_pool_t *pool;
    sky_defer_t defers;
    sky_defer_t free_defers;

    sky_uchar_t stack[] __attribute__((aligned(64)));
};

static void coro_set(sky_coro_t *coro, sky_uintptr_t func, sky_uintptr_t data);

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
#elif defined(__i386__)
void __attribute__((noinline, visibility("internal")))
coro_swapcontext(sky_coro_context_t *current, sky_coro_context_t *other);
    asm(
    ".text\n\t"
    ".p2align 5\n\t"
    ASM_ROUTINE(coro_swapcontext)
    "movl   0x4(%esp),%eax\n\t"
    "movl   %ecx,0x1c(%eax)\n\t" /* ECX */
    "movl   %ebx,0x0(%eax)\n\t"  /* EBX */
    "movl   %esi,0x4(%eax)\n\t"  /* ESI */
    "movl   %edi,0x8(%eax)\n\t"  /* EDI */
    "movl   %ebp,0xc(%eax)\n\t"  /* EBP */
    "movl   (%esp),%ecx\n\t"
    "movl   %ecx,0x14(%eax)\n\t" /* EIP */
    "leal   0x4(%esp),%ecx\n\t"
    "movl   %ecx,0x18(%eax)\n\t" /* ESP */
    "movl   8(%esp),%eax\n\t"
    "movl   0x14(%eax),%ecx\n\t" /* EIP (1) */
    "movl   0x18(%eax),%esp\n\t" /* ESP */
    "pushl  %ecx\n\t"            /* EIP (2) */
    "movl   0x0(%eax),%ebx\n\t"  /* EBX */
    "movl   0x4(%eax),%esi\n\t"  /* ESI */
    "movl   0x8(%eax),%edi\n\t"  /* EDI */
    "movl   0xc(%eax),%ebp\n\t"  /* EBP */
    "movl   0x1c(%eax),%ecx\n\t" /* ECX */
    "ret\n\t");
#else
#define coro_swapcontext(cur,oth) swapcontext(cur, oth)
#endif

__attribute__((used, visibility("internal"))) void
coro_entry_point(sky_coro_t *coro, sky_coro_func_t func, sky_uintptr_t data) {
    sky_coro_yield(coro, func(coro, data));
}

#ifdef __x86_64__

/* See comment in coro_reset() for an explanation of why this routine is
 * necessary. */
void __attribute__((visibility("internal"))) coro_entry_point_x86_64();

asm(".text\n\t"
    ".p2align 5\n\t"
    ASM_ROUTINE(coro_entry_point_x86_64)
    "mov %r15, %rdx\n\t"
    "jmp " ASM_SYMBOL(coro_entry_point) "\n\t"
);
#endif

sky_coro_switcher_t *
sky_coro_switcher_create(sky_pool_t *pool) {
    return sky_palloc(pool, sizeof(sky_coro_switcher_t));
}

sky_coro_t *
sky_coro_create(sky_coro_switcher_t *switcher, sky_coro_func_t func, sky_uintptr_t data) {
    sky_pool_t *pool;
    sky_coro_t *coro;

    pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);

    coro = sky_palloc(pool, CORO_STACK_MIN + sizeof(sky_coro_t));
    coro->switcher = switcher;
    coro->pool = pool;
    coro->defers.prev = coro->defers.next = &coro->defers;
    coro->free_defers.prev = coro->free_defers.next = &coro->free_defers;
    coro_set(coro, (sky_uintptr_t) func, data);
    return coro;

}

sky_coro_t *
sky_coro_create2(sky_coro_switcher_t *switcher, sky_coro_func_t func, sky_uintptr_t *data_ptr, sky_size_t size) {
    sky_pool_t *pool;
    sky_coro_t *coro;

    pool = sky_create_pool(SKY_DEFAULT_POOL_SIZE);

    coro = sky_palloc(pool, CORO_STACK_MIN + sizeof(sky_coro_t));
    coro->switcher = switcher;
    coro->pool = pool;
    coro->defers.prev = coro->defers.next = &coro->defers;
    coro->free_defers.prev = coro->free_defers.next = &coro->free_defers;

    coro_set(coro, (sky_uintptr_t) func, *data_ptr = (sky_uintptr_t) sky_palloc(coro->pool, size));
    return coro;
}


sky_int32_t
sky_coro_resume(sky_coro_t *coro) {
#if defined(STACK_PTR)
    assert(coro->context[STACK_PTR] >= (sky_uintptr_t) coro->stack &&
           coro->context[STACK_PTR] <= (sky_uintptr_t) (coro->stack + CORO_STACK_MIN));
#endif
    coro_swapcontext(&coro->switcher->caller, &coro->context);
    return coro->yield_value;
}

sky_int32_t
sky_coro_yield(sky_coro_t *coro, sky_int32_t value) {
    coro->yield_value = value;
    coro_swapcontext(&coro->context, &coro->switcher->caller);
    return coro->yield_value;
}

sky_defer_t *
sky_defer_add(sky_coro_t *coro, sky_defer_func_t func, sky_uintptr_t data) {
    sky_defer_t *defer;
    if ((defer = coro->free_defers.next) != &coro->free_defers) {
        defer->prev->next = defer->next;
        defer->next->prev = defer->prev;
    } else {
        defer = sky_palloc(coro->pool, sizeof(sky_defer_t));
    }
    defer->func = func;
    defer->data = data;
    defer->one_arg = true;
    defer->free = false;
    defer->prev = &coro->defers;
    defer->next = defer->prev->next;
    defer->next->prev = defer->prev->next = defer;

    return defer;
}

sky_defer_t *
sky_defer_add2(sky_coro_t *coro, sky_defer_func2_t func, sky_uintptr_t data1, sky_uintptr_t data2) {
    sky_defer_t *defer;
    if ((defer = coro->free_defers.next) != &coro->free_defers) {
        defer->prev->next = defer->next;
        defer->next->prev = defer->prev;
    } else {
        defer = sky_palloc(coro->pool, sizeof(sky_defer_t));
    }
    defer->func2 = func;
    defer->data = data1;
    defer->data2 = data2;
    defer->one_arg = false;
    defer->free = false;
    defer->prev = &coro->defers;
    defer->next = defer->prev->next;
    defer->next->prev = defer->prev->next = defer;

    return defer;
}

void
sky_defer_remove(sky_coro_t *coro, sky_defer_t *defer) {
    if (defer->free) {
        return;
    }
    defer->prev->next = defer->next;
    defer->next->prev = defer->prev;
    defer->free = true;

    defer->prev = &coro->free_defers;
    defer->next = defer->prev->next;
    defer->next->prev = defer->prev->next = defer;
}

void sky_defer_run(sky_coro_t *coro) {
    sky_defer_t *defer;

    while ((defer = coro->defers.next) != &coro->defers) {
        defer->prev->next = defer->next;
        defer->next->prev = defer->prev;
        defer->free = true;
        if (defer->one_arg) {
            defer->func(defer->data);
        } else {
            defer->func2(defer->data, defer->data2);
        }

        defer->prev = &coro->free_defers;
        defer->next = defer->prev->next;
        defer->next->prev = defer->prev->next = defer;
    }
}

void sky_coro_destroy(sky_coro_t *coro) {
    sky_defer_t *defer;

    while ((defer = coro->defers.next) != &coro->defers) {
        defer->prev->next = defer->next;
        defer->next->prev = defer->prev;
        if (defer->one_arg) {
            defer->func(defer->data);
        } else {
            defer->func2(defer->data, defer->data2);
        }
    }
    sky_destroy_pool(coro->pool);
}

sky_inline sky_pool_t *
sky_coro_pool_get(sky_coro_t *coro) {
    return coro->pool;
}

static sky_inline void
coro_set(sky_coro_t *coro, sky_uintptr_t func, sky_uintptr_t data) {
#if defined(__x86_64__)
    coro->context[5 /* R15 */] = (sky_uintptr_t) data;
    coro->context[6 /* RDI */] = (sky_uintptr_t) coro;
    coro->context[7 /* RSI */] = (sky_uintptr_t) func;
    coro->context[8 /* RIP */] = (sky_uintptr_t) coro_entry_point_x86_64;
#define STACK_PTR 9
    coro->context[STACK_PTR /* RSP */] = (((sky_uintptr_t) coro->stack + CORO_STACK_MIN) & ~0xful) - 0x8ul;
#elif defined(__i386__)
    sky_uchar_t *stack = (sky_uchar_t *) (sky_uintptr_t)(coro->stack + CORO_STACK_MIN);
    stack = (sky_uchar_t *)((sky_uintptr_t)(stack - (3 * sizeof(sky_uintptr_t))) & (sky_uintptr_t)~0x3);

    sky_uintptr_t *argp = (sky_uintptr_t *)stack;
    *argp++ = 0;
    *argp++ = (sky_uintptr_t) coro;
    *argp++ = (sky_uintptr_t) func;
    *argp = (sky_uintptr_t) data;

    coro->context[5 /* EIP */] = (sky_uintptr_t) coro_entry_point;
#define STACK_PTR 6
    coro->context[STACK_PTR /* ESP */] = (sky_uintptr_t) stack;
#else
    getcontext(&coro->context);
    coro->context.uc_stack.ss_sp = coro->stack;
    coro->context.uc_stack.ss_size = CORO_STACK_MIN;
    coro->context.uc_stack.ss_flags = 0;
    coro->context.uc_link = null;

    makecontext(&coro->context, coro_entry_point, 3, coro, func, data);
#endif
}