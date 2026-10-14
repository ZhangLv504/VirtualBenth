# VirtualBenth 项目进度

> 最近核对：2026-10-14（Asia/Shanghai）
> 用途：给当前与新会话提供动态交接。开始工作时仍须核对 Git、实际文件和最新测试输出。

## 当前阶段

- 主线：项目一 VirtualBenth，W1 协议层。
- 学习会话：S4 `ProtocolCodec` 已结束，进入 S5 `StreamParser` 设计。
- 当前状态：decode 在 VS2022 Debug 补充环境中通过 27 个测试用例、107 个断言；用户报告全部代码已在 VS2019 + RelWithDebInfo 编译通过，但该默认环境的本轮测试输出未由助手直接复核。
- 当前唯一目标：冻结 `StreamParser` 的职责、最小接口和重同步伪代码；暂不代写核心解析循环。

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
- 2026-10-13 核对 `cmake-build-relwithdebinfo/CMakeCache.txt` 与编译器识别文件：生成器为 Ninja，配置为 RelWithDebInfo，编译器为 VS2019 MSVC 14.29.30133（编译器版本标识 19.29.30159.0）。
- 2026-10-13 执行 `cmake --build cmake-build-relwithdebinfo --target protocol_tests --verbose`：命令成功；Ninja 报告 `no work to do`，表示用户此前完成构建后当前目标仍为最新，本次命令没有重新编译。`.ninja_log` 中存在 `protocol_codec.cpp.obj`、`test_protocol_codec.cpp.obj` 和 `protocol_tests.exe` 的构建记录，`ninja -t commands protocol_tests` 确认它们由上述 VS2019 工具链以 C++17、RelWithDebInfo 参数参与目标构建。
- 2026-10-13 执行 `ctest --test-dir cmake-build-relwithdebinfo --output-on-failure`：1/1 通过，0 失败。
- 2026-10-13 直接运行 `cmake-build-relwithdebinfo/tests/protocol_tests.exe`：12 个测试用例、43 个断言全部通过。
- 当前 encode 已通过项目默认环境验收；上述结论只覆盖 Frame、CRC 和 encode，不代表 decode 已实现。
- 2026-10-14 在 VS2022 Debug 补充环境执行 `cmake --build build --config Debug --target protocol_tests --verbose` 成功；日志确认重新编译 `protocol_codec.cpp` 和 `test_protocol_codec.cpp`，0 警告、0 错误。
- 2026-10-14 执行 `ctest --test-dir build -C Debug --output-on-failure`：1/1 通过，0 失败。
- 2026-10-14 直接运行 `build/tests/Debug/protocol_tests.exe`：27 个测试用例、107 个断言全部通过。
- 上述新增结果证明当前 decode 在 VS2022 Debug 补充环境通过，不替代默认 VS2019 + RelWithDebInfo 验收。
- 2026-10-14 用户报告当前全部代码已用 VS2019 + RelWithDebInfo 编译通过，并决定以该构建结果配合 VS2022 Debug 测试继续 S5；这条记录属于用户提供的默认环境构建证据，不等同于助手直接复核过默认环境测试输出。

## 已实现但未验证

- `protocol_codec.h` 已把 decode 冻结为只读接收一帧精确字节；错误集合覆盖短帧、magic、payload 上限、精确帧长、CRC、版本和命令，`DecodeResult` 默认保持失败状态。
- `tests/test_protocol_codec.cpp` 已增加 decode 契约测试：默认失败、HELLO/START 文档示例、0～5B 固定头边界、普通短帧、错误 magic、4097B、尾随字节、空/非空 payload CRC 错、CRC 优先级、合法 CRC 下的 Invalid/未知命令与非法版本，以及 4096B round-trip。
- `protocol_codec.cpp` 已完成精确单帧 decode：小端长度、4096B 上限、精确帧长、CRC、版本、命令、payload 和成功结果构造均已覆盖。
- 2026-10-14 用户授权直接修正剩余问题后，已完成第二轮静态审查；未再发现明显的端序、越界、CRC 范围或失败开放问题。
- decode 已通过补充环境测试；默认环境已由用户确认编译通过，但默认环境测试输出尚未由助手直接复核。

## 待处理/已知问题

- 助手所在执行环境此前三次构建均在编译 `protocol_codec.cpp` 和 `test_protocol_codec.cpp` 时失败，错误为 MSVC `C1902`（程序数据库管理器不匹配），没有产生 C++ 源码诊断；用户自己的 VS2019 环境不复现并已编译通过，因此该问题按执行环境差异记录，不再阻断 S5。
- 助手执行环境的 VS2019 工具链目录中 `cl.exe`、`mspdb140.dll`、`mspdbcore.dll`、`mspdbsrv.exe` 为 14.29.30159；同一安装的 `Common7/IDE` 中 `mspdbcore.dll`、`mspdbsrv.exe` 为 14.29.30158。临时前置正确工具目录和清理目标生成 PDB 后仍复现。
- 当前没有已知的 S4 代码静态阻断问题；仍不得把用户报告的默认环境编译通过写成助手已复核的默认环境测试通过。

## 当前工作区实现

- encode 相关实现和测试已提交为 `a230d26`（`ProtocolCodec::encode() 编码及其测试完成`）；当前工作区包含尚未提交的 decode、测试、注释和进度文档修改。
- `protocol/include/protocol/protocol_codec.h`：已加入 `EncodeError`、`EncodeResult` 和 `ProtocolCodec::encode()` 接口。
- `protocol/src/protocol_codec.cpp`：已加入版本、命令、payload 上限检查，以及帧头、payload、CRC 的编码逻辑。
- `protocol/CMakeLists.txt`：已把 `src/protocol_codec.cpp` 加入 `protocol` 静态库。
- `tests/test_protocol_codec.cpp`：已编写 HELLO/START 精确示例，以及 4096B、4097B、Invalid、未知命令、UnsupportedVersion 场景；旧版 encode 测试曾在默认环境通过，当前包含 decode 的 27 个用例在 VS2022 Debug 通过，默认环境当前版本仅有用户报告的编译成功证据。
- `tests/CMakeLists.txt`：已把 `test_protocol_codec.cpp` 加入测试目标。

## 本轮已处理

以下修改已经过静态审查；encode 曾由默认环境验证，当前包含 decode 的完整协议测试由 VS2022 Debug 补充环境确认，VS2019 + RelWithDebInfo 的当前完整代码由用户确认编译通过：

1. 空 payload 时跳过 `std::memcpy`，并在复制逻辑后统一推进 `pos`。
2. 已显式包含直接使用的标准库头，并把长度和位置类型改为 `std::size_t`。
3. 已使用合法命令白名单拒绝 `Invalid`、未知值和预留值，并增加 `0x55` 测试。
4. UnsupportedVersion 测试只保留版本一个无效条件；全部错误用例检查失败状态、错误码和空输出。
5. 4096B 边界测试已改名，并检查帧总长、长度字段及 payload 首尾边界。

encode 基线仍为提交 `a230d26`，本地 `main` 与 `origin/main` 一致；当前工作区另有用户的 decode 修改，以及进度文档和自测题修改。继续操作时不得覆盖这些未提交工作。

## 下一步流程

1. 先冻结 `StreamParser` 的最小接口与职责：接收任意分块输入，维护内部缓存，并一次返回 0～N 个完整 `Frame`。
2. 用户先写出查找 magic、等待头部、校验长度、截取精确帧、失败滑动 1B、成功消费整帧的伪代码。
3. 接口和伪代码审查通过后，再实现核心循环并补拆包、粘包、噪声和损坏帧重同步测试。

## 暂不开始

- 设备模拟器、Qt GUI、SQLite、ONNX。
- Modbus（未来选做，不插队）。

