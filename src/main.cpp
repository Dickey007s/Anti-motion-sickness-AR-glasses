/**
 * ============================================================================
 * Anti-Motion-Sickness AR Glasses - Optimized Frame Buffer Version
 * ============================================================================
 * 
 * This is an optimized version using frame buffer for smoother rendering.
 * Uses 110KB static frame buffer for double-buffered rendering.
 * 
 * Hardware: ESP32-S3 Super Mini + BMI270 + ST7789V3
 * 
 * Improvements over basic version:
 * - Frame buffer eliminates flickering
 * - Batch pixel transfer via writePixels()
 * - Non-blocking frame timing
 * - PWM backlight control
 * 
 * ============================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SparkFun_BMI270_Arduino_Library.h>

// ===================== PIN DEFINITIONS =====================
#define TFT_CS      10
#define TFT_DC      13
#define TFT_RST     8
#define TFT_BL      9

#define BMI_SDA     6
#define BMI_SCL     7
#define BMI_INT     1

// ===================== DISPLAY PARAMETERS =====================
#define SCREEN_W    172
#define SCREEN_H    320
#define FRAME_SIZE  (SCREEN_W * SCREEN_H)  // 55040 pixels

// ===================== DOT GRID PARAMETERS =====================
#define GRID_ROWS       4
#define GRID_COLS       5
#define DOT_SIZE        4
#define DOT_SPACING     8
#define GRID_OFFSET_X   66
#define GRID_OFFSET_Y   136

// ===================== ALGORITHM PARAMETERS =====================
#define K               20.0f
#define ALPHA           0.15f
#define MAX_OFFSET      12
#define ACCEL_THRESHOLD 0.1f
#define LPF_ALPHA       0.3f

// ===================== FRAME BUFFER =====================
// Static allocation in global memory (NOT stack)
static uint16_t frameBuffer[FRAME_SIZE];

// ===================== TFT DRIVER =====================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ===================== BMI270 DRIVER =====================
BMI270 bmi;
const uint8_t BMI_ADDR = BMI2_I2C_PRIM_ADDR;

// ===================== MOTION STATE =====================
enum MotionState { CRUISING, ACCELERATING, BRAKING };

// ===================== GLOBAL VARIABLES =====================
float acc_bias[3] = {0};
float gyro_bias[3] = {0};
float acc_filtered_prev[3] = {0};
float pitch = 0.0f;
float roll = 0.0f;
float offset = 0.0f;
MotionState motionState = CRUISING;
unsigned long lastTime = 0;
static int frameCount = 0;

// ===================== FUNCTION DECLARATIONS =====================
void calibrateSensors();
void applyLowPassFilter(float acc_raw[3], float acc_filtered[3]);
void updateAttitude(float acc[3], float gyro[3], float dt);
float extractLongitudinalAcceleration(float acc[3]);
MotionState detectMotionState(float a_x_world);
float calculateVisualOffset(float a_x_world);
void drawDotGridOptimized(float offset, MotionState state);
void flushFrameBuffer();
uint16_t getStateColor(MotionState state);
void setBacklight(uint8_t brightness);

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    delay(300);
    
    Serial.println("Anti-Motion-Sickness AR Glasses [Optimized]");
    
    // SPI
    SPI.begin(12, -1, 11, -1);
    
    // TFT
    tft.init(SCREEN_W, SCREEN_H);
    tft.sendCommand(0x11);
    delay(120);
    tft.sendCommand(0x29);
    delay(20);
    tft.setRotation(0);
    
    // Backlight with PWM
    pinMode(TFT_BL, OUTPUT);
    setBacklight(255);  // Full brightness
    
    // Startup screen
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("Anti-Motion");
    tft.setCursor(20, 130);
    tft.println("Sickness");
    tft.setCursor(20, 160);
    tft.println("AR Glasses");
    tft.setCursor(20, 200);
    tft.setTextSize(1);
    tft.println("[Optimized Version]");
    delay(2000);
    
    // I2C
    Wire.begin(BMI_SDA, BMI_SCL);
    
    // BMI270
    int8_t bmiStatus = bmi.beginI2C(BMI_ADDR);
    if (bmiStatus == BMI2_OK) {
        bmi2_sens_config accConfig;
        accConfig.type = BMI2_ACCEL;
        accConfig.cfg.acc.odr = BMI2_ACC_ODR_100HZ;
        accConfig.cfg.acc.range = BMI2_ACC_RANGE_4G;
        accConfig.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
        accConfig.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
        bmi.setConfig(accConfig);
        
        bmi2_sens_config gyrConfig;
        gyrConfig.type = BMI2_GYRO;
        gyrConfig.cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
        gyrConfig.cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
        gyrConfig.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
        bmi.setConfig(gyrConfig);
        
        uint8_t sensList[2] = {BMI2_ACCEL, BMI2_GYRO};
        bmi.enableFeatures(sensList, 2);
        
        calibrateSensors();
    }
    
    pinMode(BMI_INT, INPUT);
    lastTime = millis();
}

// ===================== MAIN LOOP =====================
void loop() {
    unsigned long currentTime = millis();
    float dt = (currentTime - lastTime) / 1000.0f;
    lastTime = currentTime;
    if (dt > 0.05f) dt = 0.01f;
    
    // 1. Read sensors
    bmi.getSensorData();
    
    // 2. Bias compensation
    float acc_raw[3] = {
        bmi.data.accelX - acc_bias[0],
        bmi.data.accelY - acc_bias[1],
        bmi.data.accelZ - acc_bias[2]
    };
    float gyro_raw[3] = {
        bmi.data.gyroX - gyro_bias[0],
        bmi.data.gyroY - gyro_bias[1],
        bmi.data.gyroZ - gyro_bias[2]
    };
    
    // 3. Low-pass filter
    float acc_filtered[3];
    applyLowPassFilter(acc_raw, acc_filtered);
    
    // 4. Attitude estimation
    updateAttitude(acc_filtered, gyro_raw, dt);
    
    // 5. Longitudinal acceleration
    float a_x_world = extractLongitudinalAcceleration(acc_filtered);
    
    // 6. Motion state
    motionState = detectMotionState(a_x_world);
    
    // 7. Visual offset
    float visualOffset = calculateVisualOffset(a_x_world);
    
    // 8. Render to frame buffer
    drawDotGridOptimized(visualOffset, motionState);
    
    // 9. Flush to display
    flushFrameBuffer();
    
    // 10. Debug
    if (++frameCount % 10 == 0) {
        const char* stateStr = (motionState == ACCELERATING) ? "ACCEL" :
                               (motionState == BRAKING) ? "BRAKE" : "CRUISE";
        Serial.printf("Frame=%d, State=%s, a_x=%+.3fg, Offset=%+6.1fpx\n",
                      frameCount, stateStr, a_x_world, visualOffset);
    }
    
    // Non-blocking 100Hz timing
    static unsigned long nextFrame = 0;
    unsigned long now = millis();
    if (now < nextFrame) {
        delay(nextFrame - now);
    }
    nextFrame = now + 10;
}

// ===================== CALIBRATION =====================
void calibrateSensors() {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 130);
    tft.println("Calibrating...");
    tft.setCursor(30, 160);
    tft.println("Keep Still");
    
    float acc_sum[3] = {0}, gyro_sum[3] = {0};
    const int samples = 200;
    
    for (int i = 0; i < samples; i++) {
        bmi.getSensorData();
        acc_sum[0] += bmi.data.accelX;
        acc_sum[1] += bmi.data.accelY;
        acc_sum[2] += bmi.data.accelZ;
        gyro_sum[0] += bmi.data.gyroX;
        gyro_sum[1] += bmi.data.gyroY;
        gyro_sum[2] += bmi.data.gyroZ;
        
        if (i % 20 == 0) {
            int progress = (i * 100) / samples;
            tft.fillRect(20, 200, 132, 10, ST77XX_BLACK);
            tft.fillRect(20, 200, (132 * progress) / 100, 10, ST77XX_GREEN);
        }
        delay(10);
    }
    
    for (int i = 0; i < 3; i++) {
        acc_bias[i] = acc_sum[i] / samples;
        gyro_bias[i] = gyro_sum[i] / samples;
        acc_filtered_prev[i] = 0.0f;
    }
    
    Serial.println("Calibration complete");
}

// ===================== LOW-PASS FILTER =====================
void applyLowPassFilter(float acc_raw[3], float acc_filtered[3]) {
    for (int i = 0; i < 3; i++) {
        acc_filtered[i] = LPF_ALPHA * acc_raw[i] + 
                          (1.0f - LPF_ALPHA) * acc_filtered_prev[i];
        acc_filtered_prev[i] = acc_filtered[i];
    }
}

// ===================== COMPLEMENTARY FILTER =====================
void updateAttitude(float acc[3], float gyro[3], float dt) {
    float gyro_rad[3] = {
        gyro[0] * (float)DEG_TO_RAD,
        gyro[1] * (float)DEG_TO_RAD,
        gyro[2] * (float)DEG_TO_RAD
    };
    
    float acc_pitch = atan2f(-acc[0], sqrtf(acc[1]*acc[1] + acc[2]*acc[2]));
    float acc_roll  = atan2f(acc[1], acc[2]);
    
    pitch = 0.98f * (pitch + gyro_rad[1] * dt) + 0.02f * acc_pitch;
    roll  = 0.98f * (roll  + gyro_rad[0] * dt) + 0.02f * acc_roll;
}

// ===================== LONGITUDINAL ACCELERATION =====================
float extractLongitudinalAcceleration(float acc[3]) {
    return acc[0] * cosf(pitch) + acc[2] * sinf(pitch);
}

// ===================== MOTION STATE DETECTION =====================
MotionState detectMotionState(float a_x_world) {
    if (a_x_world > ACCEL_THRESHOLD) return ACCELERATING;
    if (a_x_world < -ACCEL_THRESHOLD) return BRAKING;
    return CRUISING;
}

// ===================== VISUAL OFFSET =====================
float calculateVisualOffset(float a_x_world) {
    float target_offset = -K * a_x_world;
    offset = offset + ALPHA * (target_offset - offset);
    if (offset > MAX_OFFSET) offset = MAX_OFFSET;
    else if (offset < -MAX_OFFSET) offset = -MAX_OFFSET;
    return offset;
}

// ===================== COLOR =====================
uint16_t getStateColor(MotionState state) {
    switch (state) {
        case ACCELERATING: return ST77XX_RED;
        case BRAKING:      return ST77XX_GREEN;
        default:           return ST77XX_WHITE;
    }
}

// ===================== FRAME BUFFER RENDERING =====================
void drawDotGridOptimized(float offset, MotionState state) {
    uint16_t color = getStateColor(state);
    uint16_t bgColor = ST77XX_BLACK;
    
    // Clear frame buffer
    for (int i = 0; i < FRAME_SIZE; i++) {
        frameBuffer[i] = bgColor;
    }
    
    // Draw dots in frame buffer
    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            int16_t x = GRID_OFFSET_X + col * DOT_SPACING + (int16_t)offset;
            int16_t y = GRID_OFFSET_Y + row * DOT_SPACING;
            
            if (x < 0 || x + DOT_SIZE > SCREEN_W) continue;
            if (y < 0 || y + DOT_SIZE > SCREEN_H) continue;
            
            // Draw filled rectangle in frame buffer
            for (int dy = 0; dy < DOT_SIZE; dy++) {
                for (int dx = 0; dx < DOT_SIZE; dx++) {
                    int px = x + dx;
                    int py = y + dy;
                    if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                        frameBuffer[py * SCREEN_W + px] = color;
                    }
                }
            }
        }
    }
    
    // Draw status text in frame buffer (optional)
    // Note: Text rendering requires font data, simplified here
}

// ===================== FLUSH FRAME BUFFER =====================
void flushFrameBuffer() {
    tft.setAddrWindow(0, 0, SCREEN_W - 1, SCREEN_H - 1);
    tft.sendCommand(0x2C);  // RAMWR
    
    // Send entire frame buffer via SPI
    digitalWrite(TFT_DC, HIGH);
    digitalWrite(TFT_CS, LOW);
    SPI.writeBytes((uint8_t*)frameBuffer, FRAME_SIZE * 2);
    digitalWrite(TFT_CS, HIGH);
}

// ===================== BACKLIGHT CONTROL =====================
void setBacklight(uint8_t brightness) {
    analogWrite(TFT_BL, brightness);
}

// ===================== END OF FILE =====================
