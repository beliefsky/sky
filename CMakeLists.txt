project(sky C)
cmake_minimum_required(VERSION 2.8.10)
set(CMAKE_C_STANDARD 11)

set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/src/cmake")

include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(CheckFunctionExists)
include(CheckIncludeFile)
include(CodeCoverage)
include(EnableCFlag)
include(FindPkgConfig)
include(TrySanitizer)

#
# Find libraries
#
#find_package(Threads REQUIRED)
#find_package(OpenSSL REQUIRED)
#find_package(ZLIB REQUIRED)
#set(PostgreSQL_TYPE_INCLUDE_DIR /usr/include/postgresql)
#find_package(PostgreSQL REQUIRED)
set(ADDITIONAL_LIBRARIES #[[${CMAKE_THREAD_LIBS_INIT}]] #[[${OPENSSL_LIBRARIES]] #[[${ZLIB_LIBRARIES}]] #[[${PostgreSQL_LIBRARIES}]])


set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

check_function_exists(mempcpy HAS_MEMPCPY)
check_function_exists(accept4 HAS_ACCEPT4)

if (NOT HAS_CLOCK_GETTIME AND ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    list(APPEND ADDITIONAL_LIBRARIES rt)
endif ()

#
# Ensure compiler is compatible with GNU11 standard
#
check_c_compiler_flag(-std=gnu11 HAS_STD_GNU11)
if (NOT HAS_STD_GNU11)
    message(FATAL_ERROR "Compiler does not support -std=gnu11. Consider using a newer compiler")
endif()

#
# Check for GCC builtin functions
#

check_c_source_compiles("int main(void) { __builtin_bswap64(123); }" HAVE_BUILTIN_BSWAP)

check_c_source_compiles("int main(void) { __builtin_cpu_init(); }" HAVE_BUILTIN_CPU_INIT)
check_c_source_compiles("int main(void) { __builtin_clzll(0); }" HAVE_BUILTIN_CLZLL)
check_c_source_compiles("int main(void) { unsigned long long p; (void)__builtin_mul_overflow(0, 0, &p); }" HAVE_BUILTIN_MUL_OVERFLOW)
check_c_source_compiles("int main(void) { unsigned long long p; (void)__builtin_add_overflow(0, 0, &p); }" HAVE_BUILTIN_ADD_OVERFLOW)
check_c_source_compiles("int main(void) { _Static_assert(1, \"\"); }" HAVE_STATIC_ASSERT)

#
# Look for Valgrind header
#
find_path(VALGRIND_INCLUDE_DIR valgrind.h /usr/include /usr/include/valgrind /usr/local/include /usr/local/include/valgrind)
if (VALGRIND_INCLUDE_DIR)
    message(STATUS "Building with Valgrind support")
    set(USE_VALGRIND 1)
else ()
    message(STATUS "Valgrind headers not found -- disabling valgrind support")
endif()


enable_c_flag_if_avail(-mtune=native C_FLAGS_REL HAS_MTUNE_NATIVE)
enable_c_flag_if_avail(-march=native C_FLAGS_REL HAS_MARCH_NATIVE)
#
# Check if immediate binding and read-only global offset table flags
# can be used
#
if (APPLE)
    enable_c_flag_if_avail(-Wl,-bind_at_load CMAKE_EXE_LINKER_FLAGS
            HAS_IMMEDIATE_BINDING)
else ()
    enable_c_flag_if_avail(-Wl,-z,now CMAKE_EXE_LINKER_FLAGS
            HAS_IMMEDIATE_BINDING)
    enable_c_flag_if_avail(-Wl,-z,relro CMAKE_EXE_LINKER_FLAGS
            HAS_READ_ONLY_GOT)
    enable_c_flag_if_avail(-fno-plt CMAKE_C_FLAGS
            HAS_NO_PLT)
endif ()

enable_c_flag_if_avail(-msse4.2 CMAKE_C_FLAGS HAS_CPU_MSSE42)

if (${CMAKE_BUILD_TYPE} MATCHES "Rel")
    enable_c_flag_if_avail(-malign-data=abi C_FLAGS_REL HAS_ALIGN_DATA)
    enable_c_flag_if_avail(-fno-asynchronous-unwind-tables C_FLAGS_REL HAS_NO_ASYNC_UNWIND_TABLES)

    enable_c_flag_if_avail(-flto=jobserver C_FLAGS_REL HAS_LTO_JOBSERVER)
    if (NOT HAS_LTO_JOBSERVER)
        enable_c_flag_if_avail(-flto C_FLAGS_REL HAS_LTO)
    endif ()

    enable_c_flag_if_avail(-ffat-lto-objects C_FLAGS_REL HAS_LTO_FAT_OBJS)
    enable_c_flag_if_avail(-mcrc32 C_FLAGS_REL HAVE_BUILTIN_IA32_CRC32)
else ()
    option(SANITIZER "Use sanitizer (undefined, address, thread, none)" "none")

    if (${SANITIZER} MATCHES "(undefined|ub|ubsan)")
        try_sanitizer("undefined")
    elseif (${SANITIZER} MATCHES "(address|memory)")
        try_sanitizer("address")
    elseif (${SANITIZER} MATCHES "(thread|race)")
        try_sanitizer("thread")
    else ()
        message(STATUS "Building without a sanitizer")
    endif ()
endif ()


#
# These warnings are only supported by GCC, and some only in newer versions.
#
enable_warning_if_supported(-Wduplicated-cond)
enable_warning_if_supported(-Wduplicated-branches)
enable_warning_if_supported(-Wlogical-op)
enable_warning_if_supported(-Wrestrict)
enable_warning_if_supported(-Wdouble-promotion)
enable_warning_if_supported(-Wno-unused-parameter)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra -Wshadow -Wconversion -std=gnu11")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} ${C_FLAGS_REL}")
set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} ${C_FLAGS_REL}")
set(CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL} ${C_FLAGS_REL}")
add_definitions("-D_FILE_OFFSET_BITS=64")

if (APPLE)
    set(SKY_COMMON_LIBS -Wl,-force_load sky_static)
else ()
    set(SKY_COMMON_LIBS -Wl,-whole-archive sky_static -Wl,-no-whole-archive)
endif ()


#
# Generate sky_build_config.h
#
add_definitions(-include ${CMAKE_BINARY_DIR}/sky_build_config.h)
configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/sky_build_config.h.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/sky_build_config.h"
)

configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/cmake/sky.pc.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.pc"
)

add_subdirectory(src)

