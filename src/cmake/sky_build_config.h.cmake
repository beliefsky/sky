

#ifndef SKY_BUILD_CONFIG_H
#define SKY_BUILD_CONFIG_H

/* API available in Glibc/Linux, but possibly not elsewhere */
#cmakedefine SKY_HAVE_ACCEPT4
#cmakedefine SKY_HAVE_BUILTIN_BSWAP
#cmakedefine SKY_HAVE_EPOLL
#cmakedefine SKY_HAVE_KQUEUE
#cmakedefine SKY_HAVE_MALLOC
#cmakedefine SKY_HAVE_ATOMIC

#cmakedefine SKY_HAVE_EVENT_FD

/* Compiler builtins for specific CPU instruction support */
#cmakedefine SKY_HAVE_BUILTIN_IA32_CRC32
#cmakedefine SKY_HAVE_BUILTIN_MUL_OVERFLOW
#cmakedefine SKY_HAVE_BUILTIN_ADD_OVERFLOW
#cmakedefine SKY_HAVE_BUILTIN_FPCLASSIFY

/* C11 _Static_assert() */
#cmakedefine SKY_HAVE_STATIC_ASSERT

#cmakedefine SKY_HAVE_LIBUCONTEXT
#cmakedefine SKY_HAVE_OPENSSL

#endif

