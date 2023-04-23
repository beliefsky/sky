if (CMAKE_CROSSCOMPILING)
    set(IMPORT_EXECUTABLES "IMPORTFILE-NOTFILE" CACHE FILEPATH "Point it to the export file from a native build")
    include(${IMPORT_EXECUTABLES})
endif ()

set(SOURCES
        core/palloc.c
        core/mem_pool.c
        core/queue.c
        core/array.c
        core/list.c
        core/rbtree.c
        core/hash.c
        core/hashmap.c
        core/trie.c
        core/topic_tree.c
        core/coro.c
        core/string.c
        core/string_buf.c
        core/string_out_stream.c
        core/number.c
        core/float.c
        core/date.c
        core/buf.c
        core/crc32.c
        core/md5.c
        core/sha1.c
        core/base16.c
        core/base64.c
        core/random.c
        core/uuid.c
        core/json.c
        core/timer_wheel.c
        core/process.c
#        safe/spsc_queue.c
#        safe/mpsc_queue.c
#        safe/mpmc_queue.c
#        safe/mpsc_ring_buf.c
        fs/file.c
        event/event_loop.c
        inet/tcp.c
        inet/tcp_pool.c
        inet/tcp_listener.c
        inet/tcp_client.c
        inet/postgresql/pgsql_pool.c
        inet/redis/redis_pool.c
        inet/http/http_server.c
        inet/http/http_request.c
        inet/http/http_parse.c
        inet/http/http_response.c
        inet/http/module/http_module_file.c
        inet/http/module/http_module_dispatcher.c
        inet/http/http_client.c
        inet/http/http_url.c
        inet/mqtt/mqtt_io_wrappers.c
        inet/mqtt/mqtt_protocol.c
        inet/mqtt/mqtt_subs.c
        inet/mqtt/mqtt_request.c
        inet/mqtt/mqtt_response.c
        inet/mqtt/mqtt_server.c
        inet/mqtt/mqtt_client.c
        )

if (SKY_HAVE_EPOLL)
    set(SOURCES
            ${SOURCES}
            event/selector_epoll.c
            )
elseif (SKY_HAVE_KQUEUE)
    set(SOURCES
            ${SOURCES}
            event/selector_kqueue.c
            )
endif ()

if (SKY_HAVE_SSL)
    set(SOURCES
            ${SOURCES}
            inet/ssl/tls.c
            inet/ssl/tls_client.c
            )
endif ()


add_library(sky_static STATIC ${SOURCES})

target_link_libraries(sky_static)

set_target_properties(sky_static PROPERTIES
        OUTPUT_NAME sky CLEAN_DIRECT_OUTPUT 1)
set_target_properties(sky_static PROPERTIES COMPILE_FLAGS "-fPIC")


# Can't call add_library() without source files. Create an empty .c file,
# then link with the static library just recently built.
file(WRITE "${CMAKE_BINARY_DIR}/empty.c" "")
add_library(sky_shared SHARED "${CMAKE_BINARY_DIR}/empty.c")
target_link_libraries(sky_shared
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )
#if (NOT APPLE)
#	target_link_libraries(sky_shared
#		-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/libsky.sym
#	)
#endif ()

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

install(TARGETS sky_shared sky_static
        DESTINATION /usr/local/lib/sky
        )

set(CMAKE_INSTALL_FULL_INCLUDEDIR "/usr/local/include")

install(FILES
        sky_build_config.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky")

install(FILES
        core/types.h
        core/memory.h
        core/process.h
        core/palloc.h
        core/mem_pool.h
        core/array.h
        core/queue.h
        core/list.h
        core/rbtree.h
        core/hash.h
        core/hashmap.h
        core/trie.h
        core/topic_tree.h
        core/coro.h
        core/string.h
        core/string_buf.h
        core/string_out_stream.h
        core/number.h
        core/float.h
        core/date.h
        core/buf.h
        core/crc32.h
        core/md5.h
        core/sha1.h
        core/base16.h
        core/base64.h
        core/uuid.h
        core/log.h
        core/json.h
        core/timer_wheel.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/core")
#install(FILES
#        safe/atomic.h
#        safe/mpmc_queue.h
#        safe/mpsc_queue.h
#        safe/spsc_queue.h
#        safe/mpsc_ring_buf.h
#        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/safe")

install(FILES
        fs/file.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/fs")

install(FILES
        event/selector.h
        event/event_loop.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/event")
install(FILES
        inet/inet.h
        inet/tcp.h
        inet/tcp_pool.h
        inet/tcp_client.h
        inet/tcp_listener.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/inet")

if (SKY_HAVE_SSL)
    install(FILES
            inet/ssl/tls.h
            inet/ssl/tls_client.h
            DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/inet/ssl")
endif ()

install(FILES
        inet/postgresql/pgsql_pool.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/inet/postgresql")
install(FILES
        inet/redis/redis_pool.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/inet/redis")
install(FILES
        inet/http/http_url.h
        inet/http/http_parse.h
        inet/http/http_request.h
        inet/http/http_response.h
        inet/http/http_server.h
        inet/http/http_client.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/inet/http")
install(FILES
        inet/http/module/http_module_dispatcher.h
        inet/http/module/http_module_file.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/inet/http/module")
install(FILES
        inet/http/extend/http_extend_pgsql_pool.h
        inet/http/extend/http_extend_redis_pool.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/inet/http/extend")