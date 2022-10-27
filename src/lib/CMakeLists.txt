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
        core/number.c
        core/float.c
        core/date.c
        core/buf.c
        core/crc32.c
        core/md5.c
        core/sha1.c
        core/base64.c
        core/random.c
        core/uuid.c
        core/json.c
        core/timer_wheel.c
        core/process.c
        safe/spsc_queue.c
        safe/mpsc_queue.c
        safe/mpmc_queue.c
        safe/mpsc_ring_buf.c
        event/event_loop_common.c
        net/inet.c
        net/tcp_server.c
        net/tcp_pool.c
        net/tcp_client.c
        net/tcp_listener.c
        net/tls/tls.c
        net/extend/pgsql_pool.c
        net/extend/redis_pool.c
        net/http/http_io_wrappers.c
        net/http/http_request.c
        net/http/http_response.c
        net/http/http_parse.c
        net/http/http_server.c
        net/http/module/http_module_file.c
        net/http/module/http_module_dispatcher.c
        net/http/module/http_module_websocket.c
        net/mqtt/mqtt_io_wrappers.c
        net/mqtt/mqtt_protocol.c
        net/mqtt/mqtt_subs.c
        net/mqtt/mqtt_request.c
        net/mqtt/mqtt_response.c
        net/mqtt/mqtt_server.c
        net/mqtt/mqtt_client.c
        )

if (SKY_HAVE_EPOLL)
    set(SOURCES ${SOURCES} event/event_loop_epoll.c)
elseif (SKY_HAVE_KQUEUE)
    set(SOURCES ${SOURCES} event/event_loop_kqueue.c)
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
        core/number.h
        core/float.h
        core/date.h
        core/buf.h
        core/crc32.h
        core/md5.h
        core/sha1.h
        core/base64.h
        core/uuid.h
        core/log.h
        core/json.h
        core/timer_wheel.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/core")
install(FILES
        core/process.c
        safe/atomic.h
        safe/mpmc_queue.h
        safe/mpsc_queue.h
        safe/spsc_queue.h
        safe/mpsc_ring_buf.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/safe")
install(FILES
        event/event_loop.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/event")
install(FILES
        net/inet.h
        net/tcp_server.h
        net/tcp_pool.h
        net/tcp_client.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net")
install(FILES
        net/tls/tls.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/tls")
install(FILES
        net/extend/redis_pool.h
        net/extend/pgsql_pool.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/extend")
install(FILES
        net/http/http_parse.h
        net/http/http_request.h
        net/http/http_response.h
        net/http/http_server.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/http")
install(FILES
        net/http/module/http_module_dispatcher.h
        net/http/module/http_module_file.h
        net/http/module/http_module_websocket.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/http/module")
install(FILES
        net/http/extend/http_extend_pgsql_pool.h
        net/http/extend/http_extend_redis_pool.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/http/extend")
install(FILES
        net/mqtt/mqtt_io_wrappers.h
        net/mqtt/mqtt_protocol.h
        net/mqtt/mqtt_request.h
        net/mqtt/mqtt_response.h
        net/mqtt/mqtt_subs.h
        net/mqtt/mqtt_server.h
        net/mqtt/mqtt_client.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/mqtt")