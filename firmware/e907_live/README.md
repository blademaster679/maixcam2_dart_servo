# E907 RAM 控制与实机结果

本目录是独立的裸机 E907 验证分支。它通过共享内存接收 A53 指令，由 E907 自己写 PWM 寄存器。没有使用 Linux PWM 代替小核输出，也没有把原 RT-Thread 固件宣称为已验证。

## 官方依据与实现边界

已下载并阅读官方 SDK 文档：

- [AX 快速启动使用说明](https://github.com/sipeed/maix_ax620e_sdk/blob/main/docs/board/08%20-%20AX%20%E5%BF%AB%E9%80%9F%E5%90%AF%E5%8A%A8%E4%BD%BF%E7%94%A8%E8%AF%B4%E6%98%8E.pdf)：文档第 13–18 页（PDF 第 14–19 页）的 RTT 工具链、构建、共享外设和 UART 说明。标准启动由引导阶段加载签名 RT-Thread 镜像。
- [BL1 riscv_boot_up](https://github.com/sipeed/maix_ax620e_sdk/blob/main/boot/bl1/driver/riscv/riscv.c) 和 [chip_reg.h](https://github.com/sipeed/maix_ax620e_sdk/blob/main/boot/bl1/common/include/chip_reg.h)：小核复位、启动地址、时钟和释放复位顺序。
- [小核 PWM 驱动](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/drivers/pwm/drv_pwm.c) 与 [寄存器定义](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/drivers/pwm/drv_pwm_reg.h)：PWM1 对应 PWM4–7、24 MHz 时钟、周期/高电平计数与使能方式。
- [SoC 定义](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/risc-v/mc20e_e907_soc.h) 与 [timer64 实现](https://github.com/sipeed/maix_ax620e_sdk/blob/main/riscv/drivers/timer64/drv_timer64.c)：读取现有 24 MHz timer64 实现目标超时，不改写系统定时器。
- 官方 MSP `ax_sys_api.h` 中 `AX_SYS_MemAlloc` / `AX_SYS_MemFree`：申请和释放独占、非缓存映射的 CMM 内存。本机参考头文件位于 MaixCDK 的 maixcam2_msp 组件，板上实际链接 `/opt/lib/libax_sys.so`。

**官方提供的是启动序列和底层接口；在已运行 Linux 的板上，以 CMM 内存临时加载裸机固件，是本工程新实现并实测的开发路径，不是官方提供的完整加载器。**

原系统未启用标准 RTT 启动支持，但实测小核处于复位、时钟关闭；官方启动序列足以启动此独立程序。这更新了早先“需要先改系统才能做任何 E907 实验”的判断。完整 RTT 集成仍需配套工具链、SDK、内存布局和启动配置。

## 结构

- `probe.S`：148 字节的 RV32 命令应答探针，仅访问所分配的共享页，返回输入值按位取反。
- `start.S`、`servo.c`：1536 字节程序镜像，使用实际 `pwm_us_extension.c.inc`；配置四路 PWM，并独立执行超时停机。
- `../../tools/e907_live_probe.c`：A53 Linux 加载器与有界测试驱动。申请 16 KiB CMM 内存，程序位于首个 4 KiB，共享页位于 +0x1000，栈顶 +0x4000。
- `../../tools/build_e907_live.sh`：编译脚本。生成 RV32 ELF/bin 与 AArch64 Linux 加载器。

启动条件严格限制为当前已验证板型/内存范围：两条 E907 reset 位均置位、E907 时钟关闭、旧启动地址为 0，分配的物理地址位于 `0x60000000–0x80000000`。不满足则拒绝执行。

PWM 测试先确认 Linux PWM1 无 requested/enabled 用户，再临时解绑 `6061000.pwm1`，保存 PWM、pad 和时钟状态。执行结束/错误/SIGINT/SIGTERM/SIGHUP 时先复位小核，再恢复外设、重新绑定 Linux 驱动并释放 CMM 内存。PWM0/PWM2 不解绑。

## 构建

需要可生成 RV32IMA 的 GNU 裸机编译器和 AArch64 Linux 编译器。本实验不使用专有 RT-Thread 预编译库，因此可使用 Ubuntu `gcc-riscv64-unknown-elf`，不依赖官方文档需要向技术支持获取的 T-Head RTT 工具链包。

```sh
export RISCV_CC=/path/to/riscv64-unknown-elf-gcc
export ARM_CC=/path/to/aarch64-none-linux-gnu-gcc
# 仅对解包安装的 Debian 工具链需要指定 target as/ld 目录：
# export RISCV_BINUTILS_DIR=/path/to/usr/lib/riscv64-unknown-elf/bin
./tools/build_e907_live.sh
```

产物保存在 `build_e907_live/`，包含 `SHA256SUMS`。本轮使用 RV GCC 10.2.0、AArch64 GCC 11.3.0，RV32 禁用链接松弛和 small-data 绝对寻址，采用 PC 相对寻址，可放到不同 CMM 分配地址。链接器限制程序和 BSS 小于 4 KiB。

## 板端使用

接舵机观察转动请使用新增的 `--bench-servo 1|2|3|4|all`，完整接线与命令见 [独立台架测试](BENCH.md)。该模式已通过本地编译和模拟测试，尚未进行带舵机实测。

将三个产物 `e907_live_probe`、`e907_probe.bin`、`e907_servo.bin` 放入板卡同一目录，并给予加载器执行权限。

```sh
# 仅小核应答，不操作 PWM
./e907_live_probe --run-probe ./e907_probe.bin
# 未接舵机时，执行四路 PWM 寄存器验证，自动结束和恢复
./e907_live_probe --run-servo-test ./e907_servo.bin
```

本次已通过验证主机密钥的 SSH 上传至 `10.11.105.1:/tmp/servo_e907_probe/`。临时目录重启后可能丢失。无需连接 UART，也不迁移 UART1 控制台；这份裸机程序根本不使用 UART。

两种命令均是有界测试。加载器不是长期运行服务，不支持 `servo_bench start` 这样的 RTT shell 命令。`config.json` 不会自动下发到该程序。

## 共享内存 ABI v1

以 CMM 物理基址 + `0x1000` 为共享页起点，以下偏移单位为字节，小端 uint32。

| 偏移 | 字段 | 写入方 |
|---|---|---|
| 0 | 就绪 magic：探针 `0x45393037`，PWM 服务 `0x53525631`；trap `0xBAD00907` | E907 |
| 4/8/12 | mhartid / misa / mhcr | E907 |
| 16/20 | command / 请求序号 | A53 |
| 24 | 已处理序号 | E907 |
| 28/32 | 探针输入 / 结果（控制命令结果为 int32） | A53 / E907 |
| 36/40/44 | mcause / mepc / mtval | E907 |
| 48 | 通道 mask；bit0..3 对应 PWM4..7 | A53 |
| 52/56/60/64 | 四路目标脉宽 μs | A53 |
| 68/72 | active mask / 状态：0停止、1输出、2超时、3错误 | E907 |
| 76 | 当前 timer64 低 32 位 | E907 |
| 80..124 | 每路 3 个值：CONTROL / LOADCOUNT / LOADCOUNT2 | E907 |
| 128/132 | 超时次数 / ABI 版本1 | E907 |
| 136 | 已完成 PWM1 独占的标记 `0x50574D31` | 加载器 |

command：1=探针式应答；2=更新 mask 中的目标；3=停止全部；4=读状态快照；5=停止并驻留，等待加载器复位。命令 2 仅接受 1400–1600μs、333Hz；未选通道保留原状态。只更新部分通道不等于停止其他通道。

先写参数、做内存屏障，最后提交新请求序号。等待 ack 等于请求序号后再读取结果。只允许一个发送者、一次一个未完成命令。E907 在最后一条有效目标更新后约 1 秒未收到新目标时关闭全部 PWM；读状态或 ping 不会续期。

## 2026-09-18 实机证据

板卡：MaixCAM2 / `maixcam2-75c4`，镜像 `maixcam2-2026-05-29-maixpy-v4.12.5`，未连接舵机。

- 探针：100/100 次 E907 应答成功；读取 `misa=0x40909105`、`mhcr=0`，无 trap。
- 四路：1500、1522、1478、1555、1445、1500μs，全部使能位与周期计数符合期望。
- 独立目标：四路分别 1445/1478/1522/1555μs，A53 直接读硬件寄存器与 E907 返回值一致。
- 拒绝 mask=16、1399μs、1601μs；显式停止后四路使能均为0；单独选择 PWM6 时只有对应通道启用。
- 不再发送目标后，小核自动停止；A53 观测等待 `999.937 ms`，超时计数1、active mask=0。
- PWM 已启用时发送 SIGTERM，加载器执行恢复，CMM 不残留，Linux PWM1 重新绑定；PWM0 的原有输出仍在。

原始日志：

- [启动/应答](../../reports/e907_live_probe_2026-09-18.log)
- [四路控制与超时](../../reports/e907_live_servo_final_2026-09-18.log)
- [信号中断恢复](../../reports/e907_signal_cleanup_final_2026-09-18.log)

## 尚未验证的部分

尚未用示波器测量四个 H2 引脚的电平、真实周期、脉宽和更新毛刺；未接舵机，因此没有机械动作或负载结论。所有“通过”仅指本轮实际测试的层级。

程序仍用 stop/reload/start 更新脉宽，四路顺序更新，不保证同步边沿。此 RAM 加载方式用于受控开发验证，不能直接作为无人值守/飞行控制固件。SIGKILL、进程崩溃或系统故障不保证清理：小核与 CMM 内存寿命耦合，不应强制终止加载器；生产部署应采用预留内存和正式启动/监督机制。目标超时依赖小核循环继续执行，也不能替代独立硬件故障保护。
