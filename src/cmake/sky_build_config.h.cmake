

#ifndef SKY_BUILD_CONFIG_H
#define SKY_BUILD_CONFIG_H

/* API available in Glibc/Linux, but possibly not elsewhere */
#cmakedefine HAS_ACCEPT4
#cmakedefine HAS_ALLOCA_H
#cmakedefine HAS_CLOCK_GETTIME
#cmakedefine HAS_GET_CURRENT_DIR_NAME
#cmakedefine HAS_GETAUXVAL
#cmakedefine HAS_MEMPCPY
#cmakedefine HAS_MEMRCHR
#cmakedefine HAS_MKOSTEMP
#cmakedefine HAS_PIPE2
#cmakedefine HAS_PTHREADBARRIER
#cmakedefine HAS_RAWMEMCHR
#cmakedefine HAS_READAHEAD
#cmakedefine HAS_REALLOCARRAY

/* Compiler builtins for specific CPU instruction support */
#cmakedefine HAVE_BUILTIN_CLZLL
#cmakedefine HAVE_BUILTIN_CPU_INIT
#cmakedefine HAVE_BUILTIN_IA32_CRC32
#cmakedefine HAVE_BUILTIN_MUL_OVERFLOW
#cmakedefine HAVE_BUILTIN_ADD_OVERFLOW
#cmakedefine HAVE_BUILTIN_BSWAP

/* C11 _Static_assert() */
#cmakedefine HAVE_STATIC_ASSERT

/* Libraries */
#cmakedefine HAVE_LUA

/* Valgrind support for coroutines */
#cmakedefine USE_VALGRIND


#cmakedefine HAS_CPU_MSSE
#endif

