include_directories(${CMAKE_SOURCE_DIR}/src/lib)
#include_directories(BEFORE ${CMAKE_BINARY_DIR})

add_executable(sky_test sky_test.c yyjson.c)

target_link_libraries(sky_test
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )


add_executable(sky_test_http_file sky_test_http_file.c)

target_link_libraries(sky_test_http_file
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )

add_executable(sky_test_http_dispatcher sky_test_http_dispatcher.c)

target_link_libraries(sky_test_http_dispatcher
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )

#add_executable(sky_test_tcp_proxy sky_test_tcp_proxy.c)
#
#target_link_libraries(sky_test_tcp_proxy
#        ${SKY_COMMON_LIBS}
#        ${ADDITIONAL_LIBRARIES}
#        )

add_executable(sky_test_mqtt_broker sky_test_mqtt_broker.c)

target_link_libraries(sky_test_mqtt_broker
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )

add_executable(sky_test_mqtt_client sky_test_mqtt_client.c)

target_link_libraries(sky_test_mqtt_client
        ${SKY_COMMON_LIBS}
        ${ADDITIONAL_LIBRARIES}
        )
