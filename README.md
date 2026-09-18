# MaixCAM2 四舵测试：C++ 主机工具 + E907 C 固件

## 最新实机结果：E907 临时 RAM 控制已跑通

2026-09-18 后续查阅官方 SDK 后，已在 `10.11.105.1` 上实际启动 E907，并完成四路 PWM
寄存器控制、A53 独立读回、不同目标值、单路、停止、约 1 秒通信超时停机和 SIGTERM 清理测试。
日志实测超时停机等待约 999 ms。未连接舵机，未用示波器验证波形。

实现使用官方 CMM API 申请 16 KiB 独占内存，按照官方 BL1 启动序列启动小核，通过共享内存通信。
这是一套新实现的裸机 E907 验证程序，**不是原 RT-Thread 固件已运行，也不是官方现成的 Linux 加载工具**。
不修改内核或刷写启动分区；结束后恢复复位/时钟/PWM/引脚及 Linux 驱动绑定，释放内存。
因此，下方早先“未完成 E907 实机执行”的结论已被此次 RAM 路径验证更新；标准 RT-Thread 启动条件仍未满足。

- [完整实现、构建和使用说明](firmware/e907_live/README.md)
- [E907 实机四路控制日志](reports/e907_live_servo_final_2026-09-18.log)
- [E907 命令应答日志](reports/e907_live_probe_2026-09-18.log)
- [中断清理与最终恢复日志](reports/e907_signal_cleanup_final_2026-09-18.log)

已连接板卡上可以重新运行（临时文件重启后需重新上传）：

```sh
/tmp/servo_e907_probe/e907_live_probe --run-probe /tmp/servo_e907_probe/e907_probe.bin
/tmp/servo_e907_probe/e907_live_probe --run-servo-test /tmp/servo_e907_probe/e907_servo.bin
```

第二条命令实际输出四路 PWM，只用于当前已确认不接舵机的测试状态。
`start/stop/status` 是下方 RT-Thread 分支的 MSH 命令，不适用于这套 RAM 加载器。

## 2026-09-18：V1 底板与实机检查

本轮已通过 USB 网络 SSH 连接 `10.11.105.1`（主机密钥已核对），设备为
`maixcam2-75c4`，系统 `maixcam2-2026-05-29-maixpy-v4.12.5`。当前未接舵机。
**该系统未启用 `CONFIG_AX_RISCV_SUPPORT`，ax_riscv 驱动无绑定设备，未发现可用的小核加载入口。**
本轮完成主机模拟测试和板端只读检查，没有在 E907 上执行固件、刷机或输出 PWM。
详见 [本轮报告](reports/review_2026-09-18.md) 与
[实机诊断 JSON](reports/board_preflight_2026-09-18.json)。下方原有 Windows 验证记录是历史记录。

V1 原理图 H2 的四路信号脚按连接器顺序是：6=PWM7、9=PWM6、12=PWM4、15=PWM5。
当前软件舵机编号 1/2/3/4 对应 H2 的 12/15/9/6，不能按 H2 从上到下的顺序接编号。
H2 四组电源脚 5/8/11/14 接 VBATT，回流脚 4/7/10/13 经 Q3 接地（栅极为 5V）；
与 H2 第 2 脚的 5V 电源是不同网络。接舵机前需实测插头处电压和回路。
配置新增的 `carrier_revision`、`connector`、`connector_signal_pin` 是接线记录，不改变小核参数。

新版固件命令（仅在完成真实 SDK 集成、启动和资源配置后可用）：

```text
servo_bench status
servo_bench start 1
servo_bench start all
servo_bench stop
```

`start 1..4` 只初始化所选一路；`start` 等价于 `start all`。UART1 控制台阻止 3/4 路，
UART3 控制台阻止 1/2 路。V1 的 U20 引出 UART2，可以作为后续迁移候选，但必须一并处理
小核驱动、引脚、波特率及 Linux 对 UART2 的占用。USB 网络终端不是小核串口。
优化后，相同脉宽且寄存器已匹配时不再重启 PWM；改变脉宽仍有截短脉冲的可能，未验证无毛刺更新。

Linux 主机回归测试：

```sh
cmake -S cpp -B /tmp/servo-build -DBUILD_VIDEO=OFF
cmake --build /tmp/servo-build -j2
ctest --test-dir /tmp/servo-build --output-on-failure
```

新增测试直接包含真实固件/PWM 扩展源码，模拟 RT-Thread API 和寄存器，覆盖单路/四路、
控制台冲突、停止、初始化/设置/清理失败、线程失败和寄存器计算；不替代 E907 BSP 构建或波形测量。
`tools/board_preflight.py` 是独立的板端只读诊断工具，使用系统 Python 标准库；
它输出 JSON 并以 2 退出，表示尚需人工核实固件、内存预留与控制台，不会自动启动小核。

已将上一版应用工具从 Python 迁移到 C++17。E907 继续使用 C99 / RT-Thread。当前入口不依赖 Python 运行时；原 Python 版本归档于 archive_python，仅保留追溯，不再作为执行入口。MaixCDK/芯片官方 SDK 自身的构建脚本依赖仍按官方要求安装，这与板端应用语言不同。

## 目录与验证状态

| 文件 | 用途 | 当前验证 |
|---|---|---|
| cpp/src/bench.cpp | MaixCDK 四舵台架；PC模式仅空跑 | Windows程序已编译，108步空跑通过；MaixCDK硬件分支未交叉编译/上板 |
| cpp/src/collect.cpp、serial.hpp | Windows/Linux 原生串口采集与日志转换 | Windows已编译，合成日志回放通过；物理串口及Linux分支未验证 |
| cpp/src/video_measure.cpp | OpenCV C++ 视频逐帧测角与CSV | 源码已迁移；本机缺OpenCV C++开发库，尚未编译/GUI验证 |
| cpp/src/install_sdk.cpp | 将C固件集成到官方SDK，备份与版本校验 | Windows已编译，测试目录中安装/重复安装通过 |
| cpp/src/tests.cpp | 配置、限幅、解析、测角、CSV、SHA256测试 | 已编译并通过 |
| firmware/servo_bench.c | E907四舵测试、停止、日志 | C99主机语法检查通过；不是完整BSP编译 |
| firmware/pwm_us_extension.c.inc | 小核微秒PWM扩展，追加到官方驱动 | C99主机语法检查通过，尚未实测波形 |

`cpp/build/*.exe` 是此次实际编译的 Windows 工具。`validation_cpp_dry_run` 与 `validation_cpp_replay` 是**软件验证数据，不是舵机实测**。原气动数据库及飞盘模型未修改。

## 接线与参数

| 舵机 | 信号引脚 | PWM | E907枚举 |
|---|---|---|---|
| 1 | B2 | 4 | pwm_10 |
| 2 | B3 | 5 | pwm_11 |
| 3 | A30 | 6 | pwm_12 |
| 4 | A31 | 7 | pwm_13 |

供电用BEC，舵机与板卡共地，不用GPIO/3.3V引脚给电机供电。把**舵机插头处实测电压**填到config.json的bec_measured_v，不能用7.4V电池标称代替。你的厂家截图支持7.4V，标称0.07s/60°、1.8kgf·cm；它不等于实际带载性能。

默认333Hz、中位1500µs，依次测试四个舵机，每路序列：

```text
1500 → 1522 → 1500 → 1478 → 1500 → 1555 → 1500 → 1445 → 1500 µs
```

每步保持1.5秒，重复3轮；开始全体中位并等待3秒，总计约165秒。其他舵机保持中位。±22/55µs只是标称舵轴约±2°/5°的指令，不能当作实际舵面角。config.json保存每路中位、上下限、负载和电压，默认限幅1400–1600µs。

首次测试先固定装置、卸开连杆或确认中位附近没有机械干涉。正常完成最后回中再停PWM；中断/异常尝试直接停PWM，不额外强制回中。停PWM不保证所有舵机卸力；进程强制终止时不能保证清理动作执行。

## Windows：现在可以运行的C++程序

在本目录打开PowerShell：

```powershell
# 重新编译，不需要Python。默认调用本机g++。
.\cpp\build.ps1
# E907源码的C99语法检查（替身头文件，不是目标BSP构建）
.\cpp\check_firmware.ps1
# 只空跑并记录，不输出PWM；每次使用新目录
.\cpp\build\servo_bench.exe --config config.json --out runs\dry01
# 转换小核终端完整日志
.\cpp\build\servo_collect.exe --input terminal.log --out runs\rtt01
# 从已迁移的小核控制台串口读取；修改端口、波特率和电压为实测值
.\cpp\build\servo_collect.exe --port COM7 --baud 115200 --bec-v 6.0 --out runs\rtt02
```

PC版台架程序明确拒绝`--run`，不会假装控制MaixCAM2。串口采集不发送启动/停止命令，不要与另一个终端同时占用同一端口。关闭采集器不会停止板卡；可用独立终端保存完整串口日志后再转换。完整结束标记只表示日志收到结束，是否失败/人为停止查看end_record与每行rc。

核心工具也可以用CMake构建：

```bash
cmake -S cpp -B cpp/build_cmake -DBUILD_VIDEO=OFF
cmake --build cpp/build_cmake --config Release
ctest --test-dir cpp/build_cmake -C Release --output-on-failure
```

提供的MinGW构建已静态链接运行库；为兼容本机MinGW8.1，文件系统使用ghc/filesystem。第三方单头文件和许可来源在cpp/vendor/NOTICE.md。路径尽量使用ASCII，避免不同Windows终端代码页导致文件名编码不一致。

## MaixCAM2 大核：MaixCDK C++台架

工程放在`cpp/maixcdk`，保留整个cpp目录结构，因为main/CMakeLists.txt引用上级src/vendor。按官方MaixCDK文档配置MaixCAM2平台与交叉工具链，在此工程目录执行官方`maixcdk build`工作流；并将最新config.json复制到工程/设备。工程内已放入当前默认配置副本。

部署后，从SSH/USB终端运行生成的应用程序，传入：

```bash
./servo_bench_cpp --config config.json --out runs/servo01 --run
```

实际可执行文件名以SDK构建输出为准。该分支使用MaixCDK的PWM、pinmap和device_id API；用`duty_val`直接传入纳秒脉宽，检查设置返回值，并在退出时尝试禁用PWM、恢复引脚复用。这是Linux大核上的C++程序，**不是E907程序**。不要让它与小核固件同时运行并控制同一组PWM。

metadata.json保存配置；commands.csv保存每条指令、调用前后steady_clock时间和API状态；end.json保存complete/interrupted/error。实测角度列留空，API成功不等于已转到位。

## E907：全部保持C语言

E907侧仍是`firmware/servo_bench.c`及PWM驱动扩展，不使用C++、STL、JSON或OpenCV。默认参数在C源码中，**不读取config.json**。

用新C++安装工具替代原install_sdk.py：

```powershell
.\cpp\build\servo_install_sdk.exe --sdk D:\sdk\maix_ax620e_sdk --package .
```

将SDK路径替换为真实路径。工具先核对原驱动与目标应用，保存drv_pwm.c.servo_backup，再集成代码；拒绝覆盖内容不同的现有应用，不自动构建、不自动刷写。重复运行不重复追加扩展。回退可将该备份恢复为drv_pwm.c，并移除本工具新增的applications/servo_bench.c，再按原系统构建。

SDK的applications/SConscript会收录C源文件。沿用板型对应的官方riscv/Makefile、E907工具链与签名/镜像流程，不能把单个C文件直接当固件烧录。当前仍有两个上板前提：

1. **UART1与PWM6/7冲突。** A30/A31也是SDK默认小核控制台引脚。必须将小核控制台完整迁到另一组实际可用UART，包括引脚、驱动、波特率及RT_CONSOLE_DEVICE_NAME；仅改名称不够。当前C代码会在默认uart1控制台下拒绝启动。
2. **外设所有权。** Linux与小核不可同时驱动PWM4–7。PWM6还与B25照明灯共享，需要关闭相关占用并处理引脚复用。小核占用表不检查Linux占用。

解决后，在小核RT-Thread shell执行：

```text
servo_bench start
servo_bench stop
```

日志格式保持兼容：`SERVO,序号,舵机,PWM,脉宽,频率,前tick,后tick,返回码`。SDK默认tick100Hz，时间戳粒度10ms，不能据此宣称亚毫秒执行精度。

官方原PWM接口只有整数占空比；本地扩展补微秒脉宽，但采用停表/重装/启动，会有截短当前脉冲的可能。它用于初步转动和静态测角，必须测量更新波形，不能直接当作已验证的无毛刺飞行控制驱动。

## C++录像测角

与之前一致：手机/相机固定，正对转动平面，画面留转轴、零位参考和舵臂/舵面标记。视频逐帧测量；不能将舵轴角直接当作有连杆传动的舵面角。

安装与编译器ABI匹配的OpenCV C++开发库后：

```bash
cmake -S cpp -B cpp/build_video -DBUILD_VIDEO=ON -DOpenCV_DIR=/path/to/opencv/lib/cmake/opencv4
cmake --build cpp/build_video --config Release
# Windows常见exe在build_video或build_video/Release中
servo_video_measure --video servo1.mp4 --servo 1 --seq 6 --frame 120 --out measurements.csv
```

Windows使用MSVC版OpenCV时应选匹配的MSVC构建，不能将官方MSVC二进制直接链接到MinGW。Linux可安装系统libopencv-dev后构建。当前电脑配置时明确报告缺少OpenCVConfig.cmake，此工具尚未编译或交互验证。

- 依次点击转轴中心、零位方向、实际标记点。A/D前后1帧，J/L前后10帧，S保存，R重新标定，Q退出。
- 切换帧保留固定转轴/零位，需要重新点击实际标记点。相机/装置移动后重新标定。
- `--seq`是人工对应commands.csv的指令号，未知填0；seq6只是示例。换舵机或指令时重新运行并改变参数，可追加到同一测量CSV。
- measurements.csv保存原视频路径与SHA256、帧号、报告PTS、FPS、原始像素坐标、有符号角度；逆时针为正。原有Python测角CSV表头兼容。

不要将视频播放FPS自动当成拍摄FPS，也不要将主机日志接收时间当作PWM边沿。当前没有硬件同步标记，因此不自动输出“实测响应延迟”。视角偏斜、透视和镜头畸变也会带来角度误差。

## 本轮检查证据

- cpp/build/build.log：C++编译及核心测试输出。
- cpp/build/dry_run.log、validation_cpp_dry_run：108步空跑，全部测量列为空。
- validation_cpp_replay：合成日志转换结果，不是串口实测。
- cpp/build/sdk_fixture：安装器测试夹具，不是实际板卡SDK。
- cpp/build/video_configure.log：OpenCV开发库缺失的实际配置错误。

下一轮实测记录供电、每路实际舵面、负载、转向、卡滞/异响、回中、正负平台角度及三轮重复差。能转动不自动意味着精度合格；验收阈值和同步测量仍需实际试验确定。

## 官方来源

- [MaixCDK PWM C++ API](https://github.com/sipeed/MaixCDK/blob/main/components/peripheral/include/maix_pwm.hpp)
- [MaixCAM2 引脚映射实现](https://github.com/sipeed/MaixCDK/blob/main/components/peripheral/port/maixcam2/maix_pinmap.cpp)
- [MaixCDK 开发说明](https://wiki.sipeed.com/maixcdk/doc/zh/README.html)
- [MaixCAM2 官方系统 SDK](https://github.com/sipeed/maix_ax620e_sdk)
- [E907编译配置](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/config/rtconfig.py)
- [小核PWM驱动](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/drivers/pwm/drv_pwm.c)
