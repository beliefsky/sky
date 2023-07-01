include_directories(${CMAKE_SOURCE_DIR}/include)
#include_directories(BEFORE ${CMAKE_BINARY_DIR})

add_executable(sky_test sky_test.c)

target_link_libraries(sky_test
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )
