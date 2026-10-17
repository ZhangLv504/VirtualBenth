#ifndef VIRTUALBENTH_WINSOCKRUNTIME_H
#define VIRTUALBENTH_WINSOCKRUNTIME_H

#include <iostream>
#include <string>
#include <vector>
#include <winsock2.h>
#include "protocol/stream_parser.h"
#include "protocol/frame.h"
#include "protocol/protocol_codec.h"

class WinsockRuntime
{
public:
    WinsockRuntime()
    {
        WSADATA wsaData;
        WSAStartup(MAKEWORD(2, 2), &wsaData);
    }

    ~WinsockRuntime() {
        WSACleanup();
    }
};

class TcpServer
{
public:
    void run()
    {
        // 创建用于监听的套接字 (IPv4, TCP流协议)
        listenSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

        // 配置服务器的 IP 和 端口
        sockaddr_in addr = {0};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(12345);   // 绑定 12345 端口
        addr.sin_addr.s_addr = INADDR_ANY;      // 监听本机所有网卡 IP

        // 将套接字与配置好的地址绑定
        bind(listenSocket, (struct sockaddr *)&addr, sizeof(addr));

        // 开始监听，SOMAXCONN 表示让操作系统决定最大积压连接数
        listen(listenSocket, SOMAXCONN);

        std::cout << "Server is listening on port 12345..." << std::endl;

        // 取得一个客户端连接，成功返回一个新的客户端 Socket
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        std::cout << "A client connected!" << std::endl;

        StreamParser parser;
        char buf[4096];

        while (true)
        {
            // 从 TCP 字节流读取数据，返回值只是本次读取的字节数
            int bytesRead = recv(clientSocket, buf, sizeof(buf), 0);
            if (bytesRead <= 0)
            {
                std::cout << "Client disconnected or error occurred." << std::endl;
                break;
            }

            std::vector<std::uint8_t> chunk(buf, buf + bytesRead);
            std::vector<Frame> frames = parser.feed(chunk);

            for (const auto& frame : frames)
            {
                std::cout << "Received complete frame CMD: " << static_cast<int>(frame.command) << std::endl;

                if (frame.command == Command::Error)
                {
                    Frame errorFrame;
                    errorFrame.command = Command::Error;
                    EncodeResult errorResult = ProtocolCodec::encode(errorFrame);
                    send_all(clientSocket, reinterpret_cast<const char*>(errorResult.bytes.data()), (int)errorResult.bytes.size());
                    break;
                }
                else if (frame.command == Command::Hello)
                {
                    Frame ackFrame;
                    ackFrame.command = Command::Ack;
                    ackFrame.payload = {static_cast<std::uint8_t>(Command::Hello), 0x00};
                    EncodeResult ackResult = ProtocolCodec::encode(ackFrame);
                    send_all(clientSocket, reinterpret_cast<const char*>(ackResult.bytes.data()), (int)ackResult.bytes.size());

                    Frame statusFrame;
                    statusFrame.command = Command::Status;
                    ackFrame.payload = {0x01};
                    EncodeResult statusResult = ProtocolCodec::encode(statusFrame);
                    send_all(clientSocket, reinterpret_cast<const char*>(statusResult.bytes.data()), (int)statusResult.bytes.size());
                }
            }
        }

        closesocket(clientSocket);
        closesocket(listenSocket);
    }

private:
    bool send_all(SOCKET sock, const char* data, int len)
    {
        int totalSent = 0;
        while (totalSent < len)
        {
            int sent = send(sock, data + totalSent, len - totalSent, 0);
            if (sent <= 0) return false;
            totalSent += sent;
        }
        return true;
    }

    SOCKET listenSocket = 0;
};


#endif //VIRTUALBENTH_WINSOCKRUNTIME_H