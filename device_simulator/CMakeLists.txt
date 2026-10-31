# 不依赖 Winsock 的模拟器纯计算核心；Linux 也可以构建和测试。
add_library(simulator_core STATIC
    include/device_simulator/data_frame_builder.h
    include/device_simulator/data_frame_limits.h
    include/device_simulator/outbound_queue.h
    include/device_simulator/sample_budget.h
    src/data_frame_builder.cpp
    src/outbound_queue.cpp
    src/sample_budget.cpp
)

target_include_directories(simulator_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# DATA 组帧核心返回协议 Frame，因此公开传递 protocol 的头文件与链接依赖。
target_link_libraries(simulator_core PUBLIC
    protocol
)

# 当前 TCP 传输层直接使用 Winsock 类型和 API，因此只在 Windows 上创建目标。
# 非 Windows 平台仍可配置并构建独立的 protocol 与 tests，不会被模拟器阻断。
if (WIN32)
    # 头文件也列入 target sources，便于 Visual Studio 在项目树中展示和编辑。
    add_executable(device_simulator
        src/main.cpp
        src/WinsockRuntime.cpp
        src/WinsockRuntime.h
    )

    # protocol 提供 Frame、ProtocolCodec 和 StreamParser；ws2_32 提供
    # WSAStartup/socket/bind/listen/accept/recv/send/closesocket 等符号。
    target_link_libraries(device_simulator PRIVATE
        protocol
        simulator_core
        ws2_32
    )
else ()
    # 当前实现明确依赖 Winsock；未来增加 POSIX Socket 适配后再启用非 Windows 目标。
    message(STATUS "device_simulator is skipped because the current transport uses Winsock")
endif ()
