# 基于 ESP32-S3 的防晕车 AR 眼镜

通过 BMI270 六轴传感器检测车辆运动状态，在单眼 TFT 显示屏上投射反向运动的补偿点阵，使视觉感知与前庭感知达成一致，减轻晕车症状。

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
| 接口 | I2C 400kHz | I2C 400kHz |
| 地址 | 0x68（SDO 接地） | — |
| 功耗 | ~130μA | ~3.5mA |

**初版配置**：Normal Mode，ODR=100Hz，加速度计 ±4g，陀螺仪 ±500°/s。

### 显示屏：1.47" IPS TFT（172×320）

| 参数 | 规格 |
|------|------|
| 驱动芯片 | ST7789V3 |
| 分辨率 | 172 × 320（物理显存 240×320，有效画面需偏移 34 列） |
| 接口 | SPI Mode 0 |
| 颜色 | RGB565（65K 色） |
| 背光 | Active Low（低电平亮），建议直接接 3.3V |
| 刷新率目标 | 30Hz |

**库选择**：使用 `adafruit/Adafruit ST7735 and ST7789 Library`。TFT_eSPI 在该硬件上无法点亮（原因未明），相关历史文件见 `main_tftespi_backup.cpp` 和 `include/TFT_Setup.h`。

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
GPIO10 ────────────────────────── CS
GPIO11 ────────────────────────── MOSI
GPIO12 ────────────────────────── SCK
GPIO13 ────────────────────────── DC
GPIO8  ────────────────────────── RST
3.3V   ────────── VCC ─────────── VCC ─────── BL（背光直接接 3.3V）
GND    ────────── GND ─────────── GND
```

> **GPIO9 禁用说明**：ESP32-S3 的 GPIO9 为 SPI2（FSPI）的 FSPIHD 引脚。在 SPI 通信时该引脚可能被控制器抢占，导致程序崩溃或背光异常。背光建议直接硬件常亮，不占 GPIO。

---

## 快速开始

**环境要求**：VS Code + PlatformIO 插件

```bash
# 编译
pio run

# 烧录（端口在 platformio.ini 中配置为 COM13，可按实际情况修改）
pio run --target upload

# 串口监控（115200，程序内有 while(!Serial) 等待连接）
pio device monitor --baud 115200
```

**烧录失败排查**：
- `PermissionError(13)` → 串口被占用，关闭 `pio device monitor` 后再烧录
- `Could not open COMx` → 检查设备管理器中的 COM 口号，更新 `platformio.ini` 中的 `upload_port`

---

## 项目结构与重要文件

```
.
├── platformio.ini              # PlatformIO 配置：库依赖、编译参数、上传端口
│                               #   lib_deps: Adafruit GFX + ST7789 库
│                               #   upload_port: COM13（按实际修改）
│
├── src/
│   ├── main.cpp                # ⭐ 主程序入口。当前为纯白显示验证状态。
│   │                           #   Phase 2 预留区：BMI270 初始化、姿态解算、
│   │                           #   运动检测、点阵绘制 TODO 已标注。
│   ├── bmi270_driver.cpp/h     # BMI270 C++ 封装类。提供 begin()/calibrate()/update()
│   │                           #   和 getAccel()/getGyro() 接口。
│   └── bmi270_arduino_interface.cpp/h
│                               # Bosch 官方 C 库的 Arduino I2C 适配层。
│                               #   实现 bmi2_i2c_read/write/delay_us。
│
├── lib/bmi270_sensor_api/      # Bosch BMI270 官方 C 驱动（不可修改）
│   ├── bmi2.c/h, bmi270.c/h, bmi2_defs.h
│
├── docs/
│   └── 项目设计报告.md          # ⭐ 系统架构、算法设计、开发计划、KPI、风险分析
│                               #   第 5 章为核心算法（互补滤波、点阵补偿）。
│
├── backups/
│   └── adafruit_st7789_working/# TFT 驱动验证通过的备份版本
│       ├── main.cpp            #   历史可用版本
│       ├── platformio.ini      #   历史配置
│       └── TFT_Setup.h.bak     #   TFT_eSPI 历史配置（已弃用）
│
├── main_tftespi_backup.cpp     # TFT_eSPI 库的测试代码（已弃用，保留参考）
├── include/TFT_Setup.h         # TFT_eSPI 库的历史配置文件（已弃用）
└── download_bmi270_api.sh      # 下载 Bosch 官方 API 的脚本
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

### Phase 1：硬件验证（进行中）

- [x] TFT 驱动验证（Adafruit 库，172×320 纯白显示正常）
- [x] BMI270 驱动框架（Bosch 官方 API + Arduino I2C 适配层）
- [ ] **BMI270 数据读取与标定**（`bmi270_driver.begin()` 验证 + 2秒静止标定）
- [ ] 45° 折射镜片光路验证

### Phase 2：算法核心

- [ ] 互补滤波姿态解算
- [ ] 纵向加速度提取与运动状态检测
- [ ] 点阵偏移生成算法与 TFT 实时显示
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
| GPIO9 背光冲突 | 程序启动后背光熄灭/重启 | GPIO9 为 SPI2 FSPIHD，SPI 通信时状态被覆写 | 背光直接接 3.3V，不软件控制 |
| 烧录端口拒绝访问 | `PermissionError(13)` | 串口监控占用了 COM 口 | 关闭 `pio device monitor` 再烧录 |
| 172×320 画面偏移 | 画面偏左或只显示一半 | ST7789 物理显存 240×320，有效画面需偏移 34 列 | `tft.init(172, 320)` 由 Adafruit 库自动处理 |
| 串口无输出 | 黑屏且无日志 | `Serial.begin()` 后 USB CDC 未就绪 | 代码已加 `while(!Serial)` 等待 |

---

*文档版本：v1.0*
*更新日期：2026-05-06*
