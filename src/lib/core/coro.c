//
// Created by weijing on 18-3-12.
//

#include <stdlib.h>
#include "coro.h"
#include "queue.h"
#include "memory.h"
#include "log.h"


#if !defined(SIGSTKSZ) || SIGSTKSZ < 8192

#define CORE_BLOCK_SIZE 65536

#else

#define CORE_BLOCK_SIZE SIGSTKSZ

#endif

#define PAGE_SIZE 2048
#define CORO_STACK_MIN (CORE_BLOCK_SIZE - PAGE_SIZE)

#if defined(__MACH__)
#define ASM_SYMBOL(name_) "_" #name_
#else
#define ASM_SYMBOL(name_) #name_
#endif

#define ASM_ROUTINE(name_)                                      \
    ".globl " ASM_SYMBOL(name_) "\n\t" ASM_SYMBOL(name_) ":\n\t"

#if defined(__x86_64__)

typedef sky_usize_t sky_coro_context_t[10];

#elif defined(__i386__)

typedef sky_usize_t sky_coro_context_t[7];

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

struct sky_coro_switcher_s {
    sky_coro_context_t caller;
};

struct coro_block_s {
    coro_block_t *next;
};

struct sky_defer_s {
    sky_queue_t link;
    union {
        struct {
            sky_defer_func_t func;
            void *data;
        } one;
        struct {
            sky_defer_func2_t func;
            void *data1;
            void *data2;
        } two;
    };

    sky_bool_t free: 1;
    sky_bool_t one_arg: 1;
};

struct sky_coro_s {
    sky_coro_switcher_t *switcher;
    sky_coro_context_t context;
    sky_isize_t yield_value;
//===================================
    sky_bool_t self;
    coro_block_t *block;
    sky_uchar_t *ptr;
    sky_usize_t ptr_size;

    sky_queue_t defers;
    sky_queue_t global_defers;
    sky_queue_t free_defers;

    sky_uchar_t stack[];
};

static sky_isize_t coro_yield(sky_coro_t *coro, sky_isize_t value);

static void mem_block_add(sky_coro_t *coro);

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
#elif defined(SKY_HAVE_LIBUCONTEXT)
#define coro_swapcontext(cur, oth) sky_swapcontext(cur, oth)
#else
#error Unsupported platform.
#endif

__attribute__((used, visibility("internal"))) void
coro_entry_point(sky_coro_t *coro, sky_coro_func_t func, void *data) {
    return (void) coro_yield(coro, func(coro, data));
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

static sky_inline void
coro_set(sky_coro_t *coro, sky_coro_func_t func, void *data) {
    sky_uchar_t *stack = coro->stack;

#if defined(__x86_64__)
    const sky_usize_t rsp = (sky_usize_t) stack + CORO_STACK_MIN;

    coro->context[5 /* R15 */] = (sky_usize_t) data;
    coro->context[6 /* RDI */] = (sky_usize_t) coro;
    coro->context[7 /* RSI */] = (sky_usize_t) func;
    coro->context[8 /* RIP */] = (sky_usize_t) coro_entry_point_x86_64;
#define STACK_PTR 9
    coro->context[STACK_PTR /* RSP */] = (rsp & ~SKY_USIZE(0xF)) - SKY_USIZE(0x8);
#elif defined(__i386__)
    stack = (sky_uchar_t *) ((sky_usize_t) ((stack + CORO_STACK_MIN) - (3 * sizeof(sky_usize_t))) & (sky_usize_t) ~0x3);

    sky_usize_t *argp = (sky_usize_t *) stack;
    *argp++ = 0;
    *argp++ = (sky_usize_t) coro;
    *argp++ = (sky_usize_t) func;
    *argp = (sky_usize_t) data;

    coro->context[5 /* EIP */] = (sky_usize_t) coro_entry_point;
#define STACK_PTR 6
    coro->context[STACK_PTR /* ESP */] = (sky_usize_t) stack;

#elif defined(SKY_HAVE_LIBUCONTEXT)
    sky_getcontext(&coro->context);
    coro->context.uc_stack.ss_sp = stack;
    coro->context.uc_stack.ss_size = CORO_STACK_MIN;
    coro->context.uc_stack.ss_flags = 0;
    coro->context.uc_link = null;

    sky_makecontext(&coro->context, coro_entry_point, 3, coro, func, data);

#endif
}

sky_inline sky_usize_t
sky_coro_switcher_size() {
    return sizeof(sky_coro_switcher_t);
}


sky_inline sky_coro_t *
sky_coro_create(sky_coro_switcher_t *switcher, sky_coro_func_t func, void *data) {
    sky_coro_t *coro = sky_coro_new(switcher);
    if (sky_unlikely(!coro)) {
        return null;
    }

    coro_set(coro, func, data);

    return coro;
}

sky_inline sky_coro_t *
sky_coro_new(sky_coro_switcher_t *switcher) {
    if (sky_unlikely(!switcher)) {
        return null;
    }
    sky_coro_t *coro = sky_malloc(CORE_BLOCK_SIZE);
    if (sky_unlikely(!coro)) {
        return null;
    }
    coro->switcher = switcher;

    sky_queue_init(&coro->defers);
    sky_queue_init(&coro->global_defers);
    sky_queue_init(&coro->free_defers);

    coro->self = false;
    coro->block = null;
    coro->ptr = (sky_uchar_t *) (coro + 1) + CORO_STACK_MIN + 16;
    coro->ptr_size = PAGE_SIZE - sizeof(sky_coro_t) - 16;

    return coro;
}

void sky_coro_set(sky_coro_t *coro, sky_coro_func_t func, void *data) {
    if (sky_unlikely(coro->self)) {
        sky_log_error("sky_coro_set shouldn't into coro");
        abort();
    }
    coro_set(coro, func, data);
}


void
sky_coro_reset(sky_coro_t *coro, sky_coro_func_t func, void *data) {
    sky_defer_run(coro);
    coro_set(coro, func, data);
}


sky_inline sky_isize_t
sky_coro_resume(sky_coro_t *coro) {
#ifdef STACK_PTR
    if (sky_unlikely(coro->context[STACK_PTR] > (sky_usize_t) (coro->stack + CORO_STACK_MIN))) {
        sky_log_error("sky_coro_resume out of stack");
        abort();
    }
#endif
    if (sky_unlikely(coro->self)) {
        sky_log_error("sky_coro_resume shouldn't into coro");
        abort();
    }

    coro->self = true;
    coro_swapcontext(&coro->switcher->caller, &coro->context);
    coro->self = false;
    return coro->yield_value;
}

sky_isize_t
sky_coro_resume_value(sky_coro_t *coro, sky_isize_t value) {
    if (sky_unlikely(coro->self)) {
        sky_log_error("sky_coro_resume shouldn't into coro");
        abort();
    }
    coro->yield_value = value;
    coro->self = true;
    coro_swapcontext(&coro->switcher->caller, &coro->context);
    coro->self = false;
    return coro->yield_value;
}

sky_inline sky_isize_t
sky_coro_yield(sky_coro_t *coro, sky_isize_t value) {
    if (sky_unlikely(!coro->self)) {
        sky_log_error("sky_coro_yield shouldn't into coro");
        abort();
    }

    return coro_yield(coro, value);
}

sky_inline sky_coro_switcher_t *
sky_coro_get_switcher(sky_coro_t *coro) {
    return coro->switcher;
}

void
sky_coro_destroy(sky_coro_t *coro) {
    sky_defer_t *defer;
    sky_queue_t *item;
    coro_block_t *block;

    sky_queue_insert_prev_list(&coro->defers, &coro->global_defers);

    sky_queue_iterator_t iterator;
    sky_queue_iterator_init(&iterator, &coro->defers);
    while ((item = sky_queue_iterator_next(&iterator))) {
        defer = sky_queue_data(item, sky_defer_t, link);
        defer->free = true;
        defer->one_arg ? defer->one.func(defer->one.data)
                       : defer->two.func(defer->two.data1, defer->two.data2);
    }

    for (block = coro->block; block; block = block->next) {
        sky_free(block);
    }

    sky_free(coro);
}

sky_defer_t *
sky_defer_add(sky_coro_t *coro, sky_defer_func_t func, void *data) {
    sky_defer_t *defer;
    if (!sky_queue_is_empty(&coro->free_defers)) {
        sky_queue_t *tmp = sky_queue_next(&coro->free_defers);
        sky_queue_remove(tmp);
        defer = sky_queue_data(tmp, sky_defer_t, link);
    } else {
        defer = sky_coro_malloc(coro, sizeof(sky_defer_t));
    }
    defer->one.func = func;
    defer->one.data = data;
    defer->one_arg = true;
    defer->free = false;

    sky_queue_insert_next(&coro->defers, &defer->link);

    return defer;
}

sky_defer_t *
sky_defer_add2(sky_coro_t *coro, sky_defer_func2_t func, void *data1, void *data2) {
    sky_defer_t *defer;
    if (!sky_queue_is_empty(&coro->free_defers)) {
        sky_queue_t *tmp = sky_queue_next(&coro->free_defers);
        sky_queue_remove(tmp);
        defer = sky_queue_data(tmp, sky_defer_t, link);
    } else {
        defer = sky_coro_malloc(coro, sizeof(sky_defer_t));
    }
    defer->two.func = func;
    defer->two.data1 = data1;
    defer->two.data2 = data2;
    defer->one_arg = false;
    defer->free = false;

    sky_queue_insert_next(&coro->defers, &defer->link);

    return defer;
}

sky_defer_t *
sky_defer_global_add(sky_coro_t *coro, sky_defer_func_t func, void *data) {
    sky_defer_t *defer;
    if (!sky_queue_is_empty(&coro->free_defers)) {
        sky_queue_t *tmp = sky_queue_next(&coro->free_defers);
        sky_queue_remove(tmp);
        defer = sky_queue_data(tmp, sky_defer_t, link);
    } else {
        defer = sky_coro_malloc(coro, sizeof(sky_defer_t));
    }
    defer->one.func = func;
    defer->one.data = data;
    defer->one_arg = true;
    defer->free = false;

    sky_queue_insert_next(&coro->global_defers, &defer->link);

    return defer;
}


sky_defer_t *
sky_global_defer_add2(sky_coro_t *coro, sky_defer_func2_t func, void *data1, void *data2) {
    sky_defer_t *defer;
    if (!sky_queue_is_empty(&coro->free_defers)) {
        sky_queue_t *tmp = sky_queue_next(&coro->free_defers);
        sky_queue_remove(tmp);
        defer = sky_queue_data(tmp, sky_defer_t, link);
    } else {
        defer = sky_coro_malloc(coro, sizeof(sky_defer_t));
    }
    defer->two.func = func;
    defer->two.data1 = data1;
    defer->two.data2 = data2;
    defer->one_arg = false;
    defer->free = false;

    sky_queue_insert_next(&coro->global_defers, &defer->link);

    return defer;
}

sky_inline void
sky_defer_cancel(sky_coro_t *coro, sky_defer_t *defer) {
    if (sky_unlikely(defer->free)) {
        return;
    }
    defer->free = true;
    sky_queue_remove(&defer->link);
    sky_queue_insert_next(&coro->free_defers, &defer->link);
}

sky_inline void
sky_defer_remove(sky_coro_t *coro, sky_defer_t *defer) {
    if (sky_unlikely(defer->free)) {
        return;
    }
    defer->free = true;
    sky_queue_remove(&defer->link);
    sky_queue_insert_next(&coro->free_defers, &defer->link);

    defer->one_arg ? defer->one.func(defer->one.data)
                   : defer->two.func(defer->two.data1, defer->two.data2);
}

sky_inline void
sky_defer_run(sky_coro_t *coro) {
    sky_defer_t *defer;
    sky_queue_t *tmp;

    while (!sky_queue_is_empty(&coro->defers)) {
        tmp = sky_queue_next(&coro->defers);
        sky_queue_remove(tmp);
        defer = sky_queue_data(tmp, sky_defer_t, link);
        defer->free = true;
        defer->one_arg ? defer->one.func(defer->one.data)
                       : defer->two.func(defer->two.data1, defer->two.data2);

        sky_queue_insert_next(&coro->free_defers, tmp);
    }
}

sky_inline void *
sky_coro_malloc(sky_coro_t *coro, sky_u32_t size) {
    sky_uchar_t *ptr;
    if (sky_unlikely(coro->ptr_size < size)) {
        if (sky_unlikely(size > 512)) {
            coro_block_t *block = sky_malloc(size + sizeof(coro_block_t));
            block->next = coro->block;
            coro->block = block;

            return (sky_uchar_t *) (block + 1);
        }
        mem_block_add(coro);
    }
    ptr = coro->ptr;
    coro->ptr += size;
    coro->ptr_size -= size;

    return ptr;
}

static sky_inline sky_isize_t
coro_yield(sky_coro_t *coro, sky_isize_t value) {
    coro->yield_value = value;
    coro_swapcontext(&coro->context, &coro->switcher->caller);

    return coro->yield_value;
}

static sky_inline void
mem_block_add(sky_coro_t *coro) {
    coro_block_t *block = sky_malloc(PAGE_SIZE);
    block->next = coro->block;
    coro->block = block;

    coro->ptr = (sky_uchar_t *) (block + 1);
    coro->ptr_size = PAGE_SIZE - sizeof(coro_block_t);
}