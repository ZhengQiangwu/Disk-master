cmake_minimum_required(VERSION 3.10)
project(PopupBlockerTest)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# ----------------------------------------------------------------------------
# 查找我们自己的 libAudioVideoProc.so 库
# ----------------------------------------------------------------------------
# 定义库头文件的搜索路径
# 使用相对路径，假设测试项目和库项目在同一个父目录下
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Disk-master)

# 定义库文件的搜索路径
link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../Disk-master/build)
# ----------------------------------------------------------------------------
# 创建可执行文件目标
# ----------------------------------------------------------------------------
add_executable(Disk-masterTest main.cpp)

# 2. 告诉编译器去哪里找我们的API头文件 (.h)
#    我们使用相对路径指向库项目的源目录
target_include_directories(Disk-masterTest PUBLIC ../Disk-master)

target_link_libraries(Disk-masterTest PRIVATE diskcleaner)
