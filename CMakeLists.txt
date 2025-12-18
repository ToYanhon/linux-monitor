cmake_minimum_required(VERSION 3.20)

project(Linux-monitor LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSION OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(bpf)
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

add_subdirectory(proto)
add_subdirectory(kmod)
add_subdirectory(monitor)
add_subdirectory(grpc)
add_subdirectory(client)
add_subdirectory(server)
add_subdirectory(performance_server)
add_subdirectory(performance_instance)

