# ST7789 自定义动态点阵显示效果 — 修改指引

> 本文档基于当前工程 `D:\Desktop\嵌入式项目\Anti-motion sickness AR glasses` 的 `src/main.cpp` 编写。  
> 目标：在不破坏现有驱动的前提下，扩展出自定义的动态显示效果。

---

## 一、工程现状速览

| 项目 | 当前状态 |
|---|---|
| 驱动方式 | 裸 SPI 手写驱动（`class ST7789`），无 LVGL / 无 TFT_eSPI |
| 屏幕 | 1.47" ST7789V3，172 × 320，IPS |
| 像素格式 | RGB565，16 bit/像素 |
| SPI 频率 | 10 MHz（可提升到 40 MHz） |
| 当前动画 | `loop()` 中循环 `fillScreen()` 切换 8 种纯色 |

**现有 API（`class ST7789` 已提供）**：
- `fillScreen(uint16_t color)` — 全屏填充单色
- `fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)` — 画矩形
- `setRotation(uint8_t r)` — 设置旋转方向（0/1/2/3）
- `width()` / `height()` — 获取当前方向下的宽高

**颜色常量（RGB565）**：
```cpp
BLACK   = 0x0000   WHITE   = 0xFFFF
RED     = 0xF800   GREEN   = 0x07E0
BLUE    = 0x001F   YELLOW  = 0xFFE0
CYAN    = 0x07FF   MAGENTA = 0xF81F
```

---

## 二、自定义效果应该在哪里改代码

### 2.1 新增绘图函数 → `class ST7789` 内

**修改位置**：`src/main.cpp` 中 `class ST7789` 的 `public:` 区域（约在行 5-14 附近或行 68 之后）。

在 `fillRect()` 下方、其他绘图函数上方添加新的 `public` 方法。

**推荐位置**：
```cpp
    void fillScreen(uint16_t color) { ... }
    void fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) { ... }

    // ← 在这里插入你的新绘图函数

    int16_t width()  const { return _width; }
```

**为什么放这里**：
- 新函数需要访问 `private` 的 `sendCommand()`、`setAddressWindow()`、`_width`、`_height` 等成员。
- 保持类的封装性，外部只调用 `public` API。

### 2.2 帧动画逻辑 → `loop()` 函数内

**修改位置**：`src/main.cpp` 底部 `void loop()`（当前行 213-218）。

当前代码：
```cpp
void loop() {
    static int idx = 0;
    tft.fillScreen(colors[idx]);
    idx = (idx + 1) % colorCount;
    delay(1000);
}
```

把 `delay(1000)` 换成你自己的帧更新逻辑。

**注意**：Arduino 的 `loop()` 会被无限循环调用，天然就是主循环。不需要额外的 `while(true)`。

### 2.3 初始化配置 → `setup()` 内

**修改位置**：`void setup()`（行 181-211）。

如果效果需要预加载数据（如字体表、调色板、粒子初始位置），在这里初始化。

---

## 三、必须了解的底层机制

### 3.1 像素是如何写到屏幕上的

现有驱动的写像素流程（`fillRect` 内部）：
1. `setAddressWindow(x1, y1, x2, y2)` — 告诉屏幕"我要更新这个矩形区域"
2. `sendCommand(0x2C)` — 开始写入显存
3. 连续 SPI 发送像素数据（每个像素 2 字节，高字节在前）

**关键结论**：
- 只要指定了地址窗口并发了 `0x2C`，后面可以源源不断地写像素，屏幕会自动按行递增地址。
- 写满窗口后自动停止，不需要手动重置地址。

### 3.2 RGB565 颜色格式

每个像素 16 bit：
- R: bit[15..11] (5 bit)
- G: bit[10..5]  (6 bit)
- B: bit[4..0]   (5 bit)

**自定义颜色的方法**：
```cpp
// 方式1：直接拼 RGB565
uint16_t myColor = (r << 11) | (g << 5) | b;   // r=0..31, g=0..63, b=0..31

// 方式2：宏（可加在 class 上方或全局）
#define RGB565(r, g, b) (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))
uint16_t orange = RGB565(255, 165, 0);
```

### 3.3 坐标系统

- 原点 `(0, 0)` 在屏幕左上角
- `x` 向右递增，范围 `0` ~ `width()-1`
- `y` 向下递增，范围 `0` ~ `height()-1`
- `_colStart` (34) 和 `_rowStart` (0) 已在 `setAddressWindow` 中自动叠加，**你不需要手动加 offset**

---

## 四、推荐的新增函数模板

以下函数**不修改现有代码**，只在 `class ST7789` 的 `public:` 区域追加。

### 模板 A：直接画单个像素

```cpp
void drawPixel(int16_t x, int16_t y, uint16_t color) {
    if (x < 0 || x >= _width || y < 0 || y >= _height) return;
    setAddressWindow(x, y, x, y);
    uint8_t pixel[2] = {uint8_t(color >> 8), uint8_t(color & 0xFF)};
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    SPI.writeBytes(pixel, 2);
    digitalWrite(_cs, HIGH);
}
```

> ⚠️ **性能警告**：逐像素调用 `setAddressWindow` + CS 切换非常慢，只适合少量点。大量像素请用模板 B/C。

### 模板 B：批量画点（数组传入）

```cpp
void drawPixels(int16_t* xs, int16_t* ys, uint16_t* colors, int count) {
    for (int i = 0; i < count; i++) {
        drawPixel(xs[i], ys[i], colors[i]);
    }
}
```

### 模板 C：向指定窗口连续写原始像素流（最高效）

```cpp
void writePixels(const uint8_t* data, uint32_t len) {
    digitalWrite(_dc, HIGH);
    digitalWrite(_cs, LOW);
    SPI.writeBytes(const_cast<uint8_t*>(data), len);
    digitalWrite(_cs, HIGH);
}
```

用法示例（配合 `setAddressWindow`）：
```cpp
tft.setAddressWindow(0, 0, tft.width()-1, tft.height()-1);  // 全屏窗口
tft.sendCommand(0x2C);                                       // 开始写显存
tft.writePixels(frameBuffer, tft.width() * tft.height() * 2); // 发送整帧
```

---

## 五、四种典型动态效果示例（伪代码 + 实现思路）

### 示例 1：全屏呼吸灯（颜色渐变）

**效果**：全屏颜色在红↔黑之间平滑呼吸。

**实现位置**：`loop()`

**思路**：
- 维护一个 `uint8_t brightness = 0; int8_t dir = 1;`
- 每帧 `brightness += dir`，到 31 时反向。
- 用 `(brightness << 11)` 生成红色（只有 R 通道渐变）。

```cpp
void loop() {
    static uint8_t b = 0;
    static int8_t dir = 1;
    b += dir;
    if (b == 31) dir = -1;
    if (b == 0)  dir = 1;
    uint16_t color = (b << 11);  // R 从 0→31，G/B 为 0
    tft.fillScreen(color);
    delay(30);  // 约 33fps
}
```

---

### 示例 2：随机雨滴点阵

**效果**：屏幕上方随机位置出现亮点，以不同速度向下坠落，到底部消失。

**实现位置**：
- 数据结构定义在 `setup()` 上方（全局或 static）
- 更新逻辑在 `loop()`
- 辅助函数 `drawRain()` 加在 `class ST7789` 的 `public:`

**数据结构**：
```cpp
#define DROP_COUNT 30
struct Drop { int16_t x, y; uint8_t speed; uint16_t color; bool active; };
Drop drops[DROP_COUNT];
```

**思路**：
1. `setup()` 中初始化 `drops[]`（全部 `active = false`）
2. `loop()` 每帧：
   - 找不活跃的 drop，随机激活在屏幕顶部
   - 每个活跃 drop：`y += speed`
   - 如果 `y >= height()`，标记为 inactive
   - 先 `fillScreen(BLACK)` 清屏
   - 遍历所有活跃 drop，用 `drawPixel()` 画点
3. `delay(20)` 控制帧率约 50fps

> 优化：不清全屏，而是只擦除旧位置（画黑色），再画新位置，避免全屏闪烁。

---

### 示例 3：水平滚动图案（位图/字符）

**效果**：一个 16×16 的小图案从右向左滚动穿过屏幕。

**实现位置**：`loop()` + 全局图案数组

**图案数据（点阵字模思路）**：
```cpp
const uint16_t sprite[16] = {
    0b0000000000000000,
    0b0000011111100000,
    // ... 每行 16 bit，1=点亮，0=黑色
};
```

**思路**：
1. 维护 `int16_t offsetX = tft.width();`
2. 每帧 `offsetX--`，到 `-16` 时重置为 `tft.width()`
3. 清屏（或用局部刷新）
4. 遍历 sprite 的每个像素，如果在屏幕内则 `drawPixel(x + offsetX, y, color)`

> 更高效的做法：把 sprite 展开成 RGB565 数组，用 `setAddressWindow` + `writePixels` 一次性写入 16×16 的矩形区域。

---

### 示例 4：粒子爆炸效果

**效果**：屏幕中心发射多个彩色粒子，向外扩散，带淡出。

**实现位置**：`loop()` + `class ST7789` 新增函数

**数据结构**：
```cpp
#define PARTICLE_COUNT 50
struct Particle {
    float x, y;      // 浮点坐标，支持亚像素移动
    float vx, vy;    // 速度向量
    uint16_t color;
    uint8_t life;    // 剩余寿命 0..255
};
Particle particles[PARTICLE_COUNT];
```

**思路**：
1. 触发时（如 `setup()` 或定时器）初始化所有粒子从中心 `(width/2, height/2)` 以随机角度发射
2. 每帧：
   - `x += vx; y += vy;`
   - `life--`
   - 如果 `life == 0`，重新初始化该粒子（循环爆炸）
   - 根据 `life` 值对颜色做暗化（或直接保持原色）
3. 先 `fillScreen(BLACK)` 或局部擦除，再画所有存活粒子

---

## 六、性能优化建议

| 瓶颈 | 当前状态 | 优化方向 |
|---|---|---|
| SPI 频率 | 10 MHz | 在 `tft.begin()` 中改 `tft.begin(40000000)` 提升到 40 MHz |
| 逐像素画点 | `drawPixel` 每次切换 CS | 改为 `setAddressWindow` + 批量 `writePixels` |
| 全屏刷新 | `fillScreen` 逐字节 SPI | 已是最简实现，提升频率即可加速 |
| 浮点运算 | `Particle` 用 `float` | ESP32-S3 有 FPU，少量浮点无压力；大量粒子建议改定点数 |
| 帧缓冲 | 无（直接 SPI 写） | 172×320×2 = 110KB，SRAM 足够，可以建 `uint16_t frame[172][320]` 做双缓冲 |

### 关于帧缓冲

如果你的效果需要"先计算整帧再一次性刷新"（如 3D 投影、复杂混色），可以在全局定义：

```cpp
static uint16_t frameBuffer[172 * 320];  // 约 110KB，ESP32-S3 SRAM 足够
```

然后在 `loop()` 中：
1. 用 `memset` 或循环清屏（`BLACK` = `0x0000`）
2. 在 `frameBuffer[y * 172 + x]` 上直接写像素
3. 最后一次性刷到屏幕：
   ```cpp
   tft.setAddressWindow(0, 0, 171, 319);
   tft.sendCommand(0x2C);
   tft.writePixels((uint8_t*)frameBuffer, 172 * 320 * 2);
   ```

> 注意：`frameBuffer` 较大，放在全局（静态存储区），不要放在栈上（函数内局部变量会爆栈）。

---

## 七、常见问题速查

### Q1：画出来的颜色还是不对？
- 检查 MADCTL 是否已按修复指南改为 RGB（bit3=0）。
- 确认你的 RGB565 值拼写正确：`(r << 11) | (g << 5) | b`。

### Q2：画点有拖影/残影？
- 每帧开始前需要清屏或擦除旧位置。`fillScreen(BLACK)` 是最简单的做法。
- 如果拖影在屏幕边缘，检查 `_colStart` 和 `_rowStart` 是否被错误修改。

### Q3：动画卡顿？
- 提升 SPI 频率到 40 MHz。
- 减少 `drawPixel` 调用次数，改用批量写入。
- 降低 `delay()` 或改为 `millis()` 定时器做非阻塞帧率控制。

### Q4：如何旋转整个效果？
- 调用 `tft.setRotation(n)` 后，`width()` 和 `height()` 会返回对应方向的值。
- 你的绘图坐标仍然以 `(0,0)` 为左上角，驱动会自动处理坐标映射（通过 MADCTL 的 MY/MX/MV 位）。
- 注意：如果效果依赖硬编码分辨率（如 `172` 或 `320`），应改为 `tft.width()` / `tft.height()`。

---

## 八、推荐的最小修改清单（回顾）

假设你要实现"示例 2：随机雨滴"，最少需要改这些地方：

1. **`src/main.cpp` 顶部**（`#include` 下方）：添加数据结构和全局变量
2. **`class ST7789` 的 `public:` 区域**：添加 `drawPixel()` 函数
3. **`setup()` 内**：初始化雨滴数组
4. **`loop()` 内**：替换现有 `fillScreen` 循环，写入雨滴更新逻辑

不需要修改 `initSequence()`、`sendCommand()`、`setAddressWindow()` 等底层驱动。

---

## 九、BMI270 传感器驱动扩展指引

> 本节说明：BMI270 驱动代码已集成到工程中，当前版本提供**初步的初始化与原始数据读取能力**，作为后续算法开发的基础。你不需要从零写 I2C 通信和寄存器配置，只需在现有框架上叠加算法逻辑。

### 9.1 已提供的驱动文件

| 文件 | 作用 |
|---|---|
| `lib/bmi270_sensor_api/bmi2.c/.h`, `bmi2_defs.h`, `bmi270.c/.h` | Bosch 官方 C 库（初始化序列、config file 加载、寄存器封装） |
| `src/bmi270_arduino_interface.cpp/.h` | Arduino `Wire.h` ↔ Bosch API 的桥接层（I2C 读/写/延时） |
| `src/bmi270_driver.cpp/.h` | 高层 C++ 封装类：`begin()` / `calibrate()` / `update()` / `getAccel()` / `getGyro()` |

### 9.2 当前 main.cpp 中的 BMI270 基础框架

`setup()` 中已完成：
1. `bmi.begin(8, 9)` — I2C 初始化、BMI270 软复位、上传 config、配置 accel 100Hz/±4g + gyro 100Hz/±500°/s、启用传感器
2. `bmi.calibrate(200, 10)` — 2 秒静止采样，计算加速度计和陀螺仪零偏

`loop()` 中已完成：
- `bmi.update()` — 读取一帧 12 字节原始数据，自动扣除零偏，转换为物理单位（m/s² 和 °/s）

### 9.3 下一步算法扩展应该改哪里

基于当前代码，实现完整的防晕车补偿算法，**只需修改 `loop()` 函数内的以下区域**（其余文件保持不动）：

**1. 姿态解算（互补滤波）**
- 位置：`loop()` 中 `bmi.update()` 之后
- 可用数据：`bmi.getAccelX/Y/Z()`（m/s²）、`bmi.getGyroX/Y/Z()`（°/s）
- 建议：使用 `millis()` 差值计算真实 `dt`，不要硬编码 0.01s

**2. 纵向加速度提取**
- 位置：互补滤波之后
- 公式：`a_x_world = acc_x * cos(pitch) + acc_z * sin(pitch)`
- 输入：传感器坐标系加速度；输出：车辆前后方向加速度

**3. 运动状态检测**
- 位置：纵向加速度提取之后
- 阈值建议：±0.1g（约 ±0.98 m/s²）
- 状态：加速 / 减速 / 匀速

**4. 视觉补偿点阵偏移生成**
- 位置：运动状态检测之后
- 公式：`target_offset = -K * a_x_world(g)`，再经一阶平滑和限幅
- 输出：一个 `int16_t` 水平偏移量

**5. TFT 点阵绘制**
- 位置：偏移量计算之后
- 调用 `drawDotGrid(offset)` 或自己写等效逻辑
- 使用 `tft.fillRect()` 绘制 4×5 点阵，整体平移

### 9.4 不需要修改的文件

以下底层文件已经稳定，算法开发阶段**不建议改动**：
- `lib/bmi270_sensor_api/*`（Bosch 官方库）
- `src/bmi270_arduino_interface.cpp/.h`（I2C 桥接层）
- `src/bmi270_driver.cpp/.h`（驱动类 API 已足够）
- `class ST7789` 的 SPI 驱动底层（`initSequence`、`sendCommand` 等）

### 9.5 参考仓库对应说明

本项目 BMI270 驱动参考自 `https://github.com/bxhsiman/BMI270_ESP32`，该仓库为 ESP-IDF 框架实现。本工程的 Arduino 适配版本将 ESP-IDF 的 `i2c_master` 驱动替换为 Arduino `Wire.h`，其余初始化逻辑（`bmi270_init` → 配置 → `bmi2_sensor_enable` → 循环读取）保持一致。

---

*文档生成日期：2026-05-02*  
*基于当前工程 commit 前的代码状态*
