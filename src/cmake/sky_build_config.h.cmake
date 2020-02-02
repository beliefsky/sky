

#ifndef SKY_BUILD_CONFIG_H
#define SKY_BUILD_CONFIG_H

/* API available in Glibc/Linux, but possibly not elsewhere */
#cmakedefine HAVE_ACCEPT4
#cmakedefine HAVE_ALLOCA_H
#cmakedefine HAVE_CLOCK_GETTIME
#cmakedefine HAVE_GET_CURRENT_DIR_NAME
#cmakedefine HAVE_GETAUXVAL
#cmakedefine HAVE_MEMPCPY
#cmakedefine HAVE_MEMRCHR
#cmakedefine HAVE_MKOSTEMP
#cmakedefine HAVE_PIPE2
#cmakedefine HAVE_PTHREADBARRIER
#cmakedefine HAVE_RAWMEMCHR
#cmakedefine HAVE_READAHEAD
#cmakedefine HAVE_REALLOCARRAY
#cmakedefine HAVE_EVENTFD
#cmakedefine HAVE_EPOLL
#cmakedefine HAVE_KQUEUE
#cmakedefine HAVE_DLADDR
#cmakedefine HAVE_POSIX_FADVISE
#cmakedefine HAVE_LINUX_CAPABILITY
#cmakedefine HAVE_PTHREAD_SET_NAME_NP
#cmakedefine HAVE_GETENTROPY
#cmakedefine HAVE_FWRITE_UNLOCKED
#cmakedefine HAVE_GETTID

/* Compiler builtins for specific CPU instruction support */
#cmakedefine HAVE_BUILTIN_CLZLL
#cmakedefine HAVE_BUILTIN_CPU_INIT
#cmakedefine HAVE_BUILTIN_IA32_CRC32
#cmakedefine HAVE_BUILTIN_MUL_OVERFLOW
#cmakedefine HAVE_BUILTIN_ADD_OVERFLOW
#cmakedefine HAVE_BUILTIN_FPCLASSIFY

/* C11 _Static_assert() */
#cmakedefine HAVE_STATIC_ASSERT

/* Libraries */
#cmakedefine HAVE_LUA
#cmakedefine HAVE_BROTLI
#cmakedefine HAVE_ZSTD

/* Valgrind support for coroutines */
#cmakedefine HAVE_VALGRIND

#endif

