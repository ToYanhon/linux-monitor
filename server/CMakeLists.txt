file(GLOB_RECURSE SERVER_SOURCES CONFIGURE_DEPENDS src/*.cpp)
add_executable(server ${SERVER_SOURCES})
target_include_directories(server PUBLIC include)
target_link_libraries(server PUBLIC server_impl)