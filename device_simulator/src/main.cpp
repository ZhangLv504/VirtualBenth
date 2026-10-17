#include <cstdio>

#include "WinsockRuntime.h"

// device_simulator 入口（占位）：后续在此启动 TCP 服务端并推送采样数据
int main() {
    WinsockRuntime runtime;

    TcpServer server;
    server.run();

    return 0;
}
