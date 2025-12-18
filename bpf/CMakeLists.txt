# BPF模块的CMake配置
# 基于bpf/Makefile的启动命令

# 检测系统架构
execute_process(
    COMMAND uname -m
    OUTPUT_VARIABLE UNAME_M
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(UNAME_M STREQUAL "x86_64")
    set(ARCH "x86")
elseif(UNAME_M STREQUAL "aarch64")
    set(ARCH "arm64")
else()
    set(ARCH ${UNAME_M})
endif()

message(STATUS "Detected architecture: ${UNAME_M} -> ${ARCH}")

# 设置BPF目标文件
set(BPF_TARGET net_monitor)
set(BPF_OBJ ${BPF_TARGET}.bpf.o)
set(USER_SKEL ${BPF_TARGET}.skel.h)

# 自定义命令：生成vmlinux.h
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/vmlinux.h
    COMMAND bpftool btf dump file /sys/kernel/btf/vmlinux format c > ${CMAKE_CURRENT_SOURCE_DIR}/vmlinux.h
    COMMENT "Generating vmlinux.h from kernel BTF"
)

# 自定义目标：生成vmlinux.h（如果需要）
add_custom_target(vmlinux_h_target
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/vmlinux.h
    COMMENT "Generating vmlinux.h if needed"
)

# 检查vmlinux.h是否存在
if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/vmlinux.h)
    message(STATUS "vmlinux.h already exists")
    set(VMLINUX_DEP "")
else()
    message(STATUS "vmlinux.h will be generated during build")
    set(VMLINUX_DEP vmlinux_h_target)
endif()

# 自定义命令：编译BPF程序
add_custom_command(
    OUTPUT ${BPF_OBJ}
    COMMAND clang
        -target bpf
        -D __TARGET_ARCH_${ARCH}
        -Wall
        -O2 -g
        -c ${CMAKE_CURRENT_SOURCE_DIR}/${BPF_TARGET}.bpf.c
        -o ${CMAKE_CURRENT_BINARY_DIR}/${BPF_OBJ}
    COMMAND llvm-strip -g ${CMAKE_CURRENT_BINARY_DIR}/${BPF_OBJ}
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/${BPF_TARGET}.bpf.c
            ${VMLINUX_DEP}
    COMMENT "Compiling BPF program: ${BPF_TARGET}.bpf.c -> ${BPF_OBJ}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)

# 自定义目标：编译BPF对象
add_custom_target(bpf_obj
    DEPENDS ${BPF_OBJ}
    COMMENT "Building BPF object file"
)

set(SKEL_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/include)
# 自定义命令：生成skeleton头文件（在构建目录中）
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/${USER_SKEL}
    COMMAND ${CMAKE_COMMAND} -E echo "Generating skeleton from ${BPF_OBJ}"
    COMMAND bpftool gen skeleton ${CMAKE_CURRENT_BINARY_DIR}/${BPF_OBJ} > ${CMAKE_CURRENT_BINARY_DIR}/${USER_SKEL}
    DEPENDS bpf_obj
    COMMENT "Generating BPF skeleton: ${USER_SKEL}"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    VERBATIM
)

# 自定义命令：将skel.h复制到include目录
add_custom_command(
    OUTPUT ${SKEL_INCLUDE_DIR}/${USER_SKEL}
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_CURRENT_BINARY_DIR}/${USER_SKEL}
        ${SKEL_INCLUDE_DIR}/${USER_SKEL}
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${USER_SKEL}
    COMMENT "Copying ${USER_SKEL} to include directory"
)

# 自定义目标：生成skeleton
add_custom_target(bpf_skel
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/${USER_SKEL} ${SKEL_INCLUDE_DIR}/${USER_SKEL}
    COMMENT "Building BPF skeleton and copying to include directory"
)

# 添加自定义目标（标记为ALL，使其成为默认构建的一部分）
add_custom_target(bpf_all ALL
    DEPENDS bpf_obj bpf_skel
    COMMENT "Building all BPF targets"
)

# 清理目标
add_custom_target(bpf_clean
    COMMAND rm -f ${CMAKE_CURRENT_BINARY_DIR}/${BPF_OBJ}
    COMMENT "Cleaning BPF build files"
)

# 将生成的文件添加到安装目标（如果需要）
# install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${BPF_OBJ} DESTINATION lib/bpf)
# install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${USER_SKEL} DESTINATION include/bpf)

message(STATUS "BPF module configured: target=${BPF_TARGET}, arch=${ARCH}")
