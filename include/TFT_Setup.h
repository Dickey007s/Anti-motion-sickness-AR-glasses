// TFT_eSPI configuration for 1.47" ST7789 172x320 on ESP32-S3
// Loaded via platformio.ini build_flags: -DUSER_SETUP_LOADED -include "include/TFT_Setup.h"
//
// 当前引脚配置（匹配 Super Mini GP1~GP13 实际接线）：
//   SDA(MOSI)=11, SCL(SCK)=12, CS=10, DC=13, RST=8, BL=9
//   注意：屏厂丝印为 SCL/SDA，实际是 SPI 信号（SCL=SCK, SDA=MOSI）
//
// GP8~GP13 均不是 ESP32-S3 Strap Pin（Strap Pin 为 GPIO0/3/45/46），
// 但 GP8/GP9 已与 TFT RST/BL 占用，BMI270 后续请使用 GP6/GP7。

#define ST7789_DRIVER

#define TFT_WIDTH  172
#define TFT_HEIGHT 320

#define TFT_RGB_ORDER TFT_BGR

#define TFT_MOSI 11
#define TFT_SCLK 12
#define TFT_CS   10
#define TFT_DC   13
#define TFT_RST  8
#define TFT_BL   9
#define TFT_BACKLIGHT_ON HIGH

#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

#define SPI_FREQUENCY  27000000
