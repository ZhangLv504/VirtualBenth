# VirtualBenth 项目进度

> 最近核对：2026-11-01（Asia/Shanghai）
> 用途：给当前与新会话提供动态交接。开始工作时仍须核对 Git、实际文件和最新测试输出。

## 当前阶段

- 主线：项目一 VirtualBenth，W2 模拟器起步。
- 学习会话：S7 模拟器起步及 HELLO/START/STOP 最小控制状态机已通过构建、协议回归和 NetAssist 手工联调验收。
- 当前状态：连接局部 `SamplingContext`、纯计算 `SampleBudget`、64KiB `OutboundQueue` 以及非阻塞控制响应路径已通过默认 VS2022 + `RelWithDebInfo` 构建、全部自动化回归和真实 TCP 字节联调。2026-11-01 工作区已撤回提前接入 `WinsockRuntime` 的 DATA 路径，并新增不依赖 Socket、tick、时钟或队列的纯 `data_frame_builder`；协议第 9.4 节精确向量及 1020/1021 样本边界测试已完成静态复审，但尚未构建或运行。运行时当前仍不生成 DATA。
- 当前唯一目标：经用户许可后，在默认 VS2022 + `RelWithDebInfo` 环境构建 `device_simulator` 和全部四组测试，运行 CTest 及 `data_frame_builder_tests`，取得纯 DATA 单帧组装的实际编译与行为证据；通过前不接入运行时 DATA。

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
- 2026-10-19 经用户许可再次核对默认缓存：Visual Studio 17 2022、x64、多配置、MSVC 19.38.33145.0（工具集目录 14.38.33130），并显式使用 `RelWithDebInfo`。执行 `cmake --build build --config RelWithDebInfo --target device_simulator protocol_tests --verbose` 成功；协议库、`WinsockRuntime.cpp`、`main.cpp` 和全部测试源实际重新编译并链接，两个目标均为 0 警告、0 错误。
- 2026-10-19 执行 `ctest --test-dir build -C RelWithDebInfo --output-on-failure`：1/1 通过，0 失败；直接运行 `build/tests/RelWithDebInfo/protocol_tests.exe`：36 个测试用例、268 个断言全部通过。
- 2026-10-21 再次核对 `build/CMakeCache.txt`：生成器为 Visual Studio 17 2022、x64、多配置并包含 `RelWithDebInfo`；实际构建日志确认使用 MSVC 14.38 工具集和 C++17。执行 `cmake --build build --config RelWithDebInfo --target device_simulator protocol_tests --verbose` 成功，两个目标均为 0 警告、0 错误。
- 2026-10-21 执行 `ctest --test-dir build -C RelWithDebInfo --output-on-failure`：1/1 通过，0 失败；直接运行 `build/tests/RelWithDebInfo/protocol_tests.exe`：36 个测试用例、268 个断言全部通过。
- 2026-10-21 用户提供同一 NetAssist 连接的连续记录：HELLO 精确返回 ACK(HELLO)+STATUS(READY)，START 精确返回 ACK(START)+STATUS(RUNNING)，约 15.1 秒后发送 STOP 仍精确返回 ACK(STOP)+STATUS(IDLE)，且收发时间戳没有可见延迟。由此确认 Running 状态下反复执行临时 20ms DATA tick 不会阻断当前控制命令处理；期间无 DATA 符合当前骨架设计。
- 2026-10-22 核对 `build/CMakeCache.txt` 确认 Visual Studio 17 2022、x64、多配置并包含 `RelWithDebInfo`；实际构建日志确认使用 MSVC 14.38 与 C++17。执行 `cmake --build build --config RelWithDebInfo --target device_simulator protocol_tests --verbose` 成功，两个目标均为 0 警告、0 错误。
- 2026-10-22 执行 `ctest --test-dir build -C RelWithDebInfo --output-on-failure`：1/1 通过，0 失败；直接运行 `build/tests/RelWithDebInfo/protocol_tests.exe`：36 个测试用例、268 个断言全部通过。
- 2026-10-22 用户提供同一 NetAssist 连接的连续日志：HELLO→READY、第一次 START→RUNNING、STOP→IDLE、第二次 START→RUNNING、第二次 STOP→IDLE，五组响应均逐字节符合协议。第二次 START 未重新 HELLO 仍成功，证明会话确实从 Idle 重新进入 Running；两段 Running 分别持续约 16.6 秒和 11.7 秒，STOP 均无可见阻塞。当前无 DATA 符合阶段边界。
- 2026-10-19 用户提供 NetAssist 字节记录并明确确认来自同一 TCP 连接：HELLO 返回精确 ACK(HELLO)+STATUS(READY)，START 返回精确 ACK(START)+STATUS(RUNNING)，STOP 返回精确 ACK(STOP)+STATUS(IDLE)，随后再次 START 返回精确 ACK(START)+STATUS(RUNNING)；重复 HELLO 连续两次均返回精确 `ERROR(BAD_STATE)` `A5 5A 01 7F 02 00 04 00 17 78`，第二次仍能收到响应，证明第一次 BAD_STATE 后连接保持。粘贴的时间戳顺序不连续，因此“同一连接”依据用户明确确认记录，而非仅由日志时间戳推断。
- 2026-10-19 22:23–22:27 用户在一条连续 NetAssist 连接中依次验证：HELLO→READY、START→RUNNING、重复 START→BAD_STATE、STOP→IDLE、重复 STOP→BAD_STATE、采样率为 0 的 START→BAD_PARAM、合法 START→RUNNING、非空 payload 的 STOP→BAD_PARAM、合法 STOP→IDLE。每个响应帧字节均与协议预期完全一致；每次错误后的合法命令均成功，证明连接和原会话状态保持。HELLO/START/STOP 最小控制状态机据此完成手工端到端验收。
- 2026-10-22 在默认 VS2022、x64、MSVC 14.38、C++17、`RelWithDebInfo` 环境执行 `cmake --build build --config RelWithDebInfo --target device_simulator protocol_tests sample_budget_tests --verbose` 成功，三个目标均为 0 警告、0 错误；构建日志确认新 `sample_budget.cpp`、`test_sample_budget.cpp` 及修正后的 `WinsockRuntime.cpp` 实际参与编译。
- 2026-10-22 执行 `ctest --test-dir build -C RelWithDebInfo --output-on-failure`：2/2 通过，0 失败；直接运行 `protocol_tests.exe` 为 36 个用例、268 个断言通过，`sample_budget_tests.exe` 为 4 个用例、68 个断言通过。10Hz、10001Hz、单 tick 2500 样本分段和零采样率拒绝均取得自动化证据。
- 2026-10-22 在同一默认环境执行 `cmake --build build --config RelWithDebInfo --target device_simulator protocol_tests sample_budget_tests outbound_queue_tests --verbose` 成功，四个目标均为 0 警告、0 错误；构建日志确认 `outbound_queue.cpp` 与 `test_outbound_queue.cpp` 使用 MSVC 14.38、C++17、`RelWithDebInfo` 实际编译。
- 2026-10-22 执行 CTest：3/3 通过，0 失败；直接运行三个测试程序分别为协议 36 个用例/268 个断言、样本预算 4 个用例/68 个断言、发送队列 6 个用例/51 个断言全部通过。
- 2026-10-27 核对 `build/CMakeCache.txt` 与编译器识别文件：生成器为 Visual Studio 17 2022、x64、多配置，编译器为 MSVC 19.38.33145.0，并显式使用 `RelWithDebInfo`。执行 `cmake --build build --config RelWithDebInfo --target device_simulator protocol_tests sample_budget_tests outbound_queue_tests --verbose` 成功；构建日志确认 `WinsockRuntime.cpp`、`main.cpp`、`outbound_queue.cpp` 和队列测试实际重新编译，四个目标均为 0 警告、0 错误。
- 2026-10-27 执行 `ctest --test-dir build -C RelWithDebInfo --output-on-failure`：3/3 通过，0 失败；直接运行三个测试程序分别为协议 36 个用例/268 个断言、样本预算 4 个用例/68 个断言、发送队列 6 个用例/51 个断言全部通过。
- 2026-10-27 对 `protocol`、`device_simulator` 及两个测试辅助函数完成注释专项静态复核：所有结构体和枚举均补充了用途、字段/枚举值语义与关键不变量，函数注释覆盖处理思路、所有权、状态提交和失败边界；`git diff --check` 无错误。本轮只改注释和文档，未执行构建或测试，最近运行证据仍为上一条记录。
- 2026-10-28 复核用户提供的连续 NetAssist HEX 日志：HELLO、START(10000Hz, mask=0x07)、STOP 均收到精确的 ACK+STATUS；随后空 payload DATA、STATUS、ACK 三帧均结构完整、CRC 正确，并分别收到精确 `ERROR(BAD_STATE)` `A5 5A 01 7F 02 00 04 00 17 78`。该证据覆盖非阻塞控制响应字节和三种非法方向命令，但日志未包含错误后的再次 START→STOP，尚不能证明错误后连接与状态保持。
- 2026-10-28 用户在上述连接中未重新 HELLO，直接再次发送合法 START，收到精确 ACK(START)+STATUS(RUNNING)；约 20 秒后发送 STOP，收到精确 ACK(STOP)+STATUS(IDLE)。新连接的 AwaitHello 状态不允许直接 START，因此该结果证明原 TCP 会话在三次 BAD_STATE 后仍保持 Idle，解析器与非阻塞发送队列均可继续完成成功响应。至此当前非阻塞控制响应路径的真实 TCP 验收完整通过。

## 已实现但未验证

- NetAssist 联调目前是人工验收记录，尚未固化为仓库内可重复运行的自动化端到端测试。
- 工作区已恢复不可移动的单一 `SocketHandle`、单连接 `StreamParser` 和局部 `SessionState`；事件循环每轮重建 `readfds`，非 Running 状态永久等待控制字节，Running 状态按临时 20ms 单调时钟截止点设置 `select()` timeout。该实现已通过默认环境构建、协议回归和真实 TCP 控制路径联调；当前 tick 只推进截止时间，不生成或发送 DATA。
- 连接局部 `SamplingContext` 已通过静态复审、默认环境构建、协议回归和同一连接 START→STOP→START 控制路径联调。由于当前尚不发送 DATA，采样率值、通道掩码内容和序号清零仍没有线上可观察数据，后续 DATA 首帧测试需继续覆盖这些内部结果。
- 接受后的客户端 Socket 已切换为非阻塞模式，只在 `OutboundQueue` 非空时监听 `writefds`；每次可写只调用一次 `send()`，用真实返回值推进部分发送偏移，并把 `WSAEWOULDBLOCK` 视为可重试状态。ACK+STATUS 全部排空后才提交会话状态和采样上下文；版本不匹配的 ERROR 排空后再关闭。该实现已通过静态复审、`git diff --check`、默认环境构建、全部自动化回归和完整真实 TCP 控制路径联调；部分发送与 `WSAEWOULDBLOCK` 分支仍只具备静态/纯队列证据，尚未被真实网络强制触发。
- 纯 `data_frame_builder` 已在 `simulator_core` 中实现：显式输入 `chan_id/seq/ts/samples`，输出业务 `Frame`；整数和 float 位模式均显式按小端写入，不读取时钟、不修改序号、不操作队列。DATA 单帧上限已放入独立 `data_frame_limits.h`，组帧器不再依赖 tick 预算类型。协议第 9.4 节完整编码向量、1020 样本最大边界和 1021 样本拒绝测试已静态复审通过，但尚未构建或运行。

## 待处理/已知问题

- 旧 VS2019 + Ninja 构建树在助手环境持续报 MSVC `C1902`，原因指向 14.29 PDB 工具组件版本不一致；该问题保留为历史环境记录，但 VS2019 已不再是当前默认门槛，不阻断后续主线。
- 已验收的 `ed18169` 基线是单客户端、单次 `accept()`；`d68a768` 已改成多客户端循环，但该扩展不属于当前阶段且存在下列静态阻断项，不能视为已验收能力。
- 非阻塞 `send()` 的部分发送和 `WSAEWOULDBLOCK` 分支已在工作区实现，但尚未通过自动化故障注入强制触发；当前只能依赖静态审查，不能宣称这些分支已实测。
- 当前 20ms DATA tick 和 `SampleBudget` 只完成调度与样本数量计算，不代表已经实现协议 DATA；START 的采样率/通道掩码及独立序号状态已在工作区保存，但尚未生成 `chanId/seq/ts/cnt/samples`、UTC 时间戳或波形数据。
- 64KiB `OutboundQueue` 纯核心及网络控制路径已完成构建、自动化回归和真实 TCP 联调。空队列按实现不会加入 `writefds`；部分发送和 `WSAEWOULDBLOCK` 分支仍缺自动化故障注入证据，但不阻断先实现不依赖网络的 DATA 单帧组装核心。
- `Doc/项目规划V3.1.md` 第 2.4 节仍把 DATA 的 `ts` 描述为“模拟器启动后的单调时间”，与权威 `Doc/协议文档.md` 的“UTC Unix Epoch 微秒”冲突。按仓库优先级，线上 DATA 必须使用 UTC Epoch；`steady_clock` 仅用于本机节拍和超时。
- 2026-11-01 首版工作区 DATA 网络集成曾存在 `chanId`、`ts`、float 字节序、分段时间线、队列溢出和 STOP 优先级阻断项；当前工作区已整体撤回该运行时路径，改为纯组帧组件。上述网络集成问题保留为后续接入时的验收清单，不再阻断纯组件构建测试。

## 当前工作区实现

- encode 基线为 `a230d26`；decode 基线为 `e053eeb`；`StreamParser` 第一版实现为 `5178d96`，静态审查修正及文档更新为 `a7009fe`，第一批测试为 `6500c34`，最大帧临界测试为 `878fced`，S6 确定性非均匀分片测试最新提交为 `bd8ed48`。
- 2026-11-01 核对时本地 `main`、`origin/main` 与 `HEAD a8128b9` 一致；用户提交仍把 DATA 提前接入 `WinsockRuntime`。当前工作区按授权撤回该功能路径，新增 `data_frame_builder.h/.cpp`、`data_frame_limits.h`、独立测试及 CMake 登记；这些修正未暂存、未提交、未推送。
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
24. 2026-10-19 静态审查提交 `efe5eac` 与工作区 STOP 补丁：START 参数和 STOP payload 校验方向正确，但发现成功 STATUS 发送旧状态、重复 HELLO 标志拦截任意下一帧并错误关闭连接、状态被放入公开静态成员三个阻断项；本轮未修改核心实现，未运行构建或测试。
25. 2026-10-19 复审提交 `6767294` 后仍发现重复 HELLO 静态标志、STOP 状态码映射和状态提交时机问题；按用户授权直接修正为每连接局部状态、显式线上 STATUS、成功发送后提交状态，并移除 protocol 层内部状态枚举及入口调试输出。静态复核与 `git diff --check` 通过，本轮未运行构建或测试。
26. 2026-10-19 新会话核对确认上述修正已提交为 `e1f5180`，本地 `main`、`origin/main` 与 `HEAD` 一致且工作区干净；重新读取实际状态机源码与默认构建缓存后未发现新的静态阻断项。缓存仍为 Visual Studio 17 2022、x64、多配置、MSVC 19.38.33145.0（工具集目录 14.38.33130）；本轮尚未获构建或测试许可，因此未运行构建、CTest、测试程序或 TCP 联调。
27. 2026-10-19 经用户许可在默认 MSVC 2022 + RelWithDebInfo 环境重新编译 `device_simulator`、协议库和全部协议测试源，0 警告、0 错误；随后 CTest 1/1、Catch2 36 个用例和 268 个断言全部通过。最新状态机由此取得默认环境构建与协议回归证据，下一门槛为 NetAssist 真实 TCP 字节联调。
28. 2026-10-19 审查用户提供的 NetAssist 日志：START、STOP、再次 START 和重复 HELLO 的请求/响应字节全部符合协议；连续两次重复 HELLO 均收到 BAD_STATE，证明非致命错误后连接保持。由于成功 HELLO 的日志时间晚于 START→STOP→START，尚缺同一连接完整成功序列的可核对时间证据，本轮不提前宣告完整状态机验收通过。
29. 2026-10-19 用户明确确认上述 HELLO、START、STOP、再次 START 记录来自同一 TCP 连接；据此接受用户提供的连接身份补充证据，成功主路径和重复 HELLO 错误保持路径通过手工联调验收。粘贴的时间戳本身仍不构成连续顺序证据，进度记录保留这一证据来源差异。
30. 2026-10-19 审查 22:23–22:27 的连续 NetAssist 日志：HELLO、START、STOP 成功响应，重复 START/STOP 的 BAD_STATE，零采样率 START 与非空 STOP 的 BAD_PARAM 均为精确预期字节；每次错误后通过合法 STOP/START 继续成功转换，证明连接与原状态保持。S7 HELLO/START/STOP 最小控制状态机完成默认环境构建、协议回归和手工端到端验收。
31. 2026-10-20 静态审查提交 `d68a768` 及工作区注释补丁：可移动 Socket 所有权与逐连接解析状态方向本身合理，但实现提前扩展多客户端且没有 DATA 定时；发现单客户端协议/发送错误会退出整个服务端、阻塞发送会卡住事件循环、发送队列字段未接入、`FD_SETSIZE` 无边界和头文件契约过时等阻断项。当前审查不通过，本轮未运行构建、测试或联调。
32. 2026-10-20 用户明确授权助手实现修正：工作区移除 `ClientSession` 容器、Socket 移动语义、未使用发送队列和多客户端 accept 循环，恢复单连接所有权；加入基于 `steady_clock` 的临时 20ms 截止点和有界 `select()` timeout，只有成功 START 后启用 tick，STOP 后通过状态门控停用，下一次 START 重新建立节拍。同步更新头文件职责说明，静态复审与 `git diff --check` 通过；本轮未运行构建、测试或联调。
33. 2026-10-21 经用户以“下一步”授权执行验收：重新核对默认 `build` 缓存后，使用 VS2022、x64、MSVC 14.38、C++17 和 `RelWithDebInfo` 构建 `device_simulator` 与 `protocol_tests`，两个目标均为 0 警告、0 错误；随后 CTest 1/1、Catch2 36 个用例和 268 个断言全部通过。DATA tick 骨架取得构建与协议回归证据，下一门槛为同一真实 TCP 连接的 HELLO→START→STOP 联调。
34. 2026-10-21 审查用户提供的连续 NetAssist 日志：HELLO、START、STOP 的六个响应帧均逐字节符合协议；START 与 STOP 相隔约 15.1 秒，STOP 仍在日志同一毫秒收到 ACK+IDLE，证明 Running 状态反复 tick 后控制通路保持响应。DATA tick 骨架完成构建、回归与真实 TCP 联调验收。对照下一阶段资料时发现规划文档的 DATA 时间戳旧描述与协议冲突，明确以后者的 UTC Epoch 定义为准。
35. 2026-10-21 静态审查用户新增的 START 配置与 DATA 发送代码：实现范围超出当前“只保存采样上下文”小步，并存在多个阻断项。最直接的问题是 START payload 仅有索引 0～4，代码却在已发送 ACK+RUNNING 后读取索引 6～9，会抛出 `std::out_of_range` 并关闭连接；正常定时到期走 `ready == 0` 分支时又完全不发送 DATA。另有单一序号、重启不复位、样本数溢出/超长、逾期追赶阻塞控制通路及头文件契约过时等问题。本轮审查不通过，未运行构建、测试或联调。
36. 2026-10-21 用户提交 `1baabc1` 后要求“还有问题直接修改”：最新代码已修正 START 索引并改为 8 个序号，但仍提前生成 DATA，序号数组未初始化，且 `seqs_[chan_id]++ > UINT32_MAX` 对 `uint32_t` 永远不成立。按授权直接撤回全部 DATA 构造/发送及公开构造参数，新增连接局部 `SamplingContext`；START 成功后提交配置并清零逐通道序号，STOP 成功后清空，断线自然销毁。静态复审与 `git diff --check` 通过，本轮未运行构建、测试或联调。
37. 2026-10-22 用户以“下一步”许可执行验收：重新核对最新 Git、源码与默认构建缓存后，使用 VS2022、x64、MSVC 14.38、C++17 和 `RelWithDebInfo` 实际重新编译 `WinsockRuntime.cpp` 并链接 `device_simulator`，同时构建 `protocol_tests`，两个目标均为 0 警告、0 错误；随后 CTest 1/1、Catch2 36 个用例和 268 个断言全部通过。采样上下文取得构建与协议回归证据，下一门槛为同一连接再次 START 的真实 TCP 联调。
38. 2026-10-22 审查用户提供的 NetAssist 日志：同一连接依次完成 HELLO、START、STOP、再次 START、再次 STOP，五组 ACK/STATUS 响应均精确匹配协议。第二次 START 没有重新 HELLO 却从 Idle 成功进入 Running，证明控制会话能够复用；两次 STOP 均及时返回。连接局部采样上下文小步据此完成静态、构建、回归和真实 TCP 控制路径验收。后台模拟器在助手轮询前已结束且没有保留额外控制台输出，因此此条以客户端连续日志为证据。
39. 2026-10-22 对照 `Doc/协议文档.md` 与当前 `select()` 空 tick 实现，冻结 DATA 生成和发送边界：逻辑 tick 为 20ms；用 64 位整数 credit 保留低采样率及非整除余数；单帧 `cnt` 不超过 1020；过期 tick 丢弃而非无界追赶；逐通道序号仅在完整帧入队后递增；UTC 样本时间用整数余数推进。连续 DATA 不再沿用阻塞式 `send_all()`，实现前必须先建立非阻塞 Socket、64KiB 有界队列、部分发送进度与控制帧优先规则。本轮只更新设计文档，未修改功能代码，未运行构建、测试或联调。
40. 2026-10-22 静态审查最新提交 `0d56a91`：发现 `sample_credit` 在每轮 Running 循环中清零，无法保存跨 tick 余数；正常 `select()` 超时路径不生成 DATA；同时提前使用阻塞式 `send_all()`、未初始化的服务器采样字段和 8 位序号，超出已冻结的小步边界。按用户既有授权直接撤回这些 DATA/Socket 改动，保留用户的事件循环注释，并新增独立 `simulator_core`、`SampleBudget` 和自动化测试。
41. 2026-10-22 默认环境重新构建 `device_simulator`、`protocol_tests`、`sample_budget_tests` 成功，0 警告、0 错误；CTest 2/2 通过，两个测试程序分别为 36 个用例/268 个断言和 4 个用例/68 个断言全部通过；`git diff --check` 无错误。纯计算样本预算器小步完成，真实 DATA 仍未接入。
42. 2026-10-22 实现不依赖 Socket 的 `OutboundQueue`：默认容量 64KiB，控制帧可越过未开始的 DATA，已部分发送的帧必须补齐，DATA 容量不足时无副作用地拒绝，控制帧容量不足时可清除未开始 DATA，完整帧发送后才释放容量。新增 6 个自动化用例和 51 个断言；默认环境四目标构建 0 警告、0 错误，CTest 3/3 及全部直接测试通过，`git diff --check` 无错误。本轮未修改网络运行时，真实 DATA 仍未接入。
43. 2026-10-24 静态审查提交 `a2db9a9`：客户端未调用 `ioctlsocket(FIONBIO)`，响应仍全部走阻塞式 `send_all()`，`OutboundQueue` 未接入；`writefds` 却每轮无条件监听，使通常可写的 TCP Socket 导致 `select()` 立即返回并在不可读分支忙循环。另发现 STOP 的线上状态 `0x00` 被强转为内部 `AwaitHello`，产生空 STATUS payload；host 发 DATA/STATUS/ACK 时也被错误回复为 STATUS/ACK，而非 `BAD_STATE`。该提交静态审查不通过。
44. 2026-10-24 按用户既有授权直接修正：接受连接后切为非阻塞模式；ACK/STATUS/ERROR 编码后进入 `OutboundQueue`；队列非空才监听 `writefds`；一次可写只发送一次并保存偏移；`WSAEWOULDBLOCK` 保持状态；成功命令的状态和采样上下文延迟到完整响应排空后提交；恢复非法方向命令统一 `BAD_STATE`，版本错误响应排空后关闭。静态复审与 `git diff --check` 通过，本轮未运行构建、测试或 TCP 联调。
45. 2026-10-27 经用户许可重新核对默认缓存并构建 `device_simulator`、`protocol_tests`、`sample_budget_tests`、`outbound_queue_tests`：`WinsockRuntime.cpp` 等相关源文件实际重新编译，四个目标 0 警告、0 错误。CTest 3/3 通过；三个测试程序分别为 36 个用例/268 个断言、4 个用例/68 个断言、6 个用例/51 个断言全部通过。非阻塞控制响应路径取得默认环境构建与自动化回归证据。
46. 2026-10-27 尝试自动执行本机真实 TCP 字节联调：模拟器实际启动并输出监听 `127.0.0.1:12345`；沙箱内客户端连接被系统以访问权限错误拒绝，随后申请仅连接本机回环地址的额外权限但用户未批准。已终止模拟器进程，未遗留监听。此结果属于测试环境限制，不是协议联调通过证据，真实 TCP 门槛继续保留。
47. 2026-10-27 按用户要求完成项目注释专项：完善协议命令、编解码结果、流解析器、样本预算、发送队列以及 Winsock 内部状态机的结构体/枚举说明；公共和内部函数均补充整体处理思路、输入输出、所有权、状态变化和异常/失败边界；测试辅助函数说明其隔离测试变量的目的。逐项复核确认没有改变功能代码，`git diff --check` 无错误；本轮未获新的构建/测试授权，因此未重新编译或运行测试。
48. 2026-10-28 审查用户提供的连续 NetAssist 日志：HELLO、START、STOP 的六个响应帧逐字节符合协议；空 payload DATA、STATUS、ACK 均为结构和 CRC 正确的完整帧，并各自得到精确 BAD_STATE。日志尚未给出错误后的再次 START→STOP，因此本轮记为真实 TCP 部分通过，不提前宣告整个门槛完成。
49. 2026-10-28 用户继续使用原连接，未重新 HELLO 即成功完成 START→RUNNING、约 20 秒后 STOP→IDLE；四个响应帧逐字节符合协议。若连接曾重建，AwaitHello 状态会拒绝 START，因此这组证据完成了错误后连接保持、Idle 状态保持、解析同步和非阻塞成功响应的联合验收。当前 TCP 门槛通过，下一小步转入纯 DATA 单帧组装，不直接接入网络。
50. 2026-11-01 静态审查工作区 DATA 实现：改动没有建立纯组帧组件或权威示例测试，而是直接在 `WinsockRuntime.cpp` 接入 `SampleBudget`、系统时钟、序号、组帧和发送队列。发现 `chanId`/`ts` 线上字节错误、float 主机字节序依赖、分段时间线未推进、队列溢出策略冲突以及 DATA 积压可能延迟 STOP 等阻断项；审查不通过，本轮未运行构建或测试，也未修改用户核心实现。
51. 2026-11-01 用户提交后仍未建立纯组件，按其“还有问题直接修改”的授权收缩范围：撤回 `WinsockRuntime` 中全部 DATA 预算/组帧/入队逻辑，恢复已验收的控制基线；新增纯 `data_frame_builder`，通过 32/64 位整数逐字节小端写入和 `memcpy` 取得 float32 位模式后再小端写入；单帧上限抽到独立 `data_frame_limits.h`，避免组帧与 tick 预算互相依赖。新增协议第 9.4 节精确向量及 1020/1021 边界测试，并登记到 `simulator_core` 和 CTest。静态复核与独立 CRC 重算一致，本轮未运行构建或测试。

## 下一步流程

1. 获得用户构建/测试许可后，先核对 `build/CMakeCache.txt` 的 VS2022、x64、多配置和 MSVC 19.38。
2. 使用 `RelWithDebInfo` 构建 `device_simulator`、`protocol_tests`、`sample_budget_tests`、`outbound_queue_tests`、`data_frame_builder_tests`。
3. 运行 CTest 和新增测试程序；全部通过后再设计 DATA 时间线与序号生命周期，不直接接入完整网络推流。

## 暂不开始

- 多客户端循环、Qt GUI、SQLite、ONNX。
- Modbus（未来选做，不插队）。

