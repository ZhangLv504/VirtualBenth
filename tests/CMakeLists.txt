# Catch2 合并版：把两个文件里的 .cpp 编成一个静态库（不依赖网络）
add_library(catch2_amalgamated STATIC
    ${CMAKE_SOURCE_DIR}/third_party/Catch2/catch_amalgamated.cpp
)
target_include_directories(catch2_amalgamated PUBLIC
    ${CMAKE_SOURCE_DIR}/third_party/Catch2
)

add_executable(protocol_tests
    test_crc16.cpp
    test_frame.cpp
    test_protocol_codec.cpp
    test_stream_parser.cpp
)

target_link_libraries(protocol_tests PRIVATE
    protocol                 # 你的协议库
    catch2_amalgamated       # Catch2
)

add_test(NAME protocol_tests COMMAND protocol_tests)

# 模拟器纯计算逻辑独立测试，不启动 Winsock 或监听端口。
add_executable(sample_budget_tests
    test_sample_budget.cpp
)

target_link_libraries(sample_budget_tests PRIVATE
    simulator_core
    catch2_amalgamated
)

add_test(NAME sample_budget_tests COMMAND sample_budget_tests)
