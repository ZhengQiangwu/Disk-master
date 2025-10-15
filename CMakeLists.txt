# CMakeLists.txt
cmake_minimum_required(VERSION 3.10)
project(DiskCleaner CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# 新增：查找系统中的线程库
find_package(Threads REQUIRED)

# 添加源文件
add_library(diskcleaner SHARED disk_cleaner.cpp)

# 新增：将找到的线程库链接到我们的 diskcleaner 库
# Threads::Threads 是 CMake 提供的标准目标
target_link_libraries(diskcleaner PRIVATE Threads::Threads)

# UOS/Debian 系统通常不需要手动链接 stdc++fs
# 但为了兼容性，可以加上
target_link_libraries(diskcleaner PRIVATE stdc++fs)

# 设置头文件的包含目录，这样 #include "popup_blocker_api.h" 才能被找到
# PUBLIC 表示任何链接到这个库的其它CMake项目也会自动获得这个头文件路径
target_include_directories(diskcleaner PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})