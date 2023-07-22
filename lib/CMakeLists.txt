if (CMAKE_CROSSCOMPILING)
    set(IMPORT_EXECUTABLES "IMPORTFILE-NOTFILE" CACHE FILEPATH "Point it to the export file from a native build")
    include(${IMPORT_EXECUTABLES})
endif ()

include_directories(${CMAKE_SOURCE_DIR}/include)

set(SOURCES
        core/string.c
        core/palloc.c
        core/array.c
        core/list.c
        core/timer_wheel.c
        core/base16.c
        core/base64.c
        core/crc32.c
        core/md5.c
        core/date.c
        core/string_buf.c
        core/json.c
        core/buf.c
        core/trie.c
        core/coro.c
        core/number/str_to_int.c
        core/number/int_to_str.c
        core/number/str_to_float.c
        core/number/float_to_str.c
        core/hex.c
        io/inet.c
        io/tcp.c
        io/udp.c
        io/sync_wait.c
        io/event_loop.c
        io/selector/epoll.c
        io/selector/kqueue.c
        io/http/http_server.c
        io/http/http_server_wait.c
        io/http/http_server_req.c
        io/http/http_server_req_body.c
        io/http/http_parse.c
        io/http/http_server_res.c
        io/http/module/http_server_file.c
        io/http/module/http_server_dispatcher.c
        io/postgres/pgsql_pool.c
        io/postgres/pgsql_pool_wait.c
        io/postgres/pgsql_auth.c
        io/postgres/pgsql_exec.c
        )


add_library(sky_static STATIC ${SOURCES})

target_link_libraries(sky_static)

set_target_properties(sky_static PROPERTIES
        OUTPUT_NAME sky CLEAN_DIRECT_OUTPUT 1)
set_target_properties(sky_static PROPERTIES COMPILE_FLAGS "-fPIC")

file(WRITE "${CMAKE_BINARY_DIR}/empty.c" "")
add_library(sky_shared SHARED "${CMAKE_BINARY_DIR}/empty.c")
target_link_libraries(sky_shared
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )

set_target_properties(sky_shared PROPERTIES
        OUTPUT_NAME sky CLEAN_DIRECT_OUTPUT 1)

include_directories(${CMAKE_BINARY_DIR})

if (NOT SKY_HAVE_BUILTIN_FPCLASSIFY)
    set(ADDITIONAL_LIBRARIES ${ADDITIONAL_LIBRARIES} PARENT_SCOPE)
endif ()

if (SKY_HAVE_LIBUCONTEXT)
    message(STATUS "Using libucontext/${CMAKE_SYSTEM_PROCESSOR} for coroutine context switching")

    include(ExternalProject)
    if (USE_LIBUCONTEXT_DIR)
        ExternalProject_Add(libucontext
                URL ${USE_LIBUCONTEXT_DIR}
                BUILD_IN_SOURCE ON

                CONFIGURE_COMMAND ""
                BUILD_COMMAND make
                INSTALL_COMMAND make install DESTDIR=${CMAKE_BINARY_DIR}

                BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/lib/libucontext.a

                BUILD_ALWAYS OFF
                UPDATE_DISCONNECTED ON
                )
    else ()
        ExternalProject_Add(libucontext
                GIT_REPOSITORY https://gitee.com/beliefsky/libucontext.git

                BUILD_IN_SOURCE ON

                CONFIGURE_COMMAND ""
                BUILD_COMMAND make
                INSTALL_COMMAND make install DESTDIR=${CMAKE_BINARY_DIR}

                BUILD_BYPRODUCTS ${CMAKE_BINARY_DIR}/lib/libucontext.a

                BUILD_ALWAYS OFF
                UPDATE_DISCONNECTED ON
                )
    endif ()

    add_dependencies(sky_static libucontext)

    set(ADDITIONAL_LIBRARIES ${CMAKE_BINARY_DIR}/lib/libucontext.a ${ADDITIONAL_LIBRARIES} PARENT_SCOPE)
    include_directories(${CMAKE_BINARY_DIR}/usr/include)
else ()
    message(STATUS "Using built-in context switching routines for ${CMAKE_SYSTEM_PROCESSOR} processors")
endif ()