# V1.1 单路示波器测试

当前测试口固定为用户 PCB 图片上丝印 **1**：上排网络 **PWM01**，SoC **A25**。
它对应程序逻辑通道 **3**、mask `0x4`，不是 `--bench-servo 1`。
本组程序只输出这一口，另外三个舵机通道保持关闭。

三项测试均为固定 **1500 µs 高电平脉宽**，从首条指令成功检查后持续 **60 秒**，然后自动停止 PWM。
固定脉宽适合捕获稳定波形；舵机已经在中位时不应期待持续往返转动。

| 用例 | 控制路径 | 频率 | 预期周期 | 高电平宽度 | 持续时间 |
|---|---|---:|---:|---:|---:|
| 1 | E907 小核，沿用本仓库 RAM 控制路径 | 333 Hz | 约 3.003 ms | 1.500 ms | 60 s |
| 2 | Linux 原生 PWM 驱动 | 333 Hz | 约 3.003 ms | 1.500 ms | 60 s |
| 3 | Linux 原生 PWM 驱动 | 50 Hz | 20.000 ms | 1.500 ms | 60 s |

用例 1 与 2 的目标波形相同，用于比较控制路径；用例 2 与 3 只改变频率。
输出时不会每 100 ms 重启 PWM：E907 重发同目标保活，Linux 在首次启用后只读回检查。
本组测试不扩大固件默认的 1400–1600 µs 限制，不刷机、不设置开机自启。

## 在板卡上运行

测试包部署目录为 `/root/servo_scope_20261113`。先经 Wi-Fi 登录：

```sh
ssh root@192.168.137.250
cd /root/servo_scope_20261113
python3 run_scope.py --list
python3 run_scope.py --check
```

`--list` 只列出用例，`--check` 只核对文件和板卡空闲状态；这两条都不输出 PWM。
随后按需要逐个运行，不要并行启动：

```sh
# 用例 1：E907，333 Hz，1500 µs，60 秒
python3 run_scope.py 1

# 用例 2：Linux，333 Hz，1500 µs，60 秒
python3 run_scope.py 2

# 用例 3：Linux，50 Hz，1500 µs，60 秒
python3 run_scope.py 3
```

屏幕在测试期间暂时熄灭。正常结束、Ctrl+C、SIGTERM 或 SIGHUP 会停止测试并尝试恢复背光、I2C0、引脚复用和 PWM 控制器编号。
每次日志保存在 `logs/case编号_随机标识/`，包含 `output.log`、`metadata.json`、`result.json` 和恢复前后的状态。
`result.json` 中的 `cleanup_verified` 与命令退出码描述软件检查；不代表示波器已经测到波形。
不使用 `kill -9` 终止 E907 加载器；如果加载器报告不能确认小核复位，按其提示断电重启。

## 示波器连接和观察

探头尖端接图中 **1** 列上排的 **PWM01**，地夹接板卡上明确标注的 **GND**。
中排 **VBAT** 是电源，不是信号参考地。该列下排是经过 Q3 的舵机回流，不能仅凭位置把它当成直连板卡 GND。

建议先用一个通道测 `PWM01 → 板卡 GND`；333 Hz 时用约 1 ms/div，50 Hz 时用约 5 ms/div，测量周期与高电平宽度。
预期信号为约 3.3 V 逻辑电平，实际电压以示波器读数为准。
如果有第二通道，可将其尖端接舵机下排回流，两通道地夹都接同一个板卡 GND，观察回流相对 GND 的电位。
不要用地夹把下排回流直接短接到 GND 来“测回流”，这样会旁路 Q3，改变待排查的回路。

记录每项用例的实际周期、脉宽、幅值及拍到的波形。有信号后，再核对信号线是否到达舵机插头及其参考地。
若 1 无波形而 2 有，优先查 E907 路径；若 1、2 都有且一致，控制路径更可能正常；若 50 Hz 与 333 Hz 表现不同，再判断刷新率适配。

## 从源码重新构建

开发机仓库中设置既有 RV32 与 AArch64 工具链环境变量后执行：

```sh
bash tools/build_servo_scope.sh
```

产物位于 `build_servo_scope/`，包括 `servo_scope_tests.tar.gz` 和校验清单。
若板端目录丢失，可重新上传解包，不需要重新刷系统：

```sh
scp build_servo_scope/servo_scope_tests.tar.gz root@192.168.137.250:/tmp/
ssh root@192.168.137.250
mkdir -p /root/servo_scope_20261113
tar -xzf /tmp/servo_scope_tests.tar.gz -C /root/servo_scope_20261113
cd /root/servo_scope_20261113
python3 run_scope.py --check
```

只有显式执行 `run_scope.py 1`、`2` 或 `3` 才会启动对应 PWM；构建、解包、列出和检查不会启动。
