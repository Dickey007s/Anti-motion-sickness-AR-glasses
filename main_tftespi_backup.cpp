#include <Arduino.h>
#include <TFT_eSPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
    Serial.begin(115200);
    delay(1000);
    Serial.println("=== TFT Minimum Test ===");

    // ============================================================
    // 排查项2：背光尝试低电平点亮（很多 ST7789 模块背光是 Active Low）
    // 如果屏幕有画面但很暗，说明背光极性反了，可改为 HIGH 再试
    // ============================================================
    #ifdef TFT_BL
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);
    Serial.printf("Backlight pin %d set LOW (Active Low)\n", TFT_BL);
    #else
    Serial.println("WARNING: TFT_BL not defined! Backlight will not be controlled.");
    #endif

    // ============================================================
    // 排查项1：TFT_eSPI 初始化（引脚定义见 include/TFT_Setup.h）
    // 当前配置：MOSI=11, SCK=12, CS=10, DC=13, RST=8, BL=9
    // 如实际接线不同，请修改 include/TFT_Setup.h
    // ============================================================
    Serial.println("TFT init start...");
    tft.init();
    Serial.println("TFT init done");

    // ============================================================
    // 排查项3：ST7789V3 手动唤醒
    // TFT_eSPI 默认初始化序列可能不完全匹配 V3 批次，
    // 手动发送 Sleep Out + Display ON 确保退出休眠
    // ============================================================
    tft.writecommand(0x11);  // Sleep Out
    delay(120);
    tft.writecommand(0x29);  // Display ON
    delay(20);

    tft.setRotation(0);
    tft.fillScreen(TFT_BLACK);

    int w = tft.width();
    int h = tft.height();
    Serial.printf("Screen size: %d x %d\n", w, h);

    // ============================================================
    // 画三色块测试（172x320 屏幕，色块分开放避免超出边界）
    // ============================================================
    tft.fillRect(10, 10, 50, 50, TFT_RED);
    tft.fillRect(70, 10, 50, 50, TFT_GREEN);
    tft.fillRect(10, 70, 50, 50, TFT_BLUE);

    Serial.println("Test pattern drawn. If screen stays dark, check:");
    Serial.println("  1. Wiring matches TFT_Setup.h pin definitions");
    Serial.println("  2. Backlight pin polarity (try HIGH if LOW doesn't work)");
    Serial.println("  3. GND and 3.3V are properly connected");
}

void loop() {
    // 保持画面，什么都不做
}
