#include "WinsockRuntime.h"

#include <cstdlib>
#include <exception>
#include <iostream>

int main()
{
    // 所有初始化和服务端执行都放在同一个异常边界内：Winsock 初始化失败、
    // 标准容器分配失败等异常最终都会转成非零进程退出码，而不是无提示终止。
    try
    {
        // 局部对象按构造逆序析构。先构造 runtime、后构造 server，能够确保
        // server 及其 run() 中的 Socket 全部结束后才调用 WSACleanup()。
        WinsockRuntime runtime;
        TcpServer server;

        // run() 用 bool 区分正常会话结束与网络/编码故障，映射成标准进程退出码，
        // 便于脚本、测试和调试器判断模拟器是否成功完成本次会话。
        return server.run() ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception& exception)
    {
        // 异常只在进程入口统一记录；底层 RAII 对象已经在栈展开期间释放资源。
        std::cerr << "device_simulator failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
