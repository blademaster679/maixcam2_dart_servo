# V1.1 底板：ICM-42688-P + BMI270

Linux/A53 的 C++17 双 IMU 驱动和有界采集工具。BMI270 使用固定版本 Bosch C Sensor API；无需 MaixPy 或 Python spidev。没有启动 E907 或输出舵机 PWM。

## 本板连接

| IMU | MOSI | MISO | SCK | CS | 运行时访问方式 |
|---|---|---|---|---|---|
| ICM-42688-P | A0 | A1 | A4 | A2 | `/dev/spidev1.0`，硬件 CS0 |
| BMI270 | B2 | B3 | A26 | A27 | `/dev/spidev2.1` 提供硬件 SPI 时钟/数据，GPIO A27 手动片选 |

BMI270 的板上网络标作 SPI2 CS0，但当前系统未提供 `spidev2.0`。本工具不刷设备树：临时将 A27 配为 GPIO，在每个完整 SPI message 前拉低、结束后拉高。AX 内核拒绝 `SPI_NO_CS`，因此将未接本板 IMU 的 B18/B19/B20/B21 设为 GPIO 输入，隔离替代引脚和硬件 CS1；**不是把硬件 CS1 当作 CS0，也不是软件模拟 SPI 时钟**。

仅适用于报告所核对的 V1.1 接线。旧 `config.json`、E907 舵机固件仍是 V1 的 B2/B3/PWM4/PWM5 映射，不得与本板 BMI270 同时使用。所有采集程序应独占这些总线和引脚；本工具拒绝已打开的 SPI/I2C1/I2C6、已绑定的 I2C 客户端、已占用 GPIO 及使能的 PWM4/5。

## 编译

在板端或具备目标交叉编译器的 Linux 主机运行：

```sh
bash tools/build_dual_imu.sh
```

脚本使用 `CC`、`CXX` 环境变量，默认 `gcc`、`g++`。生成 `build_dual_imu/dual_imu` 和二进制 `SHA256SUMS`。在 x86 Linux 编译得到的是主机程序，不能直接复制到 AArch64 板端执行；主机程序会拒绝硬件访问。

加入标定后，完整优化编译可能超过板端约 446 MB 的内存容量；推荐在 WSL 使用 MaixCDK
附带的 AArch64 工具链，设置 `CC` 为 `aarch64-none-linux-gnu-gcc` 的绝对路径、`CXX` 为
同目录 `aarch64-none-linux-gnu-g++` 的绝对路径，再运行上述脚本。脚本不会在构建主机
执行目标二进制。将生成文件复制至板端后再运行 `dual_imu --help` 检查可执行性。

也已集成进现有 CMake 工程：

```sh
cmake -S cpp -B /tmp/servo-build -DBUILD_VIDEO=OFF
cmake --build /tmp/servo-build -j2
ctest --test-dir /tmp/servo-build --output-on-failure
```

## 运行

输出目录必须是新目录，所有运行都需要显式指定 `--probe` 或 `--run`：

```sh
# 仅验证两颗身份寄存器，各连续读取 100 次
sudo ./build_dual_imu/dual_imu --probe --out runs/imu_probe_01

# 同时采集 60 秒，允许范围 1–600 秒
sudo ./build_dual_imu/dual_imu --run --seconds 60 --out runs/dual_imu_01
```

## 静止陀螺仪零偏标定与补偿

固定底板，避免振动。以下命令持续约 240 秒：预热 60 秒、独立标定 60 秒，然后冻结
两颗 IMU 各自的三轴均值，在后续 120 秒新数据上验证残余零偏。

```sh
sudo ./build_dual_imu/dual_imu --run --calibrate --stationary-confirmed \
  --warmup-seconds 60 --calibration-seconds 60 --seconds 120 --out runs/bias01

# 重启采集程序后复用同一份参数；保持静止以复测残余零偏。
sudo ./build_dual_imu/dual_imu --run --calibration runs/bias01/calibration.json \
  --stationary-confirmed --warmup-seconds 60 --seconds 60 --out runs/bias02
```

`calibration.json` 同时保存 ICM/BMI 的偏置、参考温度、板卡主机名、传感器配置及标定质量。
参数仅用于这块板、对应传感器和配置；加载时核对主机名、量程、ODR 和寄存器配置。
主机名不是芯片唯一序列号，更换板或 IMU 后应重新标定。ICM 另记录自检寄存器和 Bank 4
用户 offset 寄存器，拒绝自检激励状态；软件不改写用户 offset，也不写 NVM。

补偿公式为 `gyro_corrected = gyro_raw_dps - calibration_bias_dps`。采集中不更新偏置，
真实慢速旋转不会被自动扣掉。原始整数及 `gx/gy/gz_dps` 保持原义；新增
`gx/gy/gz_corrected_dps` 和 `gyro_bias_applied`。应用使用补偿角速度时读取新增列。
不带标定参数时补偿标志为 0，新增列等于原始换算值。

标定门限：至少 120 组/秒且无无效/饱和数据，加速度模长 0.9–1.1 g，逐轴加速度
标准差 ≤0.01 g，陀螺仪标准差 ≤0.2°/s、均值绝对值 ≤10°/s，完整 5 秒窗口的
均值跨度 ≤0.08°/s，标定温度跨度 ≤2°C。新标定至少预热 30 秒、采样 10 秒。
这些是工程验收门限，不能排除低于门限的缓慢运动，仍需人为确保静止。

`--stationary-confirmed` 对独立验证段执行同样的稳定性检查；启用补偿时另外要求两颗
IMU 的各轴残余均值绝对值均 ≤0.05°/s。动态采集应省略此静止声明。补偿期间若与标定
参考温度相差超过 3°C，会终止并提示重新标定；这不是温度补偿模型，也不保证 3°C 内
所有温度下的精度。均值扣除不会降低随机噪声，不能据此推断长期零偏稳定性或 Allan 指标。

`warmup/`、`calibration/` 保留各阶段原始 CSV，根目录 CSV 为独立验证/使用阶段。
`summary.json` 包含阶段统计、原始均值及 `gyro_correction` 补偿统计。独立校验与绘图：

```sh
python3 tools/analyze_dual_imu_bias.py runs/bias01 runs/bias02 --output bias_analysis.json --plot
```

该校验器重算偏置、补偿值、均值/标准差，检查标定与验证时间不重叠，并核对复测确实
复用同一份参数。绘图可选，需要 matplotlib 与 numpy。

固定设置：四线 SPI mode 0、8 bit、MSB first、1 MHz；两颗均为标称 200 Hz、±4g、±500°/s。

- ICM：验证 `WHO_AM_I=0x47`，配置内部时钟、量程和 UI 滤波，按数据就绪位读取温度及六轴寄存器。数据按大端解析。
- BMI：验证 `CHIP_ID=0x24`，调用 Bosch API 上传 8192 字节配置并检查 `INTERNAL_STATUS=1`，关闭高级节能后配置加速度计与陀螺仪。SPI 读正确处理 dummy 字节，数据按小端解析。寄存器原始陀螺仪值未施加 Bosch API 的软件跨轴修正。
- BMI 的 API read 回调保留 dummy 字节，由 Bosch API 丢弃；应用直接读取寄存器时则自行跳过。两条路径不能重复丢弃 dummy 字节。
- BMI 待机阶段保留至少 450 µs 的事务后等待，关闭高级节能后的正常采集等待至少 2 µs。

芯片中断未接至 CPU，因此使用数据就绪轮询。本程序验证正常接收，不保证无丢样、硬同步或每个 200 Hz 样本都被读出，也没有进行 FIFO 压力测试。两颗安装坐标系未对齐，数据不能直接逐轴相减后当成姿态误差。

## 输出与判定

输出包括：

- `icm42688.csv`、`bmi270.csv`：原始六轴、g/°/s 换算、温度、每次读取开始/结束的主机单调时钟、有效性与饱和标志。BMI 的 `sensor_time` 是自由运行计数器，不能当作样本序号或精确采样时刻；ICM 此列为 0。
- `summary.json`：芯片 ID、配置读回、接收速率、均值/标准差、变化次数、最大主机接收间隔、传输错误及恢复检查。

普通采集的 `passed=true` 要求身份检查成功、完成指定时长、每路接收数量不少于标称数量的 60%、没有无效/饱和样本、输出确实变化且清理无错误。带静止验证参数时还须满足上述静止和残余零偏门限。该判定不代表长期精度或完整姿态系统验收。所有 CSV 均保留原始偏置，补偿值单独输出。

## 资源与退出

运行期间临时解绑 I2C1/I2C6 控制器、保存引脚复用和 SPI 参数；退出时停止传感器、恢复 ICM 原配置、将 BMI 恢复到未初始化待机，恢复 GPIO、引脚、SPI 参数并重新绑定原 I2C 控制器，最后读回核对。

BMI 初始必须是未初始化待机；拒绝重置正在使用或已配置的传感器。清理直接向 BMI `CMD=0x7e` 写软复位命令 `0xb6`，因为官方 `bmi2_soft_reset()` 会额外重新加载配置，不能用它表示“回到未初始化待机”。不写传感器 NVM。

正常结束以及 SIGINT/SIGTERM/SIGHUP 执行清理；中断退出码为 130，并保留 `passed=false` 的结果。SIGKILL、断电无法执行清理。测试过程不修改系统启动文件，重启后不依赖本工具的临时引脚设置。

源文件：`linux_bus.*` 为板级资源和 SPI 传输；`sensors.*` 为两颗驱动；`sample.hpp` 为无硬件依赖的解析；`bias.hpp` 为静止检查和固定零偏补偿；`main.cpp` 为采集与验收。Bosch 来源、版本和许可证见 `../vendor/bmi270/UPSTREAM.md`。
