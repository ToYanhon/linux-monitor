add_library(performance_server STATIC src/performance_server.cpp)

# 1. 手动指定MySQL头文件路径（根据你的实际安装位置修改）
set(MYSQL_INCLUDE_DIRS "/usr/include/mysql")

# 2. 手动指定MySQL库文件路径
set(MYSQL_LIBRARIES "/usr/lib/x86_64-linux-gnu/libmysqlclient.so")

target_include_directories(performance_server PUBLIC include ${MYSQL_INCLUDE_DIRS})
target_link_libraries(performance_server PUBLIC client pthread ${MYSQL_LIBRARIES})