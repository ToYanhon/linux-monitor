file(GLOB_RECURSE CLIENT_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(client STATIC ${CLIENT_FILES})
target_include_directories(client PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(client PUBLIC monitor_proto)