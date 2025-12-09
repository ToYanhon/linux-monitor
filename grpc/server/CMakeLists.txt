file(GLOB_RECURSE SERVER_IMPL_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(server_impl STATIC ${SERVER_IMPL_FILES})
target_include_directories(server_impl PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(server_impl PUBLIC monitor_proto)