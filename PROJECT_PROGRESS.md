# VirtualBenth 项目进度

> 最近核对：2026-10-17（Asia/Shanghai）
> 用途：给当前与新会话提供动态交接。开始工作时仍须核对 Git、实际文件和最新测试输出。

## 当前阶段

- 主线：项目一 VirtualBenth，W1 协议层。
- 学习会话：S5 `StreamParser` 已结束，进入 S6 测试与代码审查。
- 当前状态：`StreamParser` 三个成员函数及七个既有专用测试已有默认环境通过证据，S5 已完成；S6 的多帧非均匀分片测试已实现并通过静态审查，尚未构建或运行。
- 当前唯一目标：经用户授权后，在默认 VS2019 + RelWithDebInfo + Ninja 环境重新构建并运行新增分片测试。

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
- 2026-10-17 用户在获准使用默认 `cmake-build-relwithdebinfo` 重新构建并运行完整协议测试后报告“测试都通过了”；该结果覆盖当前 decode、StreamParser 正常路径、重同步和最大帧临界测试。此条属于用户提供的默认环境验收证据，助手未直接看到完整命令、用例数和断言数输出。

## 已实现但未验证

- `tests/test_stream_parser.cpp` 已增加确定性非均匀分片测试：连续输入 HELLO、payload 内含 `A5 5A` 的 START 和 STOP，并用固定块大小跨 magic、帧头、payload、CRC 与帧边界分片；累计结果后检查三帧顺序、字段、payload 和最终空缓存。2026-10-17 静态审查未发现明显覆盖缺口，尚未构建或运行。

## 待处理/已知问题

- 助手所在执行环境此前三次构建均在编译 `protocol_codec.cpp` 和 `test_protocol_codec.cpp` 时失败，错误为 MSVC `C1902`（程序数据库管理器不匹配），没有产生 C++ 源码诊断；用户自己的 VS2019 环境不复现并已编译通过，因此该问题按执行环境差异记录，不再阻断 S5。
- 助手执行环境的 VS2019 工具链目录中 `cl.exe`、`mspdb140.dll`、`mspdbcore.dll`、`mspdbsrv.exe` 为 14.29.30159；同一安装的 `Common7/IDE` 中 `mspdbcore.dll`、`mspdbsrv.exe` 为 14.29.30158。临时前置正确工具目录和清理目标生成 PDB 后仍复现。
- 用户本次只报告测试全部通过，未提供完整命令、CTest 数量和 Catch2 用例/断言数；进度中保留“用户提供的验收证据”限定，不写成助手直接复核。

## 当前工作区实现

- encode 基线为 `a230d26`；decode 基线为 `e053eeb`；`StreamParser` 第一版实现为 `5178d96`，静态审查修正及文档更新为 `a7009fe`，第一批测试为 `6500c34`，最大帧临界测试最新提交为 `878fced`。
- 2026-10-17 开始核对时核心代码工作区干净，本地 `main`、`origin/main` 与 `HEAD 878fced` 一致；当前未提交修改仅为本轮维护的 `PROJECT_PROGRESS.md` 和 `Doc/自测题.md`。
- `protocol/include/protocol/protocol_codec.h`：已包含 encode/decode 结果接口，并明确流式状态与业务语义不属于 decode。
- `protocol/src/protocol_codec.cpp`：encode 校验可发送版本/命令；decode 校验精确帧结构与 CRC，并保留线上原始版本/命令。
- `protocol/CMakeLists.txt`：已把 `src/protocol_codec.cpp` 和 `src/stream_parser.cpp` 加入 `protocol` 静态库。
- `tests/test_protocol_codec.cpp`：已编写 HELLO/START 精确示例，以及 4096B、4097B、Invalid、未知命令、未知版本场景；最新完整测试已有 VS2022 Debug 直接输出和 2026-10-17 用户报告的默认环境通过证据。
- `tests/CMakeLists.txt`：已把 `test_protocol_codec.cpp` 和 `test_stream_parser.cpp` 加入测试目标。
- `tests/test_stream_parser.cpp`：已加入三个正常路径、三个重同步用例和一个最大帧临界用例，均通过静态复审，并有 2026-10-17 用户报告的默认环境通过证据。
- `tests/test_stream_parser.cpp` 当前另有一个未提交的 S6 确定性非均匀分片测试，已静态审查、尚未构建或运行。
- `protocol/include/protocol/stream_parser.h`：已冻结最小接口、缓存成员及完整解析/重同步伪代码。
- `protocol/src/stream_parser.cpp`：已实现三个成员函数；`a7009fe` 确保 magic 位于缓存起点后才按固定偏移读帧头，并补充关键不变量注释。

## 本轮已处理

1. 对照 `Doc/协议文档.md` 和规划文档复核 S5 前置边界，发现当前 decode 拒绝未知命令会使业务层无法响应 `UNKNOWN_CMD`。
2. 按协议文档的权威优先级修正：decode 只拒绝结构/CRC 错误，未知版本/命令作为完整 Frame 交给业务层；encode 的发送白名单保持不变。
3. 更新相应测试并在 VS2022 Debug 重新构建、运行 CTest 和 Catch2，结果为 1/1、27 个用例、113 个断言通过。
4. 未运行最新改动的 VS2019 + RelWithDebInfo 构建或测试；等待用户下次默认环境构建时复核，但不阻断已获授权的 S5 设计。
5. 2026-10-15 完成 `StreamParser` 第一版接口与伪代码静态审查：接口可保留，伪代码因未使用持久缓存、未循环重同步且职责越界而暂未通过；本轮未运行构建或测试。
6. 2026-10-15 第二轮审查发现 magic 仍未搜索、半包 `continue` 会空转、协议语义和 CRC 重复处理、成功帧未精确消费；按用户授权已直接修正为声明式头文件和完整伪代码。修正后静态审查通过，本轮未运行构建或测试。
7. 2026-10-16 审查用户提交的 `StreamParser` 实现，发现找到 magic 后未消费噪声会错误读取长度；按用户授权直接修正 magic 对齐、无 magic 时保留末尾 1B、显式长度类型、候选帧命名和核心注释。修改后完成静态复核，未运行构建或测试。
8. 2026-10-16 静态审查第一批 StreamParser 测试：协议向量和 CMake 登记正确，但发现三处断言覆盖不足，当前测试尚不能证明精确帧数和单字节半包不会产生输出；未运行构建或测试。
9. 2026-10-16 用户修正三处关键断言后完成复审；按授权直接补齐标准库头、`std::size_t` 索引、不可变测试向量、HELLO 空 payload 断言、CMake 缩进和文件末尾换行。第一批测试静态复审通过，未运行构建或测试。
10. 2026-10-16 复审第二批重同步测试，发现 CRC 错帧后恢复出的 START 被错误断言为空 payload；按授权改为从合法 HELLO 翻转 CRC 位，并检查 START 完整 payload，同时整理测试名称和标签。静态复审通过，未运行构建或测试。
11. 2026-10-17 核对 Git 与最新提交 `878fced`，确认最大 4096B payload 的 4103B/1B 临界拆包测试已经提交；对照协议第 3、8、10 节完成静态复审，未发现阻断项。默认环境缓存再次确认为 Ninja、RelWithDebInfo、MSVC 19.29.30159（工具集目录 14.29.30133）；本轮尚未获测试许可，未运行构建或测试。
12. 2026-10-17 用户随后报告默认环境测试全部通过；按用户提供的验收证据结束 S5。核对现有测试后确认下一缺口是多帧字节流的可复现非均匀分片，进入 S6，不提前开始模拟器。
13. 2026-10-17 按用户授权直接完成 S6 首个测试：用固定 `{1, 4, 2, 7, 1, 5, 3, 6}` 分块喂入 HELLO + START + STOP，分片跨越帧边界并拆开 START payload 内的 `A5 5A`；静态检查通过，本轮未运行构建或测试。

## 下一步流程

1. 先取得用户对构建和运行测试的明确许可。
2. 使用默认 `cmake-build-relwithdebinfo` 重新构建 `protocol_tests`，再运行 CTest 与测试程序。
3. 根据真实输出更新验证状态；成功后再选择 S6 的下一项最小测试缺口。

## 暂不开始

- 设备模拟器、Qt GUI、SQLite、ONNX。
- Modbus（未来选做，不插队）。

