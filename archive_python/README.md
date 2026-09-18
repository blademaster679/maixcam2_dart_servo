# MaixCAM2 四舵机台架测试与视频实测记录

这套代码针对 PTK 7350 MG-D：默认 333 Hz、1500 µs 中位，四通道依次测试。分为 **E907 小核 C 固件**与**Linux/MaixPy 台架程序**，两者只能选一个控制 PWM。Python 台架程序不是小核固件。

当前交付状态：PC 逻辑测试、空跑和 C 语法检查已完成；没有连接 MaixCAM2/舵机，没有完成 E907 交叉编译、固件烧录或示波器验证。`validation_dry_run` 只有模拟指令，绝不是实测数据。已有飞盘和气动数据库不作修改。

## 接线与资源分配

| 舵机编号 | 信号针脚 | PWM编号 | 小核枚举 |
|---|---|---|---|
| 1 | B2 | 4 | pwm_10 |
| 2 | B3 | 5 | pwm_11 |
| 3 | A30 | 6 | pwm_12 |
| 4 | A31 | 7 | pwm_13 |

以上对应关系来自 MaixCDK 的 MaixCAM2 pinmap 源码；编号只是台架编号，仍需记录它们分别连接哪片舵面。舵机电源接 BEC 输出，信号地、BEC 地、开发板地共地；不要从 GPIO/3.3 V 引脚给电机供电。用万用表记录**舵机插头处**电压，填写 config.json 的 bec_measured_v，不能把电池标称 7.4 V 自动当作舵机供电。

你的厂家截图明确支持 7.4 V，标称 0.07 s/60°、1.8 kgf·cm；500–2500 µs、中位1500 µs、333 Hz。之前仅根据网页文字排除7.4 V的判断应更正。角度/速度仍需要实测，标称值不等于带实际连杆负载的响应。

**资源冲突必须先处理：**

- A30/A31 是 SDK 默认 UART1 小核控制台引脚。小核版在控制台仍为 uart1 时明确拒绝启动。需要在实际板型中把控制台迁到一组可引出的独立 UART（包括驱动启用、pinmux、波特率、RT_CONSOLE_DEVICE_NAME），不能仅改字符串。当前没有你的板卡接线与固件版本，不能声称已完成这一步。
- PWM6 同时可驱动 B25 照明灯。测试时关闭占用 PWM6 的照明程序，并将 B25 保持为合适的 GPIO 状态；不要同时使用其 PWM6 功能。
- Linux/MaixPy、小核、其他程序不能同时控制 PWM4–7；小核使用时需在实际系统中解除 Linux 对这些 PWM/时钟的占用。RTT 内部占用表不会替你检查 Linux 所有权。
- 首次中位也可能使安装偏置的连杆移动。先卸开连杆或确认中位附近无机械干涉，再接载。台架固定飞盘，不进行发射。

## 路线 A：先用 MaixPy 检查舵机与拍视频

将 bench.py 和 config.json 复制到 MaixCAM2，同目录运行。使用 USB/SSH/MaixVision 终端，避免使用 A30/A31 的 UART1。MaixPy 包由板卡环境提供，不要用 PC pip 安装一个同名包冒充。

```bash
# PC 或板卡：只生成指令，不输出 PWM
python bench.py --out runs/dry01
# MaixCAM2：实际输出；每次使用一个新目录
python bench.py --out runs/servo01 --run
```

默认顺序：四路先中位，等待3秒；然后每个舵机分别执行 `1500,1522,1500,1478,1500,1555,1500,1445,1500 µs`，每步1.5秒，重复3轮。约165秒。其他舵机保持中位，便于识别线序、转向、卡滞、回中和重复性。

±22/±55 µs 约对应标称舵轴 ±2°/±5°，**不是实测舵面角度**。连杆传动比、死区和装配偏置会改变关系。config.json 可改每路中位及脉宽上下限；默认限幅1400–1600 µs。先完成小角度测试再扩大范围；不要直接运行500–2500 µs全行程扫描。

正常结束时最后一个动作是中位，随后停止PWM；Ctrl+C/异常直接尝试停止PWM，不额外强制回中。停止PWM不等于物理断电，也不能保证各种舵机立即卸力。程序退出会尝试恢复原有引脚复用。异常退出/断电无法保证这些清理代码执行。

保存：metadata.json（配置、电压、负载、时钟说明）、commands.csv（指令及API调用前后时间）、end.json。`measured_angle_deg` 故意留空；API成功不能证明舵机已转到位。Python调度时间不作为实时性能保证。

## 路线 B：E907 小核固件

官方 SDK 的 riscv/config/rtconfig.py 明确选择 E907，固件使用 RT-Thread。servo_bench.c 使用硬件 PWM，线程仅改变目标脉宽，不用软件循环产生脉冲。

原驱动 `pwm_init(..., uint8_t duty)` 只有整数百分比：333 Hz时1%约30 µs，太粗。pwm_us_extension.c.inc 使用官方寄存器定义补充微秒脉宽设置，333 Hz时周期计数为 floor(24000000/333)。该补丁会停止、重装、再启动PWM，可能截短当前脉冲；**它适合初步转动/静态角度测试，未验证为无毛刺实时控制驱动**。研究微小阶跃响应前须用示波器核对跳变，后续飞行控制需要经过验证的周期边界更新。

在安装完整官方 SDK、其依赖和交叉工具链的 Linux/WSL 环境中：

```bash
python3 install_sdk.py /absolute/path/to/maix_ax620e_sdk
```

安装器核对研究时的 drv_pwm.c，保存 `.c.servo_backup`，追加扩展并复制 applications/servo_bench.c；不会烧录、不改启动分区。SDK版本不同会拒绝自动修改，需核对差异。

SDK 的 applications/SConscript 会自动收录此 C 文件；drivers/SConscript 已包含 pwm/*.c。构建沿用你板卡对应的官方 SDK 配置和 riscv/Makefile，工具链默认 `/usr/local/riscv64-elf-gnu-amd64/bin/riscv64-unknown-elf-gcc`，可用 RTT_EXEC_PATH 指定。先依据官方 README 完成 AX630C/MaixCAM2 项目配置，再构建 riscv；不能把孤立的 servo_bench.c 当成可直接烧录的完整固件。固件装载/签名/升级必须匹配现有系统，此处没有编造通用烧录地址。

解决控制台与Linux资源冲突、构建并部署匹配固件后，在**小核 shell**运行：

```text
servo_bench start
servo_bench stop
```

默认脉宽、限幅和顺序与台架方案一致，但小核参数在C源码中，**不读取config.json**。运行期间可用stop停止，线程每10ms检查一次请求（实际受调度影响）；输出 `SERVO,...` 和 `SERVO_END,...`。SDK默认tick为100 Hz，时间戳粒度10ms；不能用它声称测到了亚毫秒响应。

PC记录已迁移控制台的输出：

```powershell
python -m pip install -r requirements.txt
python collect.py --port COM7 --baud 115200 --out runs/rtt01 --bec-v 6.0
```

COM7、115200、6.0均是命令示例，替换为实际端口、控制台波特率、万用表读数。采集器只读取不下发启动命令；可先通过同一串口终端启动后切换采集器（可能漏开头），更推荐用终端完整保存日志，再转换，避免两个程序抢占串口：

```bash
python collect.py --input terminal.log --out runs/rtt_import01
```

raw.log完整保留原始日志，commands.csv保存解析记录并提示序号丢失；metadata.json记录tick频率与结束标记。complete只表示收到结束记录，是否失败/人为停止看end_record与每行rc。主机接收时刻不是实际PWM边沿时刻。关闭采集器不会停止板卡，停止需在shell执行stop或断开舵机电源。

## 录像与实测角度

手机/相机固定，从舵轴方向正对转动平面，画面中保留固定转轴、零位参考线、舵臂/舵面标记点。测舵面时应看舵面自己的转轴和标记，不能把舵轴角直接当作舵面角。每路分别拍更容易得到垂直视角。优先原始120/240fps视频；慢动作导出视频的播放FPS可能不是拍摄FPS，保留原文件和拍摄设置。

初步检查可用步序、画面和终端提示人工对应；若要精确测延迟，必须在同一画面拍到可靠同步标记，或者增加同步LED/逻辑分析仪。当前工具没有硬件同步，因此不会自动计算响应延迟、上升时间或宣称实时精度。

PC安装requirements后：

```bash
python video_measure.py servo1.mp4 --servo 1 --seq 6 --frame 120 --out runs/servo01/measurements.csv
```

- seq填该视频画面人工对应的commands.csv序号；无法对应填0。seq6仅是例子，不是固定“正5度”的编号。
- 首先在中位画面点击转轴中心、零位方向参考点；再点击当前帧舵臂/舵面标记点。
- A/D前后1帧，J/L前后10帧。切换帧保留固定转轴和零位，只需重新点击实际标记点；相机/装置移动后按R重新标定。
- S保存当前测量，Q退出；换指令或舵机时重新运行并设置对应servo、seq，结果追加到同一CSV。
- CSV保存视频绝对路径、SHA256、帧号、解码器报告PTS、FPS、三个原始像素点和有符号角度。正值为画面内逆时针。保留原始坐标便于复核。

这是二维平面角度测量；斜拍、透视、镜头畸变、柔性变形都会造成误差。不能把frame/FPS和实际拍摄时间无条件等同。若机位改变，零位基准必须重新建立。

## 本轮实测要记录什么

每个舵机至少保留：对应舵面、供电实测、负载状态、正/负方向、是否动作、是否卡滞/异响、回中角、各平台的实测角和三轮重复差。把观察写入同一run目录的 observations.md。视频角度可用于拟合“脉宽→实际舵面角”的关系；真实动态延迟仍需要同步。

不要因为“能转”就自动判定全部正常。本程序提供刺激与证据记录，是否满足角度误差/重复性要求需结合你给出的验收阈值。后续将有效实测数据再回填飞盘模型，而不是把指令值当作校准数据。

## 已做的软件验证与官方资料

```bash
python -m unittest -v test_tools
python check_c_syntax.py
python bench.py --out runs/dry02
```

9项测试覆盖限幅、重复通道、非法值、日志字段与角度计算；空跑108个步骤，不生成任何实测角度。C检查使用PC GCC和显式替身头文件，只证明语法，不能替代E907 SDK编译/链接。

资料查阅日期：2026-09-18。

- [MaixCAM2 硬件说明](https://wiki.sipeed.com/hardware/zh/maixcam/maixcam2.html)
- [MaixPy PWM 官方文档](https://wiki.sipeed.com/maixpy/doc/zh/peripheral/pwm.html)
- [MaixCAM2 pinmap 实现](https://github.com/sipeed/MaixCDK/blob/main/components/peripheral/port/maixcam2/maix_pinmap.cpp)
- [MaixCAM2 官方系统 SDK](https://github.com/sipeed/maix_ax620e_sdk)
- [小核 PWM 驱动](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/drivers/pwm/drv_pwm.c)
- [小核 RT-Thread 配置](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/config/rtconfig.h)
- [小核编译配置](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/config/rtconfig.py)

research目录保留查阅时源码快照用于核对，相关源码版权归原作者；工程尚未固定完整SDK提交，安装器通过驱动内容一致性检查避免盲目适配新版本。
