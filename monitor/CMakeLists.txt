file(GLOB_RECURSE MONITOR_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(monitor STATIC ${MONITOR_FILES})
target_include_directories(monitor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(monitor PUBLIC 
    monitor_proto 
)

# 查找 BCC 库
find_package(PkgConfig REQUIRED)
pkg_check_modules(BCC bcc)

# 如果 pkg-config 找不到，手动指定路径
if(NOT BCC_FOUND)
    # 尝试手动查找
    find_path(BCC_INCLUDE_DIR bcc/BPF.h PATH_SUFFIXES bcc)
    find_library(BCC_LIBRARY bcc)
    
    if(BCC_INCLUDE_DIR AND BCC_LIBRARY)
        set(BCC_INCLUDE_DIRS ${BCC_INCLUDE_DIR})
        set(BCC_LIBRARIES ${BCC_LIBRARY})
        set(BCC_FOUND TRUE)
    endif()
endif()

if(BCC_FOUND)
    message(STATUS "Found BCC: ${BCC_LIBRARIES}")
    target_include_directories(monitor PUBLIC ${BCC_INCLUDE_DIRS})
    # 链接到你的目标
    target_link_libraries(monitor PUBLIC ${BCC_LIBRARIES})
else()
    message(FATAL_ERROR "BCC library not found. Please install libbcc-dev")
endif()