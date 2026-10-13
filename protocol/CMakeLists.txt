# protocol 静态库：双端共享的协议层，刻意不依赖 Qt / socket 
add_library(protocol STATIC
    src/crc16.cpp
	src/protocol_codec.cpp
)

# 把 include/ 暴露给链接方（PUBLIC），双端才能 #include "protocol/xxx.h" 
target_include_directories(protocol PUBLIC 
	${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 把 "本库需要 C++17" 这个契约绑在库本身 
target_compile_features(protocol PUBLIC cxx_std_17)