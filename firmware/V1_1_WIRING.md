# V1.1 舵机引脚适配

依据 `SCH_载板V1.1_2026-10-28.pdf` 第 1 页的 H2 接线和 Sipeed 核心板第 6 页复用表。
保留原来舵机编号与 H2 插座位置的对应关系，更新实际 PWM 与 GPIO：

| 舵机 | H2 信号脚 | V1.1 GPIO | PWM 通道 | E907 枚举 | pad 寄存器 |
|---|---:|---|---:|---|---|
| 1 | 12 | A31 | 7 | pwm_13 | 0x02304060 |
| 2 | 15 | A30 | 6 | pwm_12 | 0x02304054 |
| 3 | 9 | A25 | 1 | pwm_01 | 0x02304018 |
| 4 | 6 | A24 | 0 | pwm_00 | 0x0230400C |

四个 pad 的 `[18:16]` 均设为复用值 3。统一定义位于 [servo_board.h](servo_board.h)，
根目录及 MaixCDK 的 `config.json` 都使用 V1.1。配置校验同时检查编号、GPIO、PWM 和 H2 信号脚；
V1 配置会被拒绝。B2/B3 留给 BMI270 SPI2 MOSI/MISO，舵机代码不再配置这两个 pad 或 PWM4/5。

H2 正极 5/8/11/14 仍为 VBATT，回流 4/7/10/13 经 Q3 接地；信号改线没有改变供电网络。

## Linux 资源与恢复

PWM7/6 位于控制器 `6061000.pwm1`，PWM1/0 位于控制器 `6060000.pwm0`。
E907 加载器检查两个控制器的全部通道均无 requested/enabled 用户，然后临时解绑两个驱动。
它只保存、修改、恢复 PWM7/6/1/0 的通道寄存器及对应时钟位，不改写 PWM2/3/4/5。
GPIO/pad 与 I2C0 管理由 [servo_v11_pins.h](../tools/servo_v11_pins.h) 提供：

- A24/A25 同时是 I2C0 SCL/SDA。存在已绑定的 I2C0 客户端或打开的 `/dev/i2c-0` 时拒绝启动。
- UART1 控制台或打开的 `/dev/ttyS1` 占用 A30/A31 时拒绝启动；已占用/输出 GPIO 也拒绝。
- 临时解绑原本绑定的 `4850000.i2c` 控制器，保存 pinmux，并申请四个 GPIO 输入作为资源锁。
- 退出时先停止 PWM/复位 E907，再恢复 pad、时钟和驱动绑定；不改动启动文件或设备树。
- A53/MaixCDK 与 E907 加载器共用 `/run/lock/maixcam2-servo-v11.lock`，不能同时控制同组舵机。

MaixCDK 现有 pinmap 表缺少 A24/A25，本项目通过上述板级资源管理设置四个 pad，
继续使用 MaixCDK PWM API 输出脉宽。不要自行在旧 pinmap 表中把 A24/A25 当成 B2/B3。
本次未验证舵机与双 IMU 同时运行；独立引脚映射不等于系统并发时序已经验收。

RT-Thread 分支使用相同映射，但其 Linux 外设释放需要由系统集成负责。
UART1 控制台现阻止舵机 1/2；UART3 不再与舵机引脚冲突。
SDK 安装器同步复制 `servo_board.h` 至 applications 与 PWM 驱动目录，保留既有版本冲突检查。

## 固件及日志兼容性

E907 舵机服务升级为 ABI v2：magic `0x53525632`，版本字段 2，资源租约标记 `0x50574D32`。
加载器和固件需一起更新，旧版搭配会在发送 PWM 命令前被拒绝。`--run-probe` 的通用探针协议不变。
mask 的 bit0..3 始终表示舵机 1..4，现在对应 PWM7/6/1/0；共享页四组寄存器快照也按舵机编号排列。

新日志在命令前输出 `BENCH_META,carrier,V1.1` 或 `SERVO_META,carrier,V1.1`。
采集器据此将逻辑编号展开成正确 PWM；无该记录的历史日志沿用 V1 的 PWM4/5/6/7，
并在元数据中标注历史默认值。不要删除新版日志中的板型记录。

## 本次验证范围（2026-10-28）

主机 CMake 构建及 CTest 8/8 通过。测试覆盖配置与 H2 对应、旧配置拒绝、两组 PWM 脉宽计算、
真实 E907 服务的逻辑 mask、旧租约拒绝、计时回绕超时停机，以及未使用寄存器和 BMI pad 不被写入。
日志转换分别测试 V1 历史记录和 V1.1 新记录。
两份配置各完成 108 条 dry-run 命令，逐行核对 PWM7/6/1/0 映射；在临时 SDK 目录验证了首次安装、重复安装和两个位置的共享头文件复制。

RV32 固件和 AArch64 加载器交叉编译通过，舵机固件 `.bin` 为 1772 字节，仍小于共享页前的 4096 字节上限。
MaixCDK 硬件分支使用实际 SDK 头文件及 AArch64 编译器通过语法检查；本次未执行完整 MaixCDK 链接构建。
本次是代码适配及软件验证，未上传或运行新的舵机输出程序，未确认实际转动、脉宽波形或电源表现。

官方 PWM 使用说明：[Sipeed PWM](https://wiki.sipeed.com/maixpy/doc/en/peripheral/pwm.html)。
寄存器/时钟依据为仓库保留的官方 `research/riscv__drivers__pwm__drv_pwm.c`、
`research/riscv__drivers__pwm__drv_pwm_reg.h` 与 `research/e907_official/riscv__risc-v__mc20e_e907_soc.h`。
