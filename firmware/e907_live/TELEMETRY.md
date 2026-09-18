# E907 测试日志与 CSV 采集

## 新记录（本次修复后）

```text
BENCH_CMD,seq,mask,pulse_us,frequency_hz,submit_monotonic_ns,checked_monotonic_ns,e907_snapshot_tick,rc
BENCH_END,rc
```

第一行为字段说明；实际日志只输出数值。`BENCH_CMD` 每次目标更新（含约 100ms 保活）打印一次；四路同时命令 mask=15。`rc=0` 表示目标命令及共享页/硬件读回检查通过，非零表示失败，不是舵机反馈。

- `submit_monotonic_ns`：A53 在调用写入/应答检查逻辑之前的 CLOCK_MONOTONIC 时间。
- `checked_monotonic_ns`：上述逻辑返回后的时间。差值包含通信、轮询和读回开销，**不是舵机机械响应时间**。
- `e907_snapshot_tick`：共享页偏移 76 的 E907 timer64 低 32 位快照，24MHz，约 179 秒回绕。它属于小核状态快照，不是物理引脚边沿捕获。
- A53 单调时钟与 E907 tick 不同源；均无视频同步锚点。录制视频时需要额外同步事件才能精确对齐，不能仅从这些字段得出机械延迟。
- `BENCH_END` 在加载器退出清理后输出；持续保持模式在停止前没有结束记录。清理错误也会反映在退出码中。

## 导入与兼容

开发机构建 `servo_collect` 后可转换既有日志：

```sh
/tmp/servo_review_build/servo_collect \
  --input reports/e907_four_servos_20260918_193218.log \
  --out /tmp/servo_csv_four
```

输出目录必须不存在，生成 `raw.log`、`commands.csv`、`metadata.json`。支持：

| 输入 | CSV 行及时间处理 |
|---|---|
| 原 RTT `SERVO,...` | 保留原 seq、before_tick、after_tick；tick_hz 来自 SERVO_META |
| 历史 `BENCH servo=...` | 每条一行，序号为采集器生成的记录编号，真实时间留空 |
| 历史 `BENCH mask=...` | 按 mask 展开通道行；同一记录共享生成的编号，时间留空 |
| 新 `BENCH_CMD,...` | 按 mask 展开通道行，保留同一命令序号、目标、时间和 rc |

CSV 前十列保持旧格式，追加 `source,mask,submit_monotonic_ns,checked_monotonic_ns,e907_snapshot_tick`。离线导入不把导入时刻写成接收/动作时刻，`host_receive_ns` 留空；串口实时采集时该列只是采集主机接收时间。没有视觉测量时 `measured_angle_deg` 为空。

现代序号包含未输出为 BENCH_CMD 的 STOP 等命令，所以不按序号空缺认定丢包。历史日志不可凭空补回时间；现有波形图仍属于标称时序重建。

仅有 `PASS` 不标记 complete；识别 `BENCH_END,0`、历史 `EXIT_STATUS 0` 或兼容的 `SERVO_END` 结束记录。解析错误、非零命令结果、采集中断或末尾残缺行会使 complete=false。原始日志保留结束记录之后的恢复/检查文本。请结合 complete、exit_code、rejected_records 判断采集结果，不能只看 CSV 是否存在。

`--bec-v` 为兼容保留的选项名，只能填操作者在插头处实际测得的电压，不证明电路存在 BEC。未提供时 measured_voltage_v=null。V1 舵机正极接 VBATT，上游稳压情况未核实。

## 部署和证据范围

本次修复后的加载器已重新交叉编译，但未上传/运行；需重新上传 `build_e907_live/e907_live_probe` 后才产生新格式。不需要改变小核共享内存 ABI。旧日志保持原样，原有实机通过结论不自动扩展到新日志实现。

新增 collector_protocol 回归覆盖实际历史日志（9/36 行）、四路展开、时间字段、tick 回绕原值、旧 SERVO、失败/残缺输入及结束判定。最新本地测试为 5/5；历史报告中的 3/3、4/4 保留其当时含义。
