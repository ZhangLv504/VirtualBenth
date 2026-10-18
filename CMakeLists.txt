# 最低版本
cmake_minimum_required(VERSION 3.17)

# 项目名
project(VirtualBenth)

# C++版本/必须支持，否则报错
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 下源码含中文注释/字符串，强制按 UTF-8 解析（否则按本地代码页 936 会报 C4819/C2001）
if(MSVC)
    # 强制指定源文件和执行文件的编码为 utf-8
    add_compile_options("/utf-8")
else()
    add_compile_options(-finput-charset=UTF-8)
endif()

# 开启 ctest（add_test 依赖它） 
enable_testing()

add_subdirectory(protocol)
add_subdirectory(tests) 
add_subdirectory(device_simulator)
