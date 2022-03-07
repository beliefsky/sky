

#ifndef SKY_BUILD_CONFIG_H
#define SKY_BUILD_CONFIG_H

/* API available in Glibc/Linux, but possibly not elsewhere */
#cmakedefine HAVE_ACCEPT4
#cmakedefine HAVE_BUILTIN_BSWAP
#cmakedefine HAVE_EPOLL
#cmakedefine HAVE_KQUEUE
#cmakedefine HAVE_MALLOC
#cmakedefine HAVE_ATOMIC

#cmakedefine HAVE_EVENT_FD

/* Compiler builtins for specific CPU instruction support */
#cmakedefine HAVE_BUILTIN_IA32_CRC32
#cmakedefine HAVE_BUILTIN_MUL_OVERFLOW
#cmakedefine HAVE_BUILTIN_ADD_OVERFLOW
#cmakedefine HAVE_BUILTIN_FPCLASSIFY

/* C11 _Static_assert() */
#cmakedefine HAVE_STATIC_ASSERT

#cmakedefine HAVE_LIBUCONTEXT
#cmakedefine HAVE_OPENSSL

#endif

