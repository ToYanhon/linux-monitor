find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)

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