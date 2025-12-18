file(GLOB_RECURSE MONITOR_FILES ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

add_library(monitor STATIC ${MONITOR_FILES})
target_include_directories(monitor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(monitor PUBLIC 
    monitor_proto 
)

# 查找 libbpf 库 - 优先使用本地编译的版本
option(USE_LOCAL_LIBBPF "Use locally compiled libbpf" ON)

if(USE_LOCAL_LIBBPF)
    # 使用本地编译的 libbpf
    set(LOCAL_LIBBPF_DIR "/tmp/libbpf-install/usr")
    if(EXISTS ${LOCAL_LIBBPF_DIR})
        set(LIBBPF_INCLUDE_DIRS ${LOCAL_LIBBPF_DIR}/include)
        set(LIBBPF_LIBRARIES ${LOCAL_LIBBPF_DIR}/lib64/libbpf.a)
        set(LIBBPF_FOUND TRUE)
        message(STATUS "Using local libbpf from ${LOCAL_LIBBPF_DIR}")
    else()
        message(WARNING "Local libbpf not found at ${LOCAL_LIBBPF_DIR}, falling back to system")
    endif()
endif()

if(NOT LIBBPF_FOUND)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBBPF libbpf)

    # 如果 pkg-config 找不到，手动指定路径
    if(NOT LIBBPF_FOUND)
        # message(STATUS "LIBBPF not found via pkg-config, trying manual search...")
        # 尝试手动查找
        find_path(LIBBPF_INCLUDE_DIR bpf/bpf.h PATH_SUFFIXES bpf)
        find_library(LIBBPF_LIBRARY bpf)
        
        if(LIBBPF_INCLUDE_DIR AND LIBBPF_LIBRARY)
            set(LIBBPF_INCLUDE_DIRS ${LIBBPF_INCLUDE_DIR})
            set(LIBBPF_LIBRARIES ${LIBBPF_LIBRARY})
            set(LIBBPF_FOUND TRUE)
        endif()
    endif()
endif()

if(LIBBPF_FOUND)
    message(STATUS "Found libbpf: ${LIBBPF_LIBRARIES}")
    target_include_directories(monitor PUBLIC ${LIBBPF_INCLUDE_DIRS})
    # 链接到你的目标，libbpf 需要 libelf 和 libz
    target_link_libraries(monitor PUBLIC ${LIBBPF_LIBRARIES} elf z)
else()
    message(FATAL_ERROR "libbpf library not found. Please install libbpf-dev")
endif()

# 添加对生成的skel.h文件的include路径
target_include_directories(monitor PUBLIC ${CMAKE_SOURCE_DIR}/include)
