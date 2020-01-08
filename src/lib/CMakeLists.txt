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
        core/number.c
        core/date.c
        core/buf.c
        core/md5.c
        core/base64.c
        core/uuid.c
        core/json.c
        core/json_builder.c
        event/event_loop.c
        net/tcp.c
        net/http/http_request.c
        net/http/http_parse.c
        net/http/http_server.c
        net/http/module/http_module_file.c
        net/http/module/http_module_dispatcher.c
        net/http/extend/http_extend_pgsql_pool.c
        net/http/extend/http_extend_redis_pool.c
        )
add_library(sky_static STATIC ${SOURCES})
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
    set(ADDITIONAL_LIBRARIES ${ADDITIONAL_LIBRARIES} -lm PARENT_SCOPE)
endif ()

install(TARGETS sky_shared sky_static
        LIBRARY DESTINATION /usr/local/lib/sky
        ARCHIVE DESTINATION /usr/local/lib/sky
        )

set(CMAKE_INSTALL_FULL_INCLUDEDIR "/usr/local/include")

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
        core/number.h
        core/date.h
        core/cpuinfo.h
        core/buf.h
        core/md5.h
        core/base64.h
        core/log.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/core")

install(FILES
        event/event_loop.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/event")
install(FILES
        net/inet.h
        net/tcp.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net")
install(FILES
        net/http/http_parse.h
        net/http/http_request.h
        net/http/http_server.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/http")
install(FILES
        net/http/module/http_module_dispatcher.h
        net/http/module/http_module_file.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/http/module")
install(FILES
        net/http/extend/http_extend_pgsql_pool.h
        net/http/extend/http_extend_redis_pool.h
        DESTINATION "${CMAKE_INSTALL_FULL_INCLUDEDIR}/sky/net/http/extend")