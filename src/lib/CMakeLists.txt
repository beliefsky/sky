if (CMAKE_CROSSCOMPILING)
    set(IMPORT_EXECUTABLES "IMPORTFILE-NOTFILE" CACHE FILEPATH "Point it to the export file from a native build")
    include(${IMPORT_EXECUTABLES})
endif ()

set(SOURCES
        core/palloc.c
        core/array.c
        core/list.c
        core/cpuinfo.c
        core/hash.c
        core/rbtree.c
        core/trie.c
        core/coro.c
        core/string.c
        core/string_buf.c
        core/number.c
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
        math/matrix.c
        event/event_loop_epoll.c
        event/event_loop_kqueue.c
        net/tcp.c
        net/udp.c
        net/clients/tcp_pool.c
        net/clients/tcp_rw_pool.c
        net/clients/tcp_async_pool.c
        net/clients/pgsql_pool.c
        net/clients/redis_pool.c
        net/http/http_io_wrappers.c
        net/http/http_request.c
        net/http/http_response.c
        net/http/http_parse.c
        net/http/http_server.c
        net/http/module/http_module_file.c
        net/http/module/http_module_dispatcher.c
        net/http/module/http_module_websocket.c
        net/tls/tls.c
        )
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

if (NOT HAVE_BUILTIN_FPCLASSIFY)
    set(ADDITIONAL_LIBRARIES ${ADDITIONAL_LIBRARIES} PARENT_SCOPE)
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
        core/palloc.h
        core/array.h
        core/hash.h
        core/list.h
        core/rbtree.h
        core/trie.h
        core/coro.h
        core/string.h
        core/string_buf.h
        core/number.h
        core/date.h
        core/cpuinfo.h
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
        event/event_loop.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/event")
install(FILES
        net/inet.h
        net/tcp.h
        net/udp.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net")
install(FILES
        net/clients/tcp_pool.h
        net/clients/redis_pool.h
        net/clients/pgsql_pool.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/clients")
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