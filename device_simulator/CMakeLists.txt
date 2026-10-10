add_executable (device_simulator
    src/main.cpp
) 

target_link_libraries (device_simulator PRIVATE
    protocol
)

# 后面做 TCP 服务端要用 socket，Windows 下需要 Winsock 库 
if (WIN32) 
	target_link_libraries (device_simulator PRIVATE ws2_32) 
endif ()