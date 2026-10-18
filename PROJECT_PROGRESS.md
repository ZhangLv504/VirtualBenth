# VirtualBenth 项目进度

> 最近核对：2026-10-18（Asia/Shanghai）
> 用途：给当前与新会话提供动态交接。开始工作时仍须核对 Git、实际文件和最新测试输出。

## 当前阶段

- 主线：项目一 VirtualBenth，W2 模拟器起步。
- 学习会话：S7 模拟器起步已通过构建、协议回归和 NetAssist 手工联调验收。
- 当前状态：`protocol`、`ProtocolCodec`、`StreamParser` 及当前全部协议测试已在默认 MSVC 2022 + RelWithDebInfo 环境通过；`device_simulator` 已完成单客户端 Winsock 服务端与 HELLO 最小闭环，合法 HELLO、版本不匹配和未知命令三条路径均获得真实 TCP 字节证据。
- 当前唯一目标：进入 W2 的下一个小里程碑，先冻结 START/STOP 的最小会话状态与响应矩阵，再实现控制状态机；暂不开始 DATA 周期推流。

## 默认构建环境

- 编译器工具链：Visual Studio 2022，当前缓存为 MSVC 19.38.33145.0（工具集目录 14.38.33130）。
- 构建配置：`RelWithDebInfo`。
- 当前 CMake 生成器：Visual Studio 17 2022，多配置、x64。
- 默认构建目录：`build`；构建使用 `--config RelWithDebInfo`，CTest 使用 `-C RelWithDebInfo`。
- `cmake-build-relwithdebinfo` 为旧 VS2019 + Ninja 历史构建树，不再作为当前默认环境验收门槛。

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
- 2026-10-17 助手再次执行默认环境构建 `cmake --build cmake-build-relwithdebinfo --target protocol_tests --verbose`，VS2019 在包括新增测试在内的所有编译单元统一报 MSVC `C1902`，没有产生 C++ 源码诊断；该结果仍判定为助手执行环境问题。
- 2026-10-17 核对 `build/CMakeCache.txt` 后确认补充环境为 Visual Studio 17 2022、x64、MSVC 19.38.33145.0；执行 `cmake --build build --config Debug --target protocol_tests --verbose` 成功，日志确认重新编译 `stream_parser.cpp` 和 `test_stream_parser.cpp`，0 警告、0 错误。
- 2026-10-17 执行 `ctest --test-dir build -C Debug --output-on-failure`：1/1 通过；直接运行 `build/tests/Debug/protocol_tests.exe`：35 个测试用例、182 个断言全部通过。
- 2026-10-17 用户将默认环境切换为 MSVC 2022 + RelWithDebInfo；核对 `build/CMakeCache.txt` 确认 Visual Studio 17 2022、x64、MSVC 19.38.33145.0，并支持 `RelWithDebInfo` 配置。
- 2026-10-17 执行 `cmake --build build --config RelWithDebInfo --target protocol_tests --verbose` 成功；日志确认 Catch2、`crc16.cpp`、`protocol_codec.cpp`、`stream_parser.cpp` 及全部测试源以 C++17、RelWithDebInfo 参数重新编译，0 警告、0 错误。
- 2026-10-17 执行 `ctest --test-dir build -C RelWithDebInfo --output-on-failure`：1/1 通过，0 失败；直接运行 `build/tests/RelWithDebInfo/protocol_tests.exe`：35 个测试用例、182 个断言全部通过。
- 2026-10-17 新增全部二段切点测试后执行 `cmake --build build --config RelWithDebInfo --target protocol_tests --verbose` 成功；日志确认 `test_stream_parser.cpp` 由 MSVC 14.38 以 C++17、RelWithDebInfo 参数重新编译并重新链接，0 警告、0 错误。
- 2026-10-17 随后执行 `ctest --test-dir build -C RelWithDebInfo --output-on-failure`：1/1 通过，0 失败；直接运行 `build/tests/RelWithDebInfo/protocol_tests.exe`：36 个测试用例、268 个断言全部通过。
- 2026-10-18 核对 `build/CMakeCache.txt` 和编译器识别文件：生成器为 Visual Studio 17 2022、x64、多配置，MSVC 19.38.33145.0，工具集目录 14.38.33130；继续使用 `RelWithDebInfo`。
- 2026-10-18 执行 `cmake --build build --config RelWithDebInfo --target device_simulator protocol_tests --verbose` 成功；`main.cpp`、`WinsockRuntime.cpp` 以 C++17、RelWithDebInfo 实际重新编译并链接 `device_simulator.exe`，0 警告、0 错误。
- 2026-10-18 执行 `ctest --test-dir build -C RelWithDebInfo --output-on-failure`：1/1 通过，0 失败；直接运行测试程序：36 个测试用例、268 个断言全部通过。
- 2026-10-18 13:53:28 用户使用 NetAssist 向 `127.0.0.1:12345` 发送合法 HELLO `A5 5A 01 01 00 00 50 18`，服务端一次接收记录返回精确 19B：ACK(HELLO, OK) `A5 5A 01 06 02 00 01 00 89 E2` 与 STATUS(READY) `A5 5A 01 05 01 00 01 89 0C`。
- 2026-10-18 13:54:14 用户发送版本 2 HELLO `A5 5A 02 01 00 00 50 5C`，服务端返回精确 ERROR(VER_MISMATCH) `A5 5A 01 7F 02 00 02 00 14 D8`，随后主动断开，符合协议。
- 2026-10-18 13:56:33 用户发送未知命令 `0x55` 帧，服务端返回精确 ERROR(UNKNOWN_CMD) `A5 5A 01 7F 02 00 01 00 14 28`；13:56:54 在同一连接继续发送合法 HELLO 后仍收到正确 19B ACK+READY，证明未知命令不会关闭或破坏连接。

## 已实现但未验证

- 当前没有尚未验证的 S7 代码修改；最新实现已包含在 `f091fef`，并已有默认环境构建、协议回归及三条 NetAssist 联调证据。
- NetAssist 联调目前是人工验收记录，尚未固化为仓库内可重复运行的自动化端到端测试。

## 待处理/已知问题

- 旧 VS2019 + Ninja 构建树在助手环境持续报 MSVC `C1902`，原因指向 14.29 PDB 工具组件版本不一致；该问题保留为历史环境记录，但 VS2019 已不再是当前默认门槛，不阻断后续主线。
- 当前仍是单客户端、单次 `accept()` 的 S7 最小实现；连接结束后服务端进程退出，尚不能连续接受新客户端。
- START/STOP 状态机和 DATA 推流尚未实现；当前对已定义但尚未处理的非 HELLO 命令返回 `BAD_STATE`。
- `send_all()` 的部分发送分支和各 Winsock 失败清理路径已有代码审查与构建证据，但尚未通过故障注入强制触发。

## 当前工作区实现

- encode 基线为 `a230d26`；decode 基线为 `e053eeb`；`StreamParser` 第一版实现为 `5178d96`，静态审查修正及文档更新为 `a7009fe`，第一批测试为 `6500c34`，最大帧临界测试为 `878fced`，S6 确定性非均匀分片测试最新提交为 `bd8ed48`。
- 2026-10-18 联调前核对，本地 `main`、`origin/main` 与 `HEAD f091fef` 一致；`f091fef` 已包含完善后的服务端实现及扩充注释。当前未提交修改仅为本轮维护的 `PROJECT_PROGRESS.md` 和 `Doc/自测题.md`。
- `protocol/include/protocol/protocol_codec.h`：已包含 encode/decode 结果接口，并明确流式状态与业务语义不属于 decode。
- `protocol/src/protocol_codec.cpp`：encode 校验可发送版本/命令；decode 校验精确帧结构与 CRC，并保留线上原始版本/命令。
- `protocol/CMakeLists.txt`：已把 `src/protocol_codec.cpp` 和 `src/stream_parser.cpp` 加入 `protocol` 静态库。
- `tests/test_protocol_codec.cpp`：已编写 HELLO/START 精确示例，以及 4096B、4097B、Invalid、未知命令、未知版本场景；最新完整测试已有 VS2022 Debug 直接输出和 2026-10-17 用户报告的默认环境通过证据。
- `tests/CMakeLists.txt`：已把 `test_protocol_codec.cpp` 和 `test_stream_parser.cpp` 加入测试目标。
- `tests/test_stream_parser.cpp`：已加入三个正常路径、三个重同步用例和一个最大帧临界用例，均通过静态复审，并有 2026-10-17 用户报告的默认环境通过证据。
- `tests/test_stream_parser.cpp` 的 S6 确定性非均匀分片测试已提交为 `bd8ed48`，并通过当前默认 MSVC 2022 + RelWithDebInfo 环境验证。
- `tests/test_stream_parser.cpp` 的全部二段切点测试已提交为 `a13c2e4`，并通过静态审查及默认 MSVC 2022 + RelWithDebInfo 环境验证。
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
14. 2026-10-17 经用户授权执行 `cmake --build cmake-build-relwithdebinfo --target protocol_tests --verbose`，默认缓存确认为 Ninja、RelWithDebInfo、MSVC 19.29.30159；编译在多个无关源文件上统一报 `C1902` 后停止，退出码 1。因构建失败，未运行 CTest 和测试程序。
15. 2026-10-17 按用户授权切换到经缓存确认的 VS2022 Debug 补充环境；重新编译 `stream_parser.cpp` 与 `test_stream_parser.cpp` 成功，0 警告、0 错误，随后 CTest 1/1 通过，Catch2 35 个用例、182 个断言全部通过。
16. 2026-10-17 用户将默认环境改为 MSVC 2022 + RelWithDebInfo；核对多配置缓存后重新编译全部协议与测试源，0 警告、0 错误，随后 CTest 1/1、Catch2 35 个用例和 182 个断言全部通过。S6 非均匀分片测试据此完成默认环境验收。
17. 2026-10-17 按用户授权直接实现全部二段切点测试：遍历 13B START 帧内部的 12 个切点，以独立解析器验证半包保留和补齐恢复；静态审查通过，本轮尚未运行构建或测试。
18. 2026-10-17 经用户授权在默认 MSVC 2022 + RelWithDebInfo 环境重新编译新增测试，0 警告、0 错误；CTest 1/1、Catch2 36 个用例和 268 个断言全部通过。S6 据此完成，下一阶段进入 S7 模拟器起步。
19. 2026-10-17 对用户提交的首版 Winsock 服务端完成静态审查：确认网络主流程已经搭出，但发现未检查 Winsock/Socket API 结果、HELLO 响应 payload 不符合权威协议、未处理 `send()` 部分发送等阻断项；本轮未修改实现，未运行构建、测试或联调。
20. 2026-10-17 复审提交 `92c7a17` 后发现 STATUS payload 误写到 `ackFrame`，且 Winsock 返回值与清理缺口仍在；按用户授权直接修正为声明/实现分离、`WinsockRuntime` 与 `SocketHandle` RAII、逐项错误检查、loopback 监听、完整 `send_all()`、HELLO/ERROR 业务分发及明确退出码，并补充关键生命周期与 TCP 语义注释。静态复核及 `git diff --check` 未发现问题，本轮未运行构建、测试或联调。
21. 2026-10-17 根据用户反馈继续扩充模拟器注释：头文件明确公开接口、生命周期和当前阶段边界；实现文件解释匿名命名空间、协议常量、Socket 所有权、部分发送、帧构造、错误分发，以及 `socket → bind → listen → accept → recv → parse → process` 各阶段的不变量与失败语义；未改变功能范围。本轮未运行构建、测试或联调。
22. 2026-10-18 经用户许可核对默认缓存后构建 `device_simulator` 与 `protocol_tests`：模拟器实际重新编译并链接，0 警告、0 错误；随后 CTest 1/1、Catch2 36 个用例和 268 个断言全部通过。
23. 2026-10-18 用户使用 NetAssist 完成三条真实 TCP 验收：合法 HELLO 返回精确 ACK+READY，版本不匹配返回 VER_MISMATCH 后断开，未知命令返回 UNKNOWN_CMD 且连接可继续完成 HELLO。S7 最小 TCP 服务端闭环据此验收通过。

## 下一步流程

1. 对照协议冻结 START/STOP 的合法状态转换、payload 校验、ACK/STATUS 顺序和 ERROR 分支。
2. 只实现最小会话状态机，不同时加入 DATA 定时器、多客户端循环或线程。
3. 静态审查通过后，再经用户许可执行默认环境构建、协议回归和 NetAssist 控制命令联调。

## 暂不开始

- DATA 周期推流、多客户端循环、Qt GUI、SQLite、ONNX。
- Modbus（未来选做，不插队）。

