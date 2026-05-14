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
  // ESP32-S3 使用 USBSerial/JTAG 控制器，必须用 USBSerial 而非 Serial
  USBSerial.begin(115200);
  delay(500);
  USBSerial.println("\n=== ESP32-S3 启动 ===");

  // ---------- SPI 初始化 ----------
  SPI.begin(12, -1, 11, -1);

  // ---------- TFT 初始化 ----------
  tft.init(SCREEN_W, SCREEN_H);
  tft.sendCommand(0x11);  // Sleep Out
  delay(120);
  tft.sendCommand(0x29);  // Display ON
  delay(20);
  tft.setRotation(0);
  tft.fillScreen(ST77XX_WHITE);
  USBSerial.println("TFT 初始化完成");

  // ===================== I2C 初始化 =====================
  Wire.begin(BMI_SDA, BMI_SCL);

  // ===================== BMI270 初始化 =====================
  int8_t bmiStatus = bmi.beginI2C(BMI_ADDR);
  if (bmiStatus != BMI2_OK) {
    USBSerial.printf("BMI270 初始化失败，状态码: %d\n", bmiStatus);
  } else {
    USBSerial.println("BMI270 连接成功");

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

    USBSerial.println("BMI270 配置完成：Accel 100Hz/4g，Gyro 100Hz");
  }

  // 配置中断引脚（预留，暂未使用中断）
  pinMode(BMI_INT, INPUT);

  USBSerial.println("=== 开始输出六轴传感器数据 ===\n");
}

void loop() {
  // 读取 BMI270 加速度计 + 陀螺仪数据
  bmi.getSensorData();

  // 打印六轴数据
  USBSerial.printf("ACC: %.3f %.3f %.3f  GYR: %.3f %.3f %.3f\n",
                   bmi.data.accelX, bmi.data.accelY, bmi.data.accelZ,
                   bmi.data.gyroX, bmi.data.gyroY, bmi.data.gyroZ);

  // TODO Phase 2: 零偏补偿、低通滤波、姿态解算、点阵绘制

  delay(200);  // 5Hz 输出，避免刷屏
}
