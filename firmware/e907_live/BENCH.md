# 独立四舵机台架测试（V1.1）

需要接示波器定位 PWM 时，使用新增的[三项单路、各 60 秒测试](SCOPE.md)。
用户 PCB 上丝印 `1` 是 PWM01/A25，对应程序逻辑通道 `3`。

当前映射采用 2026-11-13 版原理图，见 [V1.1 适配说明](../V1_1_WIRING.md)。V1.1 四路往返已由操作者确认，第二批四路也已完成识别和中位记录。请成对使用新版加载器与 ABI v2 固件。

本模式只运行固定动作，无视觉输入，不依赖其他工程。A53 加载和发送预设指令，E907 配置硬件 PWM。使用实验性 RAM 加载器，不刷机。源代码：`servo.c`、`start.S`、`servo.ld`、`../../tools/e907_live_probe.c`、`../../tools/e907_bench_sequence.h`；编译器设置见 [README](README.md#构建)。

## 两批舵机校准记录

第一批记录在 [servo_calibration.json](../servo_calibration.json)，第二批记录在
[servo_calibration_set02_2026-11-20.json](../servo_calibration_set02_2026-11-20.json)。
两批位置分别按照各自文件的 `view_reference` 所述照片或三维视图确认。

| 底板丝印 | 逻辑通道 | 第一批位置 | 第一批中位 / µs | 第二批位置 | 第二批中位 / µs |
|---|---:|---|---:|---|---:|
| 0 | 4 | 左上 | 1530 | 左上 | 1440 |
| 1 | 3 | 右下 | 1570 | 左下 | 1525 |
| 7 | 1 | 左下 | 1580 | 右下 | 1570 |
| 6 | 2 | 右上 | 1450 | 右上 | 1515 |

2026-11-22，第一批 0 口中位由 1580 µs 更新为 1530 µs，1 口由 1450 µs 更新为
1570 µs，6 口由 1520 µs 更新为 1450 µs。使用现有板端程序归位，每次选择一条执行：

```sh
./e907_live_probe --hold-servo 4 e907_servo.bin --pulse-us 1530
./e907_live_probe --hold-servo 3 e907_servo.bin --pulse-us 1570
./e907_live_probe --hold-servo 2 e907_servo.bin --pulse-us 1450
```

原 0 口角度实测保留在 JSON 中，以旧中位 1580 µs 为基准，已标记需要重新验证。
下述扫角、阶跃和预设角度保持仍采用旧编译参数，详见[第一批扫角说明](FOUR_SWEEP.md)。

新版加载器提供识别动作，按丝印 0→1→7→6 依次输出 1450–1550 µs，每路 10 秒，
间隔 2 秒，约 46 秒结束。1500 µs 是此识别动作的参考值。

```sh
./e907_live_probe --identify-ports e907_servo.bin
# 第二批 0 口固定在用户确认中位，Ctrl+C 结束
./e907_live_probe --hold-servo 4 e907_servo.bin --pulse-us 1440
```

普通持续模式允许 1400–1600 µs。校准 JSON 是独立记录，加载器需要通过 `--pulse-us`
显式接收脉宽。当前扫角、阶跃和 0 口角度保持命令采用第一批编译参数，详见
[第一批扫角说明](FOUR_SWEEP.md)。第二批尚未记录角度与脉宽的实测对应关系。

## V1.1 接线

断电接线，按连接器实际引脚编号核对方向：

| 测试编号 | SoC 引脚 | PWM | H2 信号 | H2 电源正极 | H2 回流 |
|---|---|---|---|---|---|
| 1 | A31 | 7 | 6 | 5 | 4 |
| 2 | A30 | 6 | 3 | 2 | 1 |
| 3 | A25 | 1 | 9 | 8 | 7 |
| 4 | A24 | 0 | 12 | 11 | 10 |

这些正极连接 VBATT，回流经 Q3 接地，Q3 栅极接 5V。接入前测量每组正极与回流之间的实际电压，并按实际舵机型号核对额定供电范围。核对舵机线序，电源需要承受舵机电流，不能从 GPIO 取电。首次固定舵机外壳、卸下连杆负载。首次 1500μs 定位可能从未知位置产生较大移动，后续才是小幅往返。

## 上传与运行

在开发机仓库根目录设置好编译器路径后构建，复制本次新生成的文件（板端旧加载器不支持新参数）。IP 是之前连接板卡的地址：

```sh
./tools/build_e907_live.sh
ssh root@10.11.105.1 'mkdir -p /tmp/e907_bench'
scp build_e907_live/e907_live_probe build_e907_live/e907_servo.bin root@10.11.105.1:/tmp/e907_bench/
ssh root@10.11.105.1
```

以下在板端 root shell 执行：

```sh
cd /tmp/e907_bench
chmod +x e907_live_probe
# 先观察 1 号，约 15 秒，结束后自动停止输出并恢复硬件
./e907_live_probe --bench-servo 1 ./e907_servo.bin
# 分别测试其他三路
./e907_live_probe --bench-servo 2 ./e907_servo.bin
./e907_live_probe --bench-servo 3 ./e907_servo.bin
./e907_live_probe --bench-servo 4 ./e907_servo.bin
# 四路依次测试，总计约 60 秒；同一时刻只有一路输出
./e907_live_probe --bench-servo all ./e907_servo.bin
```

默认执行一轮，添加 `--cycles N` 可指定 1–10000 轮；完成后自动退出。四个舵机可以同时接好。循环示例：

```sh
# 1→2→3→4 为一轮，共 3 轮，约 3 分钟
./e907_live_probe --bench-servo all ./e907_servo.bin --cycles 3
# 仅 2 号舵机执行 5 轮，约 75 秒
./e907_live_probe --bench-servo 2 ./e907_servo.bin --cycles 5
```

每轮打印 `BENCH cycle=当前轮/总轮数`。轮间不额外等待，每路结束先停 PWM；全部轮次完成才复位小核并恢复资源。任一轮发生错误或按 Ctrl+C，立即进入清理，不继续下一轮。`0` 不表示无限循环，非法次数在访问硬件前拒绝。更新此功能后需重新上传 `e907_live_probe`。

每路以 333Hz 输出：

```text
1500 → 1550 → 1500 → 1450 → 1500 → 1600 → 1500 → 1400 → 1500 μs
```

初始居中保持 3 秒，后续每步保持 1.5 秒。按 500–2500μs 对应 180° 的理想线性映射，往返约为中心 ±4.5°、±9°；实际角度以测量为准，不包含端点或全行程扫描。每约 100ms 重发目标维持 E907 的 1 秒超时期限，同脉宽不会反复重启 PWM。每条指令检查应答和 A53 独立寄存器读回，并确认其他三路未启用。

**Ctrl+C** 进入复位/停 PWM/恢复流程，不执行回中动作；正常结束先完成居中停留，再停止输出。停止 PWM 不代表断开舵机电源，也不保证释放力矩。寄存器不符、通信失败或检测到超时即终止测试。

若报 `REFUSED`，按提示检查 E907、PWM0/PWM1、I2C0 或 GPIO 占用。运行时需独占 PWM7/6/1/0 和对应引脚，关闭使用这些资源的其他应用。未接舵机的 `--run-servo-test` 是寄存器及故障注入验证，不用作机械动作测试。此模式不读取 `config.json`，动作参数固定在源码中。

本次加大幅度后需重新上传 `build_e907_live/e907_live_probe`；小核固件的 1400–1600μs 限制未改变。小幅录像不能作为增幅效果证据；增幅单路已取得实机通过日志，见当日总结。

## 验证范围

本次 RV32 固件、AArch64 加载器编译通过；本地 CTest 4/4 通过，新增测试覆盖通道选择、持续时间、保活间隔、错误及取消后停止。之前无舵机实测已验证 E907 四路寄存器控制和超时，日志见 README。

**单路小幅/增幅和四路依次模式已运行，指令与寄存器通过；小幅录像可见机械响应，真实引脚波形和角度精度仍未测量。** 终端 `PASS` 表示指令及寄存器检查通过，不能证明舵机已转动。观察每路按顺序往返、其他路没有动作，并检查电源是否稳定。

这是受控台架开发工具：不能使用 `kill -9` 强制杀死加载器，CMM 内存寿命与进程相关，崩溃或系统故障不保证清理。小核超时依赖其程序正常执行。PWM 更新仍用 stop/reload/start，尚未测量更新毛刺，详见 README 的实现边界。

## 固定位置持续输出

持续模式为 333Hz，默认 1600μs（理论相对中位约 +9°），可通过 `--pulse-us 1400..1600` 指定固定脉宽。只接受单个通道，不执行往返或自动结束。每约 100ms 保活并核对寄存器，E907 仍保留约 1 秒断流停机。停止信号会进入硬件恢复流程，不先回中。

```sh
# 板端前台运行；Ctrl+C 停止
cd /tmp/e907_bench
./e907_live_probe --hold-servo 2 ./e907_servo.bin
# 图中丝印 0 / PWM00 / A24 是逻辑通道 4；固定中位输出
./e907_live_probe --hold-servo 4 ./e907_servo.bin --pulse-us 1500
```

当前 0 口后台保持工具部署在 `/root/servo_pwm0_hold_20261113/`，`hold_pwm0.py --run`
持续输出 333 Hz / 1500μs，未设置自动结束，也不随 SSH 断开而停止。管理命令：

```sh
python3 /root/servo_pwm0_hold_20261113/hold_pwm0.py --status
python3 /root/servo_pwm0_hold_20261113/hold_pwm0.py --stop
```

`--stop` 核对进程身份后发送 SIGTERM，并等待 PWM、引脚、I2C0 与屏幕恢复；停止前屏幕暂时熄灭。
目录不会设置开机自启，板卡断电重启后不会自行恢复输出。

若由后台启动，使用 SIGTERM 停止，不能用 SIGKILL：

```sh
kill -TERM "$(cat /tmp/e907_bench/hold.pid)"
cat /tmp/e907_bench/hold.log
```

看到 `RESTORE: PWM0/PWM1 registers/clocks, V1.1 pads and driver bindings restored` 表示驱动恢复步骤已完成。后台运行会在 SSH 断开后继续保持输出；上述 PID/日志路径对应本次部署。重启后 `/tmp` 文件可能丢失，程序不会开机自启。此模式仍有 README 中实验性 CMM 加载器的限制。

## 四路同时往返

新增 `--bench-together all`，每一步把相同脉宽写入四路目标，并用一条共享内存命令提交 `mask=0xF`。E907 仍顺序更新 PWM 寄存器，不能保证边沿严格同步或四个舵机机械速度一致。安装方向不同也可能使视觉上的旋转方向不同。

```sh
# 开发机：先确认旧测试已退出，再上传本次编译的加载器
scp build_e907_live/e907_live_probe root@10.11.105.1:/tmp/e907_bench/
```

```sh
# 板端：四路同时测试一轮，约 15 秒
cd /tmp/e907_bench
chmod +x e907_live_probe
./e907_live_probe --bench-together all ./e907_servo.bin --cycles 1

# 四路同时测试三轮，约 45 秒
./e907_live_probe --bench-together all ./e907_servo.bin --cycles 3
```

本模式仅接受 `all`。不填 `--cycles` 默认为一轮，次数范围仍为 1–10000。每路均为 333Hz，动作是四路一起执行：

`1500 → 1550 → 1500 → 1450 → 1500 → 1600 → 1500 → 1400 → 1500 μs`

首次居中 3 秒，随后每步 1.5 秒，每约 100ms 保活。每条命令验证 active mask 和全部四路的寄存器；失败即终止并恢复。正常完成先结束回中停留，再停止全部输出；Ctrl+C 直接进入清理。日志中的 `mask=0xf` 表示四路同时启用。原 `--bench-servo all` 仍是依次测试，一轮约 60 秒；`--hold-servo` 仍只支持单路固定输出。

本次已通过交叉编译和本地模拟测试（四路分组更新、两轮持续时间、失败/取消后停止），已上传该模式，但尚未进行四路同时转动的实机验证。运行前确认供电可承担四个舵机同时动作的电流；逐路测试不能验证这一点。

最新日志格式及采集命令见 [TELEMETRY.md](TELEMETRY.md)，完整历史与图片见 [当日总结](../../reports/servo_test_summary_2026-09-18.md)。
