# VirtualBenth 项目进度

> 最近核对：2026-10-13（Asia/Shanghai）  
> 用途：给当前与新会话提供动态交接。开始工作时仍须核对 Git、实际文件和最新测试输出。

## 当前阶段

- 主线：项目一 VirtualBenth，W1 协议层。
- 学习会话：S4 `ProtocolCodec::encode()` 实现与测试已完成，默认环境复验后进入 `ProtocolCodec::decode()`。
- 当前状态：encode 已通过 VS2022 Debug 补充验证，但尚未用项目默认的 VS2019 + RelWithDebInfo 对最新工作区复验。
- 当前唯一目标：在获得用户许可后，使用 `cmake-build-relwithdebinfo` 重新构建并运行协议测试；通过后开始设计 decode。

## 默认构建环境

- 编译器工具链：Visual Studio 2019，当前缓存为 MSVC 14.29.30133。
- 构建配置：`RelWithDebInfo`。
- 当前 CMake 生成器：Ninja。
- 默认构建目录：`cmake-build-relwithdebinfo`。
- `build` 目录使用 Visual Studio 17 2022 多配置生成器，只作为补充验证环境。

## 已冻结的协议要点

- magic：`A5 5A`；版本：`0x01`；多字节字段显式小端。
- `payload_len` 最大 4096B；完整帧固定开销 8B，最大 4104B。
- CRC16-Modbus 覆盖 `version + command + payload_len + payload`，即从完整帧偏移 2 开始的 `4 + payload_len` 字节；不含 magic 和帧尾 CRC。
- DATA 固定字段 15B，协议允许的最大 `cnt` 为 1020。
- DATA 的 `ts` 使用 UTC Unix Epoch 微秒绝对时间；持续时间和超时仍使用单调时钟。
- ACK 表示控制命令在对端应用层处理成功；STATUS 表示设备当前状态，不能互相替代。
- V1 同一连接最多一个待确认控制命令，ACK 超时为 2000ms。
- 非法 `payload_len > 4096` 不等待 payload；解析器从当前 magic 起滑动 1 字节重新同步。

完整契约及测试向量见 `Doc/协议文档.md`，本摘要不能替代协议文档。

## 已验证

- CMake/Visual Studio 2022 Debug 构建曾成功。
- 在进入当前 S4 修改前，CTest 为 1/1 通过；Catch2 为 5 个测试用例、21 个断言通过。
- `Frame` 的命令值/默认值与 CRC16-Modbus 独立测试在上述结果中已验证。
- 2026-10-13 在补充环境执行 `cmake --build build --config Debug --target protocol_tests` 成功；构建日志确认重新编译了 `protocol_codec.cpp` 和 `test_protocol_codec.cpp`。
- 2026-10-13 在补充环境执行 `ctest --test-dir build -C Debug --output-on-failure`：1/1 通过，0 失败。
- 2026-10-13 在补充环境直接运行 `build/tests/Debug/protocol_tests.exe`：12 个测试用例、43 个断言全部通过。
- 上述结果证明当前 encode 在 VS2022 Debug 下通过，但在默认 VS2019 + RelWithDebInfo 下仍待复验。

## 当前工作区实现

- `protocol/include/protocol/protocol_codec.h`：已加入 `EncodeError`、`EncodeResult` 和 `ProtocolCodec::encode()` 接口。
- `protocol/src/protocol_codec.cpp`：已加入版本、命令、payload 上限检查，以及帧头、payload、CRC 的编码逻辑。
- `protocol/CMakeLists.txt`：已把 `src/protocol_codec.cpp` 加入 `protocol` 静态库。
- `tests/test_protocol_codec.cpp`：已编写 HELLO/START 精确示例，以及 4096B、4097B、Invalid、未知命令、UnsupportedVersion 场景；VS2022 Debug 已通过，默认环境待复验。
- `tests/CMakeLists.txt`：已把 `test_protocol_codec.cpp` 加入测试目标。

## 本轮已处理

以下修改已经过静态审查，并由上述 VS2022 Debug 补充环境确认；默认环境仍待复验：

1. 空 payload 时跳过 `std::memcpy`，并在复制逻辑后统一推进 `pos`。
2. 已显式包含直接使用的标准库头，并把长度和位置类型改为 `std::size_t`。
3. 已使用合法命令白名单拒绝 `Invalid`、未知值和预留值，并增加 `0x55` 测试。
4. UnsupportedVersion 测试只保留版本一个无效条件；全部错误用例检查失败状态、错误码和空输出。
5. 4096B 边界测试已改名，并检查帧总长、长度字段及 payload 首尾边界。

当前仍有已暂存与未暂存修改并存；操作前先查看 `git status`、`git diff` 和 `git diff --cached`，不要覆盖用户工作。

## 下一步流程

1. 取得用户许可后，用 `cmake-build-relwithdebinfo` 构建 `protocol_tests`。
2. 在该目录运行 CTest 和 Catch2，记录 VS2019 + RelWithDebInfo 的真实结果。
3. 默认环境通过后，明确 `DecodeError` / `DecodeResult`，再由用户实现第一版完整帧 decode。

## 暂不开始

- `StreamParser`、设备模拟器、Qt GUI、SQLite、ONNX。
- Modbus（未来选做，不插队）。

