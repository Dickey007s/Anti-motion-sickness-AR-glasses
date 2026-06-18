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
#include <Preferences.h>

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

// ===================== PARTICLE SYSTEM PARAMETERS =====================
#define MAX_PARTICLES       50
#define CENTER_X            (SCREEN_W / 2)      // 86
#define CENTER_Y            (SCREEN_H / 2)      // 160
#define SPAWN_RADIUS        8.0f
#define MAX_PARTICLE_SPEED  3.0f
#define MIN_PARTICLE_AGE    30
#define MAX_PARTICLE_AGE    60
#define MAX_PARTICLE_SIZE   6
#define SPAWN_RATE_CRUISE   1
#define SPAWN_RATE_MOTION   3
#define BIAS_FACTOR         0.25f
#define SPEED_SCALE         0.30f

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

// ===================== NON-VOLATILE STORAGE =====================
Preferences prefs;
#define PREFS_NAMESPACE     "ams_glasses"
#define KEY_CAL_VALID       "cal_valid"
#define KEY_ACC_BIAS_X      "acc_bias_x"
#define KEY_ACC_BIAS_Y      "acc_bias_y"
#define KEY_ACC_BIAS_Z      "acc_bias_z"
#define KEY_GYRO_BIAS_X     "gyro_bias_x"
#define KEY_GYRO_BIAS_Y     "gyro_bias_y"
#define KEY_GYRO_BIAS_Z     "gyro_bias_z"

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
static bool bmiReady = false;   // BMI270 是否初始化成功

// ===================== PARTICLE DATA STRUCTURE =====================
struct Particle {
    float x;          // sub-pixel center X
    float y;          // sub-pixel center Y
    float vx;         // horizontal velocity (px/frame)
    float vy;         // vertical velocity (px/frame)
    uint8_t size;     // current drawn size in pixels
    uint8_t brightness; // current brightness [0,255]
    uint8_t age;      // current age in frames
    uint8_t maxAge;   // life limit in frames
    bool active;      // slot in use
};

// Fixed-size particle pool - no malloc/free on the render path
static Particle particlePool[MAX_PARTICLES];

// ===================== FUNCTION DECLARATIONS =====================
void calibrateSensors();
void applyLowPassFilter(float acc_raw[3], float acc_filtered[3]);
void updateAttitude(float acc[3], float gyro[3], float dt);
float extractLongitudinalAcceleration(float acc[3]);
MotionState detectMotionState(float a_x_world);
float calculateVisualOffset(float a_x_world);
void drawDotGridOptimized(float visualOffset, MotionState state);
static inline float randomFloat();
static inline uint16_t dimColor565(uint16_t color, uint8_t brightness);
static int findInactiveParticle();
static void spawnParticle(float biasX);
static void drawParticle(const Particle& p, uint16_t baseColor);
void flushFrameBuffer();
uint16_t getStateColor(MotionState state);
void setBacklight(uint8_t brightness);
bool loadCalibration();
void saveCalibration();
void i2cScan();
bool initBMI270(uint8_t addr);
void showErrorScreen(const char* title, const char* detail);

// ===================== SETUP =====================
void setup() {
    Serial.begin(115200);
    // Wait for serial with timeout; don't hang if running standalone
    unsigned long serialTimeout = millis();
    while (!Serial && (millis() - serialTimeout < 3000)) {
        delay(10);
    }
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
    Wire.setClock(100000);  // 100kHz，避免部分模块/layout 跟不上
    delay(100);
    i2cScan();  // 扫描 I2C 总线，方便排查地址问题

    // BMI270
    bmiReady = initBMI270(BMI2_I2C_PRIM_ADDR);  // 0x68
    if (!bmiReady) {
        Serial.println("Primary I2C address failed, trying secondary...");
        bmiReady = initBMI270(BMI2_I2C_SEC_ADDR);  // 0x69
    }

    if (bmiReady) {
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

        // Try to load previously saved calibration from flash
        if (!loadCalibration()) {
            calibrateSensors();
            saveCalibration();
        }
    } else {
        showErrorScreen("BMI270 ERROR", "Check wiring/I2C");
        Serial.println("WARNING: Running in DEMO mode without BMI270");
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
    
    float visualOffset = 0.0f;
    MotionState currentState = CRUISING;

    if (bmiReady) {
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
        currentState = detectMotionState(a_x_world);

        // 7. Visual offset
        visualOffset = calculateVisualOffset(a_x_world);
    } else {
        // Demo mode: use sine wave to simulate acceleration/deceleration
        static float demoTime = 0.0f;
        demoTime += dt;
        visualOffset = MAX_OFFSET * sinf(demoTime * 2.0f);
        if (visualOffset > 2.0f) currentState = ACCELERATING;
        else if (visualOffset < -2.0f) currentState = BRAKING;
        else currentState = CRUISING;
    }

    // 8. Render to frame buffer
    drawDotGridOptimized(visualOffset, currentState);

    // 9. Flush to display
    flushFrameBuffer();

    // 10. Debug
    if (++frameCount % 10 == 0) {
        const char* stateStr = (currentState == ACCELERATING) ? "ACCEL" :
                               (currentState == BRAKING) ? "BRAKE" : "CRUISE";
        Serial.printf("Frame=%d, State=%s, Offset=%+6.1fpx, BMI=%s\n",
                      frameCount, stateStr, visualOffset,
                      bmiReady ? "OK" : "DEMO");
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
    Serial.println("Starting sensor calibration...");
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setCursor(30, 130);
    tft.println("Calibrating...");
    tft.setCursor(30, 160);
    tft.println("Keep Still");

    float acc_sum[3] = {0}, gyro_sum[3] = {0};
    const int samples = 200;
    int validSamples = 0;

    for (int i = 0; i < samples; i++) {
        int8_t status = bmi.getSensorData();
        if (status != BMI2_OK) {
            Serial.printf("Calibration sample %d failed, status: %d\n", i, status);
            delay(10);
            continue;
        }
        validSamples++;
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

    if (validSamples == 0) {
        Serial.println("Calibration failed: no valid sensor samples");
        // Use zero bias as fallback so loop() can still run
        validSamples = 1;
    }

    for (int i = 0; i < 3; i++) {
        acc_bias[i] = acc_sum[i] / validSamples;
        gyro_bias[i] = gyro_sum[i] / validSamples;
        acc_filtered_prev[i] = 0.0f;
    }

    Serial.printf("Calibration complete (%d/%d valid samples)\n", validSamples, samples);
    Serial.printf("Acc bias: %.3f %.3f %.3f\n", acc_bias[0], acc_bias[1], acc_bias[2]);
    Serial.printf("Gyro bias: %.3f %.3f %.3f\n", gyro_bias[0], gyro_bias[1], gyro_bias[2]);
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

// ===================== PARTICLE HELPERS =====================
static inline float randomFloat() {
    return (float)random(0, 1000) / 1000.0f;
}

static inline uint16_t dimColor565(uint16_t color, uint8_t brightness) {
    if (brightness >= 255) return color;
    uint8_t r = (color >> 11) & 0x1F;
    uint8_t g = (color >> 5) & 0x3F;
    uint8_t b = color & 0x1F;
    r = (uint8_t)((r * brightness) / 255);
    g = (uint8_t)((g * brightness) / 255);
    b = (uint8_t)((b * brightness) / 255);
    return ((uint16_t)r << 11) | ((uint16_t)g << 5) | b;
}

static int findInactiveParticle() {
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (!particlePool[i].active) return i;
    }
    return -1;
}

static void spawnParticle(float biasX) {
    int idx = findInactiveParticle();
    if (idx < 0) return;

    Particle& p = particlePool[idx];

    // Birth near the screen center
    float angle = randomFloat() * 2.0f * PI;
    float radius = randomFloat() * SPAWN_RADIUS;
    p.x = (float)CENTER_X + cosf(angle) * radius;
    p.y = (float)CENTER_Y + sinf(angle) * radius;
    p.age = 0;
    p.maxAge = (uint8_t)(MIN_PARTICLE_AGE + (int)(randomFloat() * (MAX_PARTICLE_AGE - MIN_PARTICLE_AGE)));
    p.size = 1;
    p.brightness = 0;

    // Diffusion direction: radial outward, biased opposite to sensed acceleration.
    // visualOffset > 0  -> flow to +X (right)
    // visualOffset < 0  -> flow to -X (left)
    float absBias = biasX;
    if (absBias < 0.0f) absBias = -absBias;
    float targetAngle = (biasX > 0.0f) ? 0.0f :
                        (biasX < 0.0f) ? PI  : angle;
    float blend = absBias * 0.15f;
    if (blend > 0.85f) blend = 0.85f;
    float dirAngle = angle * (1.0f - blend) + targetAngle * blend;
    dirAngle += (randomFloat() - 0.5f) * 0.4f;

    float speed = (absBias * SPEED_SCALE) + 0.4f;
    if (speed > MAX_PARTICLE_SPEED) speed = MAX_PARTICLE_SPEED;
    if (absBias < 0.5f) {
        // Cruising: slow, uniform radial expansion with gentle drift
        speed = 0.4f + randomFloat() * 0.6f;
    }

    p.vx = cosf(dirAngle) * speed + biasX * 0.4f;
    p.vy = sinf(dirAngle) * speed;
    p.active = true;
}

static void drawParticle(const Particle& p, uint16_t baseColor) {
    uint8_t size = p.size;
    uint8_t brightness = p.brightness;
    if (size == 0 || brightness == 0) return;

    uint16_t color = dimColor565(baseColor, brightness);

    int16_t left = (int16_t)(p.x - (float)size * 0.5f);
    int16_t top  = (int16_t)(p.y - (float)size * 0.5f);

    for (int dy = 0; dy < size; dy++) {
        int16_t py = top + dy;
        if (py < 0 || py >= SCREEN_H) continue;
        for (int dx = 0; dx < size; dx++) {
            int16_t px = left + dx;
            if (px < 0 || px >= SCREEN_W) continue;
            frameBuffer[py * SCREEN_W + px] = color;
        }
    }
}

// ===================== FRAME BUFFER RENDERING =====================
void drawDotGridOptimized(float visualOffset, MotionState state) {
    // Seed the PRNG on the very first render frame
    static bool seeded = false;
    if (!seeded) {
        randomSeed(millis());
        seeded = true;
    }

    uint16_t baseColor = getStateColor(state);

    // Clear frame buffer
    for (int i = 0; i < FRAME_SIZE; i++) {
        frameBuffer[i] = ST77XX_BLACK;
    }

    float biasX = visualOffset * BIAS_FACTOR;
    int spawnRate = (state == CRUISING) ? SPAWN_RATE_CRUISE : SPAWN_RATE_MOTION;

    // Update existing particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
        Particle& p = particlePool[i];
        if (!p.active) continue;
        p.x += p.vx;
        p.y += p.vy;
        p.age++;

        // Update size and brightness based on life progress
        float t = (float)p.age / (float)p.maxAge;
        if (t > 1.0f) t = 1.0f;
        p.size = 1 + (uint8_t)(t * (MAX_PARTICLE_SIZE - 1));
        p.brightness = (uint8_t)(255.0f * sinf(t * (float)PI));

        if (p.age >= p.maxAge ||
            p.x < -2.0f || p.x > (float)SCREEN_W + 2.0f ||
            p.y < -2.0f || p.y > (float)SCREEN_H + 2.0f) {
            p.active = false;
        }
    }

    // Spawn new particles near the center
    for (int i = 0; i < spawnRate; i++) {
        spawnParticle(biasX);
    }

    // Render active particles
    for (int i = 0; i < MAX_PARTICLES; i++) {
        if (particlePool[i].active) {
            drawParticle(particlePool[i], baseColor);
        }
    }
}

// ===================== FLUSH FRAME BUFFER =====================
void flushFrameBuffer() {
    tft.startWrite();
    tft.setAddrWindow(0, 0, SCREEN_W, SCREEN_H);
    tft.writePixels(frameBuffer, FRAME_SIZE);
    tft.endWrite();
}

// ===================== BACKLIGHT CONTROL =====================
void setBacklight(uint8_t brightness) {
    analogWrite(TFT_BL, brightness);
}

// ===================== CALIBRATION PERSISTENCE =====================
// Save calibration biases to ESP32 NVS so they survive power cycles.
void saveCalibration() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putBool(KEY_CAL_VALID, true);
    prefs.putFloat(KEY_ACC_BIAS_X, acc_bias[0]);
    prefs.putFloat(KEY_ACC_BIAS_Y, acc_bias[1]);
    prefs.putFloat(KEY_ACC_BIAS_Z, acc_bias[2]);
    prefs.putFloat(KEY_GYRO_BIAS_X, gyro_bias[0]);
    prefs.putFloat(KEY_GYRO_BIAS_Y, gyro_bias[1]);
    prefs.putFloat(KEY_GYRO_BIAS_Z, gyro_bias[2]);
    prefs.end();
    Serial.println("Calibration saved to flash");
}

// Load calibration biases from ESP32 NVS. Returns true if valid data exists.
bool loadCalibration() {
    prefs.begin(PREFS_NAMESPACE, true);
    if (!prefs.getBool(KEY_CAL_VALID, false)) {
        prefs.end();
        Serial.println("No saved calibration found");
        return false;
    }

    acc_bias[0] = prefs.getFloat(KEY_ACC_BIAS_X, 0.0f);
    acc_bias[1] = prefs.getFloat(KEY_ACC_BIAS_Y, 0.0f);
    acc_bias[2] = prefs.getFloat(KEY_ACC_BIAS_Z, 0.0f);
    gyro_bias[0] = prefs.getFloat(KEY_GYRO_BIAS_X, 0.0f);
    gyro_bias[1] = prefs.getFloat(KEY_GYRO_BIAS_Y, 0.0f);
    gyro_bias[2] = prefs.getFloat(KEY_GYRO_BIAS_Z, 0.0f);
    prefs.end();

    Serial.println("Loaded calibration from flash");
    Serial.printf("  Acc bias:  %.3f %.3f %.3f\n", acc_bias[0], acc_bias[1], acc_bias[2]);
    Serial.printf("  Gyro bias: %.3f %.3f %.3f\n", gyro_bias[0], gyro_bias[1], gyro_bias[2]);
    return true;
}

// ===================== DIAGNOSTIC HELPERS =====================
void i2cScan() {
    Serial.println("\n--- I2C Scan ---");
    int found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        uint8_t err = Wire.endTransmission();
        if (err == 0) {
            Serial.printf("  Found I2C device at 0x%02X\n", addr);
            found++;
        }
    }
    if (found == 0) {
        Serial.println("  No I2C device found!");
    }
    Serial.println("----------------\n");
}

bool initBMI270(uint8_t addr) {
    Serial.printf("Trying BMI270 at 0x%02X... ", addr);
    int8_t status = bmi.beginI2C(addr);
    if (status == BMI2_OK) {
        Serial.println("OK");
        return true;
    } else {
        Serial.printf("FAILED (code %d)\n", status);
        return false;
    }
}

void showErrorScreen(const char* title, const char* detail) {
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 120);
    tft.println(title);
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
    tft.setTextSize(1);
    tft.setCursor(10, 160);
    tft.println(detail);
    tft.setCursor(10, 180);
    tft.println("Running demo pattern");
    delay(2000);
}

// ===================== END OF FILE =====================
