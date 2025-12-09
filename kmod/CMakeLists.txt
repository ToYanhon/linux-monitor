# 自动探测当前运行的内核版本 KERNEL_VERSION 和构建目录 KERNEL_SOURCE_DIR
execute_process(
    COMMAND uname -r
    OUTPUT_VARIABLE KERNEL_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(KERNEL_SOURCE_DIR "/lib/modules/${KERNEL_VERSION}/build")

# 自动探测当前运行的内核版本和构建目录
execute_process(
    COMMAND uname -r
    OUTPUT_VARIABLE KERNEL_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
)
set(KERNEL_SOURCE_DIR "/lib/modules/${KERNEL_VERSION}/build")

# 检查内核构建目录是否存在
if(NOT EXISTS "${KERNEL_SOURCE_DIR}/Makefile")
    message(FATAL_ERROR "内核构建目录未找到: ${KERNEL_SOURCE_DIR}\n请安装对应版本的内核头文件，例如: sudo apt install linux-headers-${KERNEL_VERSION}")
endif()

# 添加内核模块的构建逻辑
set(KERNEL_MODULES
 cpu_load_monitor_kmod
 cpu_softirq_monitor_kmod
 cpu_stat_monitor_kmod
)

set(KERNEL_MODULE_OUTPUT_DIR ${CMAKE_BINARY_DIR}/kmod)
file(MAKE_DIRECTORY ${KERNEL_MODULE_OUTPUT_DIR})


set(MODULE_TARGETS)
foreach(MODULE ${KERNEL_MODULES})
    # 定义内核模块的构建目标
    add_custom_target(${MODULE}_module
        COMMAND make -C ${KERNEL_SOURCE_DIR} M=${CMAKE_CURRENT_SOURCE_DIR} modules
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Building kernel module: ${MODULE}"
        VERBATIM
    )
    list(APPEND MODULE_TARGETS ${MODULE}_module)

    # 定义一个清理目标
    add_custom_target(${MODULE}_clean
        COMMAND make -C ${KERNEL_SOURCE_DIR} M=${CMAKE_CURRENT_SOURCE_DIR} clean
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "cleaned built modules"
    )
endforeach()

# 定义所有内核模块的构建目标
add_custom_target(modules ALL DEPENDS ${MODULE_TARGETS} VERBATIM)