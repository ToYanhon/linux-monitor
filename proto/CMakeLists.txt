# 设置 Protobuf 和 gRPC 的查找路径
set(CMAKE_PREFIX_PATH "/usr/local/lib/cmake/protobuf;/usr/local/lib/cmake/grpc;${CMAKE_PREFIX_PATH}")

# grpc_cpp_plugin 应该已经由 Dockerfile 从 grpc/cxx 镜像复制到 /usr/bin/grpc_cpp_plugin
set(GRPC_CPP_PLUGIN "/usr/bin/grpc_cpp_plugin")

if(EXISTS "${GRPC_CPP_PLUGIN}")
    message(STATUS "Found grpc_cpp_plugin: ${GRPC_CPP_PLUGIN}")
else()
    message(FATAL_ERROR "grpc_cpp_plugin not found at ${GRPC_CPP_PLUGIN}")
endif()

# 首先尝试 CONFIG 模式查找
find_package(Protobuf CONFIG QUIET)
if(NOT Protobuf_FOUND)
    # 如果 CONFIG 模式失败，尝试 MODULE 模式
    find_package(Protobuf MODULE QUIET)
endif()

# 对于 gRPC，只使用 CONFIG 模式，因为 MODULE 模式需要 FindgRPC.cmake
find_package(gRPC CONFIG QUIET)
if(NOT gRPC_FOUND)
    # 如果 CONFIG 模式失败，尝试使用 pkg-config
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(gRPC QUIET grpc++)
        if(gRPC_FOUND)
            set(gRPC_FOUND TRUE)
            add_library(gRPC::grpc INTERFACE IMPORTED)
            target_include_directories(gRPC::grpc INTERFACE ${gRPC_INCLUDE_DIRS})
            target_link_libraries(gRPC::grpc INTERFACE ${gRPC_LIBRARIES})
            
            add_library(gRPC::grpc++ INTERFACE IMPORTED)
            target_include_directories(gRPC::grpc++ INTERFACE ${gRPC_INCLUDE_DIRS})
            target_link_libraries(gRPC::grpc++ INTERFACE ${gRPC_LIBRARIES})
            
            # 创建 grpc_cpp_plugin 目标
            add_executable(gRPC::grpc_cpp_plugin IMPORTED)
            set_target_properties(gRPC::grpc_cpp_plugin PROPERTIES
                IMPORTED_LOCATION "${GRPC_CPP_PLUGIN}"
            )
        endif()
    endif()
endif()

if(NOT Protobuf_FOUND)
    message(FATAL_ERROR "Protobuf not found")
endif()

if(NOT gRPC_FOUND)
    message(FATAL_ERROR "gRPC not found")
endif()

# 确保 grpc_cpp_plugin 可执行
if(NOT EXISTS "${GRPC_CPP_PLUGIN}")
    message(FATAL_ERROR "grpc_cpp_plugin not found at ${GRPC_CPP_PLUGIN}. Please install grpc-plugins package.")
endif()

file(GLOB_RECURSE PROTO_FILES ${CMAKE_CURRENT_SOURCE_DIR}/*.proto)

add_library(monitor_proto ${PROTO_FILES})    
target_link_libraries(monitor_proto
    PUBLIC
        protobuf::libprotobuf   
        gRPC::grpc            
        gRPC::grpc++         
)
target_include_directories(monitor_proto PUBLIC
  ${PROTOBUF_INCLUDE_DIRS}       
  ${CMAKE_CURRENT_BINARY_DIR}
)     

get_target_property(grpc_cpp_plugin_location gRPC::grpc_cpp_plugin LOCATION)    
protobuf_generate(TARGET monitor_proto LANGUAGE cpp)   

protobuf_generate(TARGET monitor_proto LANGUAGE grpc
  GENERATE_EXTENSIONS .grpc.pb.h .grpc.pb.cc
  PLUGIN "protoc-gen-grpc=${grpc_cpp_plugin_location}"
)
