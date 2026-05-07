#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SparkFun_BMI270_Arduino_Library.h>

// ===================== 引脚定义 =====================
#define TFT_CS   10
#define TFT_DC   13
#define TFT_RST  8

#define BMI_SDA   6
#define BMI_SCL   7
#define BMI_INT   1

// ===================== 显示参数 =====================
#define SCREEN_W  172
#define SCREEN_H  320

// ===================== TFT 驱动 =====================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ===================== BMI270 驱动 =====================
BMI270 bmi;
const uint8_t BMI_ADDR = BMI2_I2C_PRIM_ADDR; // 0x68

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

  // ===================== I2C 初始化 =====================
  Wire.begin(BMI_SDA, BMI_SCL);

  // ===================== BMI270 初始化 =====================
  int8_t bmiStatus = bmi.beginI2C(BMI_ADDR);
  if (bmiStatus != BMI2_OK) {
    Serial.printf("BMI270 初始化失败，状态码: %d\n", bmiStatus);
    // 不阻塞，继续运行，后续 loop 中会跳过 BMI270 读取
  } else {
    Serial.println("BMI270 连接成功");

    // 配置加速度计：100Hz，±4g
    bmi2_sens_config accConfig;
    accConfig.type = BMI2_ACCEL;
    accConfig.cfg.acc.odr = BMI2_ACC_ODR_100HZ;
    accConfig.cfg.acc.range = BMI2_ACC_RANGE_4G;
    accConfig.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
    accConfig.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
    bmi.setConfig(accConfig);

    // 配置陀螺仪：100Hz
    bmi2_sens_config gyrConfig;
    gyrConfig.type = BMI2_GYRO;
    gyrConfig.cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
    gyrConfig.cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
    gyrConfig.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
    bmi.setConfig(gyrConfig);

    // 启用加速度计和陀螺仪
    uint8_t sensList[2] = {BMI2_ACCEL, BMI2_GYRO};
    bmi.enableFeatures(sensList, 2);

    Serial.println("BMI270 配置完成：Accel 100Hz/4g，Gyro 100Hz");
  }

  // 配置中断引脚（预留，暂未使用中断）
  pinMode(BMI_INT, INPUT);

  // TODO: 上电静止标定 2 秒，采样 200 组求零偏
}

void loop() {
  // ===================== Phase 2 预留区：主循环 10ms 周期 (100Hz) =====================

  // 读取 BMI270 加速度计 + 陀螺仪数据
  bmi.getSensorData();

  // 打印调试数据（调试用，后续可注释掉）
  Serial.printf("ACC: %.3f %.3f %.3f  GYR: %.3f %.3f %.3f\n",
                bmi.data.accelX, bmi.data.accelY, bmi.data.accelZ,
                bmi.data.gyroX, bmi.data.gyroY, bmi.data.gyroZ);

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

  delay(10);  // 100Hz 周期控制
}
