#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ===================== 引脚定义 =====================
// Super Mini 可用 GP1~GP13，当前接线：
//   SCL(SCK)=12, SDA(MOSI)=11, CS=10, DC=13, RST=8, BL=9
#define TFT_CS   10
#define TFT_DC   13
#define TFT_RST  8
#define TFT_BL   9

// ===================== 分辨率 =====================
// 1.47" ST7789V3 实际分辨率 172x320，但芯片物理显存为 240x320，
// 有效画面需要向中间偏移 34 列（(240-172)/2=34），否则画面偏到左边或看不到。
#define SCREEN_W   172
#define SCREEN_H   320
#define COL_OFFSET 34

// ===================== 自定义驱动类 =====================
// Adafruit 库的 _colstart/_rowstart 是 protected，子类可以直接修改。
// 不继承的话无法设置 172x320 必需的 34 列偏移。
class ST7789_172x320 : public Adafruit_ST7789 {
public:
  ST7789_172x320(int8_t cs, int8_t dc, int8_t rst)
    : Adafruit_ST7789(cs, dc, rst) {}

  void init172() {
    init(SCREEN_W, SCREEN_H);
    _colstart = COL_OFFSET;  // 关键：34 列偏移
    _rowstart = 0;
  }
};

ST7789_172x320 tft = ST7789_172x320(TFT_CS, TFT_DC, TFT_RST);

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ST7789 SuperMini 172x320 Test ===");

  // ===================== 背光 =====================
  // 大部分 ST7789 模块背光是 Active Low（低电平亮）。
  // 如果屏幕始终全黑，把这行的 LOW 改成 HIGH 再试。
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  Serial.println("Backlight: LOW (Active Low)");

  // ===================== SPI 引脚显式指定 =====================
  // ESP32-S3 默认 FSPI 引脚恰好是 SCK=12、MOSI=11，
  // 这里显式调用是为了 100% 确认，不受任何默认值影响。
  // 第 4 个参数 SS 传 -1，因为 CS 由 Adafruit 库软件控制。
  SPI.begin(12, -1, 11, -1);
  Serial.println("SPI.begin(12, -1, 11, -1) done");

  // ===================== 初始化 =====================
  Serial.println("TFT init 172x320...");
  tft.init172();
  Serial.println("TFT init done");

  // ===================== ST7789V3 手动唤醒 =====================
  // 部分 V3 批次在默认 init 序列后仍处于休眠，手动补发 Sleep Out + Display ON
  tft.sendCommand(0x11);  // Sleep Out
  delay(120);
  tft.sendCommand(0x29);  // Display ON
  delay(20);
  Serial.println("Manual wake-up commands sent");

  // ===================== 颜色调试 =====================
  // 如果红绿蓝色块颜色反了（比如红变蓝），取消下面一行的注释
  // tft.sendCommand(0x21);  // 0x21=反色开启, 0x20=反色关闭

  // ===================== 画测试画面 =====================
  tft.setRotation(0);
  tft.fillScreen(ST77XX_BLACK);

  int w = tft.width();   // 应返回 172
  int h = tft.height();  // 应返回 320
  Serial.printf("tft.width=%d, tft.height=%d\n", w, h);

  // 第一行：红、绿、蓝三色块（占满宽度，验证偏移是否正确）
  int bw = w / 3;
  tft.fillRect(0,      0, bw, 60, ST77XX_RED);
  tft.fillRect(bw,     0, bw, 60, ST77XX_GREEN);
  tft.fillRect(bw * 2, 0, w - bw * 2, 60, ST77XX_BLUE);

  // 白色外框（验证整个 172x320 区域是否都被正确寻址）
  tft.drawRect(0, 0, w, h, ST77XX_WHITE);

  // 文字
  tft.setCursor(5, 80);
  tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  tft.setTextSize(2);
  tft.println("ST7789");
  tft.setCursor(5, 105);
  tft.println("172x320");

  Serial.println("Test pattern drawn.");
  Serial.println("If screen is still black, check:");
  Serial.println("  1. Wiring matches pin definitions above");
  Serial.println("  2. Backlight polarity (try HIGH if LOW doesn't work)");
  Serial.println("  3. 3.3V power is stable");
}

void loop() {
  // 每 3 秒闪烁背光一次，方便确认 GPIO9 是否真的控制了背光
  static unsigned long last = 0;
  if (millis() - last > 3000) {
    last = millis();
    static bool bl = true;
    bl = !bl;
    digitalWrite(TFT_BL, bl ? LOW : HIGH);
    Serial.println(bl ? "BL -> LOW (ON)" : "BL -> HIGH (OFF)");
  }
}
