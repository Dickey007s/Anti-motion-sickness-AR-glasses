# 基于 ESP32-S3 的防晕车 AR 眼镜

通过 BMI270 六轴传感器检测车辆运动状态，在单眼 TFT 显示屏上投射反向运动的补偿粒子流，使视觉感知与前庭感知达成一致，减轻晕车症状。

---

## 硬件细节

### 主控：ESP32-S3 Super Mini

| 参数 | 规格 |
|------|------|
| 架构 | Xtensa LX7 双核，最高 240MHz |
| 内存 | 512KB SRAM + 8MB Flash（实际可用 4MB） |
| 可用 GPIO | GP1 ~ GP13（本项目范围内） |
| USB | 内置 USB-Serial（CDC），波特率 115200 |
| 供电 | 3.3V 稳压，工作电流约 100~150mA |

**Strap Pin 警告**：GPIO0/3/45/46 为 Strap Pin，上电时决定启动模式，**禁止用作通用 IO**。

### IMU：Bosch BMI270

| 参数 | 加速度计 | 陀螺仪 |
|------|----------|--------|
| 量程 | ±2g/±4g/±8g/±16g | ±125°/s ~ ±2000°/s |
| 分辨率 | 16-bit | 16-bit |
| 接口 | I2C 100kHz（启动扫描）/ 400kHz | I2C |
| 地址 | 0x68（SDO 接地）或 0x69 | — |
| 功耗 | ~130μA | ~3.5mA |

**当前配置**：Normal Mode，ODR=100Hz，加速度计 ±4g，陀螺仪 100Hz。

**驱动库**：使用 `sparkfun/SparkFun BMI270 Arduino Library`（基于 Bosch 官方 Sensor API 封装，自动处理 8KB 配置固件加载）。

### 显示屏：1.47" IPS TFT（172×320）

| 参数 | 规格 |
|------|------|
| 驱动芯片 | ST7789V3 |
| 分辨率 | 172 × 320（物理显存 240×320，有效画面需偏移 34 列） |
| 接口 | SPI Mode 0 |
| 颜色 | RGB565（65K 色） |
| 背光 | 软件 PWM 控制（GPIO9） |
| 刷新率目标 | 100Hz |

**库选择**：使用 `adafruit/Adafruit ST7735 and ST7789 Library`。TFT_eSPI 在该硬件上无法点亮（原因未明），相关历史文件见 `main_tftespi_backup.cpp` 和 `include/TFT_Setup.h`。

**渲染方式**：使用 110KB 静态帧缓冲整帧渲染，再通过 `tft.writePixels()` 批量推送，消除闪烁并提升流畅度。

### 光学模组

- 45° 折射镜片（PMMA 或光学玻璃）
- TFT 水平放置，光线以 45° 入射角折射进入人眼
- 虚像距离约 1.5~2m，与车窗外景物同焦
- 单眼覆盖（左眼或右眼），透光率 ≥85%

---

## 引脚接线

```
ESP32-S3          BMI270          ST7789V3
─────────────────────────────────────────────
GPIO6  ────────── SDA
GPIO7  ────────── SCL
GPIO1  ────────── INT
GPIO10 ────────────────────────── CS
GPIO11 ────────────────────────── MOSI
GPIO12 ────────────────────────── SCK
GPIO13 ────────────────────────── DC
GPIO8  ────────────────────────── RST
GPIO9  ────────────────────────── BL（背光，PWM 控制）
3.3V   ────────── VCC ─────────── VCC
GND    ────────── GND ─────────── GND
```

> **BMI270 双 GND 说明**：模块上有两个 GND 引脚，内部已短接。实际接线时任选一个接 GND 即可，另一个悬空。
>
> **3.3V 分路**：ESP32-S3 Super Mini 只有一个 3.3V 引脚，需用面包板电源轨或一拖三杜邦线给 BMI270 和 ST7789V3 并联供电。总电流 < 100mA，远低于 LDO 极限。

---

## 快速开始

**环境要求**：VS Code + PlatformIO 插件

如果系统 PATH 中找不到 `pio`，可使用 PlatformIO 虚拟环境中的完整路径：

```bash
# 编译
~/.platformio/penv/bin/pio run

# 烧录（端口自动检测）
~/.platformio/penv/bin/pio run --target upload

# 串口监控（115200）
~/.platformio/penv/bin/pio device monitor --baud 115200
```

> **注意**：当前代码在 `setup()` 中等待串口连接，但有 3 秒超时；若脱离电脑独立供电，不会无限卡住。

**烧录失败排查**：
- `PermissionError(13)` → 串口被占用，关闭 `pio device monitor` 后再烧录
- `Could not open COMx` / 找不到端口 → 检查 USB 线是否支持数据，或手动在 `platformio.ini` 中设置 `upload_port`
- 烧录完成后断电重启没反应 → 检查开发板是否进入下载模式（BOOT 键是否被按住 / GPIO0 是否被拉低）

---

## BMI270 串口调试

上电后串口会输出以下信息：

```
Anti-Motion-Sickness AR Glasses [Optimized]
--- I2C Scan ---
  Found I2C device at 0x68
----------------
Trying BMI270 at 0x68... OK
Loaded calibration from flash
  Acc bias:  0.012 -0.008 1.023
  Gyro bias: 0.500 -1.200 0.300
Frame=10, State=CRUISE, Offset=   +0.0px, BMI=OK
...
```

如果 BMI270 未连接或 I2C 故障，程序会自动进入演示模式：

```
Trying BMI270 at 0x68... FAILED (code -2)
Primary I2C address failed, trying secondary...
Trying BMI270 at 0x69... FAILED (code -2)
WARNING: Running in DEMO mode without BMI270
Frame=10, State=ACCEL, Offset=   +5.2px, BMI=DEMO
```

| 字段 | 说明 | 静止平放时预期 |
|------|------|---------------|
| `ACC X/Y/Z` | 加速度，单位 **g** | X≈0, Y≈0, **Z≈1.0**（重力） |
| `GYR X/Y/Z` | 角速度，单位 **°/s** | 三轴都 ≈0（±2°/s 内正常） |
| `State` | 运动状态 | CRUISE / ACCEL / BRAKE |
| `Offset` | 视觉补偿偏移量（像素） | 静止时接近 0 |
| `BMI` | 传感器状态 | OK 或 DEMO |

**初始化失败**：若看到 `FAILED (code -2)`，表示 I2C 通信失败：
1. 检查 SDA/SCL 接线（GPIO6/7）
2. 确认 GND 共地
3. 串口查看 I2C Scan 是否发现 0x68 / 0x69

---

## 项目结构与重要文件

```
.
├── platformio.ini              # PlatformIO 配置：库依赖、编译参数、上传端口
│                               #   lib_deps: Adafruit GFX + ST7789 + SparkFun BMI270
│                               #   board: lolin_s3_mini
│
├── src/
│   └── main.cpp                # ⭐ 主程序入口。当前状态：
│                               #   - 1.47" ST7789 帧缓冲渲染（172×320，100Hz）
│                               #   - 粒子系统反向运动补偿视觉效果
│                               #   - BMI270 双地址 I2C 初始化 + I2C 扫描
│                               #   - 传感器异常时自动进入演示模式
│                               #   - 校准零偏保存到 NVS，断电不丢失
│
├── docs/
│   └── 项目设计报告.md          # ⭐ 系统架构、算法设计、开发计划、KPI、风险分析
│                               #   第 5 章为核心算法（互补滤波、补偿渲染）。
│
├── backups/
│   └── adafruit_st7789_working/# TFT 驱动验证通过的备份版本
│       ├── main.cpp            #   历史可用版本
│       ├── platformio.ini      #   历史配置
│       └── TFT_Setup.h.bak     #   TFT_eSPI 历史配置（已弃用）
│
├── lib/bmi270_sensor_api/      # [已弃用] Bosch 官方 C 驱动（旧版自定义封装）
│                               #   现改用 SparkFun BMI270 Arduino Library
│
├── main_tftespi_backup.cpp     # TFT_eSPI 库的测试代码（已弃用，保留参考）
├── include/TFT_Setup.h         # TFT_eSPI 库的历史配置文件（已弃用）
└── download_bmi270_api.sh      # 下载 Bosch 官方 API 的脚本（旧版）
```

---

## 协作者指引（Agent 工作流）

### 接手任务前必读

1. **先读本文档**（README.md）了解硬件和项目结构
2. **再读 `docs/项目设计报告.md`** 了解系统架构和算法设计
3. **最后读 `src/main.cpp`** 了解当前代码状态和预留的 TODO

### 修改代码前检查

- **引脚冲突**：任何新增 GPIO 使用前，先查本 README「引脚接线」表，确认不与现有设备冲突
- **Strap Pin**：GPIO0/3/45/46 绝对禁止用作通用 IO
- **SPI2 引脚**：GPIO6~17 为 SPI2 默认引脚，若用作 GPIO 可能在 SPI 通信时被覆写

### 代码规范

- 使用 PlatformIO 编译验证，无警告后再提交
- 串口调试输出保留，但 `loop()` 中的高频输出需控制频率（避免 100Hz 刷屏）
- 新增算法模块建议在独立 `.cpp/.h` 中实现，`main.cpp` 仅负责调用

---

## 开发路线图

### Phase 1：硬件验证（已完成）

- [x] TFT 驱动验证（Adafruit 库，172×320 显示正常）
- [x] BMI270 驱动集成（SparkFun 库，I2C 六轴数据读取正常）
- [x] BMI270 参数配置（100Hz/4g/Normal Mode）
- [x] 上电静止标定（200 次采样零偏）
- [x] 校准数据断电保存（NVS）
- [ ] 45° 折射镜片光路验证

### Phase 2：算法核心（已完成主要功能，持续优化）

- [x] 零偏补偿与低通滤波
- [x] 互补滤波姿态解算（pitch/roll）
- [x] 世界坐标系纵向加速度提取与运动状态检测
- [x] 反向运动补偿渲染（粒子系统 + 帧缓冲）
- [ ] 车内路测与参数调优（K、alpha、阈值）

### Phase 3：集成优化

- [ ] 整机结构精简与佩戴舒适度
- [ ] 功耗测试与续航优化
- [ ] 多工况路测

---

## 已知问题与避坑指南

| 问题 | 现象 | 原因 | 解决 |
|------|------|------|------|
| TFT_eSPI 黑屏 | 屏幕完全不亮 | 库与 ESP32-S3 Super Mini 兼容性未知 | 改用 Adafruit 库，见 `src/main.cpp` |
| 烧录端口拒绝访问 | `PermissionError(13)` | 串口监控占用了 COM 口 | 关闭 `pio device monitor` 再烧录 |
| 172×320 画面偏移 | 画面偏左或只显示一半 | ST7789 物理显存 240×320，有效画面需偏移 34 列 | `tft.init(172, 320)` 由 Adafruit 库自动处理 |
| 串口无输出 | 黑屏且无日志 | `Serial.begin()` 后 USB CDC 未就绪 | 已加 3 秒超时，不会无限等待 |
| BMI270 初始化失败 | 串口输出状态码 -2 | I2C 通信失败（接线/地址/共地） | 检查 GPIO6/7 接线，程序会自动尝试 0x69 |
| 只有一个 3.3V 引脚 | 无法同时给多个设备供电 | Super Mini 引脚少 | 用面包板/一拖三杜邦线并联，总电流 < 100mA |
| 断电后需要重新烧录 | 上电后程序不运行 | 误以为固件未保存 / 开发板进入下载模式 | 固件写入 Flash 后持久保存；检查 BOOT 键和 GPIO0 |

---

## 更新日志

### v1.2（2026-06-18）

- **新增**：帧缓冲 + 粒子系统反向运动补偿渲染
  - 使用 110KB 静态帧缓冲，消除闪烁
  - 粒子从屏幕中心生成并向外扩散，受纵向加速度影响产生方向偏置
- **新增**：BMI270 鲁棒初始化
  - I2C 总线扫描
  - 自动尝试 0x68 / 0x69 两个地址
  - 传感器故障时自动进入演示模式，屏幕仍然正常工作
- **新增**：校准数据断电保存
  - 使用 ESP32 NVS（Preferences）保存 acc/gyro 零偏
  - 下次上电直接加载，无需重新校准
- **变更**：背光改为软件 PWM 控制（GPIO9）
- **文档**：全面更新 README，反映当前实现状态

### v1.1（2026-05-07）

- **新增**：BMI270 六轴传感器驱动集成
  - 使用 `sparkfun/SparkFun BMI270 Arduino Library`（PlatformIO 自动下载）
  - I2C 接口：SDA=GPIO6，SCL=GPIO7，INT=GPIO1
  - 配置：Accel 100Hz/±4g，Gyro 100Hz，Normal Mode
  - 六轴原始数据串口输出（调试用）
- **变更**：弃用旧版 `lib/bmi270_sensor_api/` 自定义驱动，改为 SparkFun 官方封装
- **文档**：更新引脚接线表、新增 BMI270 调试说明、更新开发路线图

### v1.0（2026-05-06）

- 项目初始化
- TFT 驱动验证通过（Adafruit 库，172×320 纯白显示）
- 旧版 BMI270 自定义驱动框架（Bosch API + Arduino I2C 适配层）

---

*文档版本：v1.2*
*更新日期：2026-06-18*
