#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ===================== 引脚定义 =====================
#define TFT_CS   10
#define TFT_DC   13
#define TFT_RST  8

// ===================== 显示参数 =====================
#define SCREEN_W  172
#define SCREEN_H  320

// ===================== TFT 驱动 =====================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ===================== 点阵算法参数（Phase 2 实现）====================
// TODO Phase 2: 定义点阵规格 4行 x 5列，点间距 8px
// TODO Phase 2: 定义增益 K = 20 px/g，平滑系数 alpha = 0.15，最大偏移 MAX_OFFSET = 12 px
// TODO Phase 2: 定义运动状态枚举：ACCELERATING / BRAKING / CRUISING
// TODO Phase 2: 定义加速度阈值 ACCEL_THRESHOLD = 0.1g

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); }
  delay(300);

  // ---------- SPI 初始化 ----------
  SPI.begin(12, -1, 11, -1);

  // ---------- TFT 初始化 ----------
  tft.init(SCREEN_W, SCREEN_H);
  tft.sendCommand(0x11);  // Sleep Out
  delay(120);
  tft.sendCommand(0x29);  // Display ON
  delay(20);

  tft.setRotation(0);

  // ---------- 最简纯白显示（Phase 1 验证通过）----------
  tft.fillScreen(ST77XX_WHITE);

  // ===================== Phase 2 预留区 =====================
  // TODO: BMI270 初始化（I2C，SDA=GPIO6, SCL=GPIO7，地址 0x68）
  // TODO: 配置 BMI270 为 Normal Mode，ODR=100Hz，±4g / ±500°/s
  // TODO: 上电静止标定 2 秒，采样 200 组求零偏
}

void loop() {
  // ===================== Phase 2 预留区：主循环 10ms 周期 (100Hz) =====================

  // TODO: 读取 BMI270 加速度计 + 陀螺仪原始数据
  // TODO: 零偏补偿与低通滤波：a_filtered = 0.3 * a_raw + 0.7 * a_filtered_prev

  // TODO: 互补滤波姿态解算
  //   pitch = 0.98 * (pitch + gyr_y * dt) + 0.02 * atan2(-acc_x, sqrt(acc_y^2 + acc_z^2))
  //   roll  = 0.98 * (roll  + gyr_x * dt) + 0.02 * atan2(acc_y, acc_z)

  // TODO: 世界坐标系纵向加速度提取
  //   a_x_world = a_x * cos(pitch) + a_z * sin(pitch)

  // TODO: 运动状态检测
  //   if a_x_world >  ACCEL_THRESHOLD: state = ACCELERATING
  //   if a_x_world < -BRAKE_THRESHOLD: state = BRAKING
  //   else:                            state = CRUISING

  // TODO: 点阵目标偏移量计算
  //   target_offset = -K * a_x_world

  // TODO: 一阶平滑滤波
  //   offset = offset + alpha * (target_offset - offset)

  // TODO: 限幅保护
  //   offset = clamp(offset, -MAX_OFFSET, MAX_OFFSET)

  // TODO: TFT 绘制点阵帧
  //   1. 清屏（fillScreen ST77XX_BLACK 或深色背景）
  //   2. 绘制 4x5 点阵，每点 x 坐标 += offset
  //   3. 可选：根据状态改变点阵颜色（加速红色 / 减速绿色 / 匀速白色）

  // delay(10);  // 100Hz 周期控制
}
