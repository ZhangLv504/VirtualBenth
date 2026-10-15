# VirtualBenth 项目进度

> 最近核对：2026-10-15（Asia/Shanghai）
> 用途：给当前与新会话提供动态交接。开始工作时仍须核对 Git、实际文件和最新测试输出。

## 当前阶段

- 主线：项目一 VirtualBenth，W1 协议层。
- 学习会话：S4 `ProtocolCodec` 已结束，进入 S5 `StreamParser` 设计。
- 当前状态：`StreamParser` 最小接口和核心流程伪代码已通过第二轮静态审查；头文件已整理为可编译的接口声明，尚未实现或加入 CMake。
- 当前唯一目标：用户依据已冻结伪代码，在 `stream_parser.cpp` 中实现 `feed()`、`reset()` 和 `buffered_size()`；本轮只完成核心循环，不提前扩展连接层超时或业务语义。

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
- CRC 正确且帧长合法的未知版本/命令属于业务语义事件，不是流式同步错误；decode 保留原始值，业务层负责 `VER_MISMATCH` / `UNKNOWN_CMD` 响应及连接处置。

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
- 2026-10-14 对照权威协议发现并修正语义边界：CRC 正确的未知版本/命令不再被 decode 当作结构错误拒绝，而是保留原始值交给业务层；VS2022 Debug 重新编译 `protocol_codec.cpp` 和 `test_protocol_codec.cpp` 成功，0 警告、0 错误。
- 2026-10-14 修正后执行 `ctest --test-dir build -C Debug --output-on-failure`：1/1 通过；直接运行测试程序：27 个测试用例、113 个断言全部通过。

## 已实现但未验证

- `protocol_codec.h` 已把 decode 冻结为只读接收一帧精确字节；错误集合只覆盖短帧、magic、payload 上限、精确帧长和 CRC，`DecodeResult` 默认保持失败状态。
- `tests/test_protocol_codec.cpp` 已增加 decode 契约测试：默认失败、HELLO/START 文档示例、0～5B 固定头边界、普通短帧、错误 magic、4097B、尾随字节、空/非空 payload CRC 错、CRC 优先级、合法 CRC 下保留 Invalid/未知命令与未知版本，以及 4096B round-trip。
- `protocol_codec.cpp` 已完成精确单帧 decode：小端长度、4096B 上限、精确帧长、CRC、原始版本/命令保留、payload 和成功结果构造均已覆盖。
- 2026-10-14 用户授权直接修正剩余问题后，已完成第二轮静态审查；未再发现明显的端序、越界、CRC 范围或失败开放问题。
- decode 的最新语义边界已通过补充环境测试；用户报告的默认环境编译成功早于该边界调整，因此最新改动在默认环境仍待用户后续构建时复核。
- `protocol/include/protocol/stream_parser.h` 已新增 `feed()`、`reset()`、`buffered_size()` 和内部 `buffer_`，并记录通过静态审查的核心流程伪代码；尚未实现、加入构建或测试。

## 待处理/已知问题

- 助手所在执行环境此前三次构建均在编译 `protocol_codec.cpp` 和 `test_protocol_codec.cpp` 时失败，错误为 MSVC `C1902`（程序数据库管理器不匹配），没有产生 C++ 源码诊断；用户自己的 VS2019 环境不复现并已编译通过，因此该问题按执行环境差异记录，不再阻断 S5。
- 助手执行环境的 VS2019 工具链目录中 `cl.exe`、`mspdb140.dll`、`mspdbcore.dll`、`mspdbsrv.exe` 为 14.29.30159；同一安装的 `Common7/IDE` 中 `mspdbcore.dll`、`mspdbsrv.exe` 为 14.29.30158。临时前置正确工具目录和清理目标生成 PDB 后仍复现。
- 当前没有已知的 S4 代码静态阻断问题；最新语义边界改动尚无默认环境验证证据，不得沿用改动前的用户构建结果。
- `StreamParser` 目前只有接口和伪代码，没有 `.cpp` 实现、CMake 源文件登记或专用测试，不能视为已实现或通过验收。
- `protocol/include/protocol/stream_parser.h` 当前 Git 状态为 `AM`：暂存区仍是最初空骨架，修订后的接口与伪代码位于未暂存工作区；提交前需由用户按意图重新暂存。

## 当前工作区实现

- encode 基线为 `a230d26`；decode、测试、核心注释和进度文档已由用户提交为 `e053eeb`（`ProtocolCodec::decode() 编码及其测试完成`），当前本地 `main` 与 `origin/main` 一致。
- 当前未提交修改把 decode 的边界调整为结构/CRC 解码，并同步修改测试、`Frame` 注释、进度文档和自测题；继续操作时不得覆盖。
- `protocol/include/protocol/protocol_codec.h`：已包含 encode/decode 结果接口，并明确流式状态与业务语义不属于 decode。
- `protocol/src/protocol_codec.cpp`：encode 校验可发送版本/命令；decode 校验精确帧结构与 CRC，并保留线上原始版本/命令。
- `protocol/CMakeLists.txt`：已把 `src/protocol_codec.cpp` 加入 `protocol` 静态库。
- `tests/test_protocol_codec.cpp`：已编写 HELLO/START 精确示例，以及 4096B、4097B、Invalid、未知命令、未知版本场景；旧版 encode 测试曾在默认环境通过，最新完整测试在 VS2022 Debug 通过，默认环境的用户构建证据早于本轮语义边界调整。
- `tests/CMakeLists.txt`：已把 `test_protocol_codec.cpp` 加入测试目标。
- `protocol/include/protocol/stream_parser.h`：已冻结最小接口、缓存成员及完整解析/重同步伪代码。

## 本轮已处理

1. 对照 `Doc/协议文档.md` 和规划文档复核 S5 前置边界，发现当前 decode 拒绝未知命令会使业务层无法响应 `UNKNOWN_CMD`。
2. 按协议文档的权威优先级修正：decode 只拒绝结构/CRC 错误，未知版本/命令作为完整 Frame 交给业务层；encode 的发送白名单保持不变。
3. 更新相应测试并在 VS2022 Debug 重新构建、运行 CTest 和 Catch2，结果为 1/1、27 个用例、113 个断言通过。
4. 未运行最新改动的 VS2019 + RelWithDebInfo 构建或测试；等待用户下次默认环境构建时复核，但不阻断已获授权的 S5 设计。
5. 2026-10-15 完成 `StreamParser` 第一版接口与伪代码静态审查：接口可保留，伪代码因未使用持久缓存、未循环重同步且职责越界而暂未通过；本轮未运行构建或测试。
6. 2026-10-15 第二轮审查发现 magic 仍未搜索、半包 `continue` 会空转、协议语义和 CRC 重复处理、成功帧未精确消费；按用户授权已直接修正为声明式头文件和完整伪代码。修正后静态审查通过，本轮未运行构建或测试。

## 下一步流程

1. 用户新建 `protocol/src/stream_parser.cpp`，按头文件伪代码实现三个成员函数；核心循环只操作 `buffer_`，完整候选帧交给 `ProtocolCodec::decode()`。
2. 实现后把源文件加入 `protocol/CMakeLists.txt`，先进行静态审查，不同时扩展设备模拟器或连接层。
3. 静态审查通过后添加拆包、粘包、噪声、CRC 错和超长长度测试，再按授权环境运行验证。

## 暂不开始

- 设备模拟器、Qt GUI、SQLite、ONNX。
- Modbus（未来选做，不插队）。

