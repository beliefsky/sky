include_directories(${CMAKE_SOURCE_DIR}/src/lib)

add_executable(sky sky.c)

target_link_libraries(sky
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )

install(TARGETS sky
        RUNTIME DESTINATION /usr/local/sky/bin
        )


