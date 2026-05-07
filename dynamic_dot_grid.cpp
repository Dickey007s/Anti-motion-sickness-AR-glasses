/**
 * ============================================================================
 * Anti-Motion-Sickness AR Glasses - Dynamic Dot Grid Compensation Algorithm
 * ============================================================================
 * 
 * Hardware: ESP32-S3 Super Mini + BMI270 (I2C) + ST7789V3 1.47" TFT (SPI)
 * 
 * Features:
 * - BMI270 6-axis IMU data acquisition with calibration
 * - Low-pass filtering for noise reduction
 * - Complementary filter for attitude estimation
 * - World-frame longitudinal acceleration extraction
 * - Motion state detection (Accelerating / Braking / Cruising)
 * - Visual compensation offset calculation with smoothing
 * - Dynamic 4x5 dot grid rendering on ST7789V3 display
 * 
 * Algorithm Parameters:
 * - K = 20.0 px/g        (visual gain)
 * - ALPHA = 0.15         (smoothing coefficient)
 * - MAX_OFFSET = 12 px   (clamping limit)
 * - ACCEL_THRESHOLD = 0.1g (motion detection threshold)
 * - LPF_ALPHA = 0.3      (low-pass filter coefficient)
 * 
 * Frame Rate: 100 Hz (10ms period)
 * 
 * Generated: 2026-05-06
 * ============================================================================
 */

#include <Arduino.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <SparkFun_BMI270_Arduino_Library.h>

// ===================== PIN DEFINITIONS =====================
#define TFT_CS      10      // SPI Chip Select
#define TFT_DC      13      // Data/Command select
#define TFT_RST     8       // Reset
#define TFT_BL      9       // Backlight control

#define BMI_SDA     6       // I2C SDA
#define BMI_SCL     7       // I2C SCL
#define BMI_INT     1       // Interrupt (reserved)

// ===================== DISPLAY PARAMETERS =====================
#define SCREEN_W    172     // Display width
#define SCREEN_H    320     // Display height

// ===================== DOT GRID PARAMETERS =====================
#define GRID_ROWS       4       // Number of rows in dot grid
#define GRID_COLS       5       // Number of columns in dot grid
#define DOT_SIZE        4       // Size of each dot (pixels)
#define DOT_SPACING     8       // Spacing between dots (pixels)
#define GRID_OFFSET_X   66      // Grid start X (centered)
#define GRID_OFFSET_Y   136     // Grid start Y (centered)

// ===================== ALGORITHM PARAMETERS =====================
#define K               20.0f   // Visual gain: pixels per g
#define ALPHA           0.15f   // Smoothing coefficient (0-1)
#define MAX_OFFSET      12      // Maximum visual offset (pixels)
#define ACCEL_THRESHOLD 0.1f    // Motion detection threshold (g)
#define LPF_ALPHA       0.3f    // Low-pass filter coefficient

// ===================== TFT DRIVER =====================
Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);

// ===================== BMI270 DRIVER =====================
BMI270 bmi;
const uint8_t BMI_ADDR = BMI2_I2C_PRIM_ADDR;  // 0x68 (SDO grounded)

// ===================== MOTION STATE ENUM =====================
enum MotionState {
    CRUISING = 0,       // Constant velocity
    ACCELERATING = 1,   // Accelerating
    BRAKING = 2         // Decelerating
};

// ===================== GLOBAL VARIABLES =====================

// Sensor biases (calculated during setup)
float acc_bias[3] = {0.0f, 0.0f, 0.0f};
float gyro_bias[3] = {0.0f, 0.0f, 0.0f};

// Low-pass filter previous values
float acc_filtered_prev[3] = {0.0f, 0.0f, 0.0f};

// Attitude angles (radians)
float pitch = 0.0f;
float roll = 0.0f;

// Visual offset (pixels)
float offset = 0.0f;

// Current motion state
MotionState motionState = CRUISING;

// Timing
unsigned long lastTime = 0;

// Frame counter for debug output
static int frameCount = 0;

// ===================== FUNCTION DECLARATIONS =====================

// Sensor calibration
void calibrateSensors();

// Signal processing
void applyLowPassFilter(float acc_raw[3], float acc_filtered[3]);
void updateAttitude(float acc[3], float gyro[3], float dt);
float extractLongitudinalAcceleration(float acc[3]);

// Motion detection
MotionState detectMotionState(float a_x_world);

// Visual compensation
float calculateVisualOffset(float a_x_world);

// Display rendering
void drawDotGrid(float offset, MotionState state);
uint16_t getStateColor(MotionState state);

// Debug output
void printDebugInfo(float a_x_world, float visualOffset);

// ===================== SETUP =====================
void setup() {
    // Initialize serial for debugging
    Serial.begin(115200);
    while (!Serial) { delay(10); }
    delay(300);
    
    Serial.println("========================================");
    Serial.println("Anti-Motion-Sickness AR Glasses");
    Serial.println("Dynamic Dot Grid Compensation Algorithm");
    Serial.println("========================================");
    
    // ---------- SPI Initialization ----------
    SPI.begin(12, -1, 11, -1);  // SCK=12, MISO=-1, MOSI=11, SS=-1
    Serial.println("[OK] SPI initialized");
    
    // ---------- TFT Initialization ----------
    tft.init(SCREEN_W, SCREEN_H);
    tft.sendCommand(0x11);      // SLPOUT: Exit sleep mode
    delay(120);
    tft.sendCommand(0x29);      // DISPON: Turn on display
    delay(20);
    tft.setRotation(0);         // Portrait orientation
    
    // Turn on backlight
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, HIGH);
    
    // Show startup screen
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_WHITE);
    tft.setTextSize(2);
    tft.setCursor(20, 100);
    tft.println("Anti-Motion");
    tft.setCursor(20, 130);
    tft.println("Sickness");
    tft.setCursor(20, 160);
    tft.println("AR Glasses");
    delay(2000);
    
    Serial.println("[OK] TFT initialized");
    
    // ---------- I2C Initialization ----------
    Wire.begin(BMI_SDA, BMI_SCL);
    Serial.println("[OK] I2C initialized");
    
    // ---------- BMI270 Initialization ----------
    int8_t bmiStatus = bmi.beginI2C(BMI_ADDR);
    if (bmiStatus != BMI2_OK) {
        Serial.printf("[ERROR] BMI270 initialization failed: %d\n", bmiStatus);
        
        // Show error on screen
        tft.fillScreen(ST77XX_BLACK);
        tft.setTextColor(ST77XX_RED);
        tft.setTextSize(2);
        tft.setCursor(10, 140);
        tft.println("BMI270 Error");
        tft.setCursor(10, 170);
        tft.printf("Code: %d", bmiStatus);
        
        // Don't block - continue without BMI270
    } else {
        Serial.println("[OK] BMI270 connected");
        
        // Configure accelerometer: 100Hz, +/-4g
        bmi2_sens_config accConfig;
        accConfig.type = BMI2_ACCEL;
        accConfig.cfg.acc.odr = BMI2_ACC_ODR_100HZ;
        accConfig.cfg.acc.range = BMI2_ACC_RANGE_4G;
        accConfig.cfg.acc.bwp = BMI2_ACC_NORMAL_AVG4;
        accConfig.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;
        bmi.setConfig(accConfig);
        
        // Configure gyroscope: 100Hz
        bmi2_sens_config gyrConfig;
        gyrConfig.type = BMI2_GYRO;
        gyrConfig.cfg.gyr.odr = BMI2_GYR_ODR_100HZ;
        gyrConfig.cfg.gyr.bwp = BMI2_GYR_NORMAL_MODE;
        gyrConfig.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;
        bmi.setConfig(gyrConfig);
        
        // Enable both sensors
        uint8_t sensList[2] = {BMI2_ACCEL, BMI2_GYRO};
        bmi.enableFeatures(sensList, 2);
        
        Serial.println("[OK] BMI270 configured: Accel 100Hz/4g, Gyro 100Hz");
        
        // Calibrate sensors (2 seconds, 200 samples)
        calibrateSensors();
    }
    
    // Configure interrupt pin (reserved for future use)
    pinMode(BMI_INT, INPUT);
    
    // Initialize timing
    lastTime = millis();
    
    Serial.println("========================================");
    Serial.println("Setup complete. Entering main loop...");
    Serial.println("========================================");
}

// ===================== MAIN LOOP =====================
void loop() {
    // Calculate actual delta time
    unsigned long currentTime = millis();
    float dt = (currentTime - lastTime) / 1000.0f;
    lastTime = currentTime;
    
    // Ensure dt is reasonable (prevent jump after pause)
    if (dt > 0.05f) dt = 0.01f;
    
    // ===================== 1. SENSOR DATA ACQUISITION =====================
    bmi.getSensorData();
    
    // ===================== 2. BIAS COMPENSATION =====================
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
    
    // ===================== 3. LOW-PASS FILTERING =====================
    float acc_filtered[3];
    applyLowPassFilter(acc_raw, acc_filtered);
    
    // ===================== 4. ATTITUDE ESTIMATION =====================
    updateAttitude(acc_filtered, gyro_raw, dt);
    
    // ===================== 5. LONGITUDINAL ACCELERATION =====================
    float a_x_world = extractLongitudinalAcceleration(acc_filtered);
    
    // ===================== 6. MOTION STATE DETECTION =====================
    motionState = detectMotionState(a_x_world);
    
    // ===================== 7. VISUAL OFFSET CALCULATION =====================
    float visualOffset = calculateVisualOffset(a_x_world);
    
    // ===================== 8. RENDER DOT GRID =====================
    drawDotGrid(visualOffset, motionState);
    
    // ===================== 9. DEBUG OUTPUT =====================
    if (++frameCount % 10 == 0) {
        printDebugInfo(a_x_world, visualOffset);
    }
    
    // ===================== 10. FRAME RATE CONTROL =====================
    // Non-blocking delay to maintain 100Hz
    static unsigned long nextFrame = 0;
    unsigned long now = millis();
    if (now < nextFrame) {
        delay(nextFrame - now);
    }
    nextFrame = now + 10;  // 100Hz = 10ms period
}

// ===================== SENSOR CALIBRATION =====================
/**
 * @brief Calibrate sensor biases by averaging 200 samples over 2 seconds
 * 
 * Requirements:
 * - Device must be stationary during calibration
 * - Takes 2 seconds to complete
 * - Updates global acc_bias and gyro_bias arrays
 */
void calibrateSensors() {
    Serial.println("----------------------------------------");
    Serial.println("Starting sensor calibration...");
    Serial.println("Please keep the device stationary!");
    Serial.println("----------------------------------------");
    
    // Show calibration screen
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(2);
    tft.setCursor(30, 130);
    tft.println("Calibrating...");
    tft.setCursor(30, 160);
    tft.println("Keep Still");
    
    float acc_sum[3] = {0.0f, 0.0f, 0.0f};
    float gyro_sum[3] = {0.0f, 0.0f, 0.0f};
    const int samples = 200;  // 200 samples at 100Hz = 2 seconds
    
    for (int i = 0; i < samples; i++) {
        bmi.getSensorData();
        
        acc_sum[0] += bmi.data.accelX;
        acc_sum[1] += bmi.data.accelY;
        acc_sum[2] += bmi.data.accelZ;
        
        gyro_sum[0] += bmi.data.gyroX;
        gyro_sum[1] += bmi.data.gyroY;
        gyro_sum[2] += bmi.data.gyroZ;
        
        // Progress bar
        if (i % 20 == 0) {
            int progress = (i * 100) / samples;
            tft.fillRect(20, 200, 132, 10, ST77XX_BLACK);
            tft.fillRect(20, 200, (132 * progress) / 100, 10, ST77XX_GREEN);
        }
        
        delay(10);  // 100Hz sampling
    }
    
    // Calculate averages
    for (int i = 0; i < 3; i++) {
        acc_bias[i] = acc_sum[i] / samples;
        gyro_bias[i] = gyro_sum[i] / samples;
    }
    
    // Reset filter history
    for (int i = 0; i < 3; i++) {
        acc_filtered_prev[i] = 0.0f;
    }
    
    Serial.println("Calibration complete!");
    Serial.printf("ACC Bias: [%.4f, %.4f, %.4f] g\n", 
                  acc_bias[0], acc_bias[1], acc_bias[2]);
    Serial.printf("GYRO Bias: [%.4f, %.4f, %.4f] deg/s\n", 
                  gyro_bias[0], gyro_bias[1], gyro_bias[2]);
    Serial.println("----------------------------------------");
}

// ===================== LOW-PASS FILTER =====================
/**
 * @brief Apply first-order low-pass filter to accelerometer data
 * 
 * Formula: y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
 * 
 * @param acc_raw Raw accelerometer data (after bias removal)
 * @param acc_filtered Filtered output
 */
void applyLowPassFilter(float acc_raw[3], float acc_filtered[3]) {
    for (int i = 0; i < 3; i++) {
        acc_filtered[i] = LPF_ALPHA * acc_raw[i] + 
                          (1.0f - LPF_ALPHA) * acc_filtered_prev[i];
        acc_filtered_prev[i] = acc_filtered[i];
    }
}

// ===================== COMPLEMENTARY FILTER =====================
/**
 * @brief Update attitude estimation using complementary filter
 * 
 * Combines gyroscope integration (fast response) with accelerometer
 * (stable long-term) for robust pitch/roll estimation.
 * 
 * Formula:
 *   pitch = 0.98 * (pitch + gyro_y * dt) + 0.02 * atan2(-acc_x, sqrt(acc_y^2 + acc_z^2))
 *   roll  = 0.98 * (roll  + gyro_x * dt) + 0.02 * atan2(acc_y, acc_z)
 * 
 * @param acc Filtered accelerometer data (g)
 * @param gyro Bias-compensated gyroscope data (deg/s)
 * @param dt Time step (seconds)
 */
void updateAttitude(float acc[3], float gyro[3], float dt) {
    // Convert gyroscope from deg/s to rad/s
    float gyro_rad[3] = {
        gyro[0] * DEG_TO_RAD,
        gyro[1] * DEG_TO_RAD,
        gyro[2] * DEG_TO_RAD
    };
    
    // Calculate attitude from accelerometer (gravity reference)
    // atan2(-acc_x, sqrt(acc_y^2 + acc_z^2)) gives pitch angle
    float acc_pitch = atan2f(-acc[0], sqrtf(acc[1] * acc[1] + acc[2] * acc[2]));
    
    // atan2(acc_y, acc_z) gives roll angle
    float acc_roll  = atan2f(acc[1], acc[2]);
    
    // Complementary filter: 98% trust gyroscope (short-term), 2% trust accelerometer (long-term)
    pitch = 0.98f * (pitch + gyro_rad[1] * dt) + 0.02f * acc_pitch;
    roll  = 0.98f * (roll  + gyro_rad[0] * dt) + 0.02f * acc_roll;
}

// ===================== LONGITUDINAL ACCELERATION =====================
/**
 * @brief Extract longitudinal (forward/backward) acceleration in world frame
 * 
 * The sensor is mounted with X-axis pointing forward. We need to project
 * the acceleration onto the world X-axis using the estimated pitch angle.
 * 
 * Formula: a_x_world = acc_x * cos(pitch) + acc_z * sin(pitch)
 * 
 * @param acc Filtered accelerometer data (g)
 * @return Longitudinal acceleration in world frame (g)
 */
float extractLongitudinalAcceleration(float acc[3]) {
    return acc[0] * cosf(pitch) + acc[2] * sinf(pitch);
}

// ===================== MOTION STATE DETECTION =====================
/**
 * @brief Detect current motion state based on longitudinal acceleration
 * 
 * Thresholds:
 *   a_x_world >  0.1g  -> ACCELERATING
 *   a_x_world < -0.1g  -> BRAKING
 *   otherwise          -> CRUISING
 * 
 * @param a_x_world Longitudinal acceleration (g)
 * @return Detected motion state
 */
MotionState detectMotionState(float a_x_world) {
    if (a_x_world > ACCEL_THRESHOLD) {
        return ACCELERATING;
    } else if (a_x_world < -ACCEL_THRESHOLD) {
        return BRAKING;
    } else {
        return CRUISING;
    }
}

// ===================== VISUAL OFFSET CALCULATION =====================
/**
 * @brief Calculate visual compensation offset with smoothing and clamping
 * 
 * Algorithm:
 *   1. target_offset = -K * a_x_world (negative = opposite direction)
 *   2. offset = offset + alpha * (target_offset - offset) (smoothing)
 *   3. offset = clamp(offset, -MAX_OFFSET, MAX_OFFSET) (safety)
 * 
 * @param a_x_world Longitudinal acceleration (g)
 * @return Smoothed and clamped visual offset (pixels)
 */
float calculateVisualOffset(float a_x_world) {
    // Calculate target offset (opposite to acceleration direction)
    float target_offset = -K * a_x_world;
    
    // Apply first-order smoothing
    offset = offset + ALPHA * (target_offset - offset);
    
    // Clamp to safety limits
    if (offset > MAX_OFFSET) {
        offset = MAX_OFFSET;
    } else if (offset < -MAX_OFFSET) {
        offset = -MAX_OFFSET;
    }
    
    return offset;
}

// ===================== COLOR SELECTION =====================
/**
 * @brief Get display color based on motion state
 * 
 * Color coding:
 *   ACCELERATING -> RED (warning: speeding up)
 *   BRAKING      -> GREEN (safe: slowing down)
 *   CRUISING     -> WHITE (neutral)
 * 
 * @param state Current motion state
 * @return RGB565 color value
 */
uint16_t getStateColor(MotionState state) {
    switch (state) {
        case ACCELERATING:
            return ST77XX_RED;      // 0xF800
        case BRAKING:
            return ST77XX_GREEN;    // 0x07E0
        case CRUISING:
        default:
            return ST77XX_WHITE;    // 0xFFFF
    }
}

// ===================== DOT GRID RENDERING =====================
/**
 * @brief Render dynamic dot grid on TFT display
 * 
 * Grid layout: 4 rows x 5 columns
 * - Each dot is DOT_SIZE x DOT_SIZE pixels
 * - Spaced by DOT_SPACING pixels
 * - Entire grid shifts horizontally by 'offset' pixels
 * - Color changes based on motion state
 * 
 * @param offset Horizontal offset (pixels, can be negative)
 * @param state Current motion state (determines color)
 */
void drawDotGrid(float offset, MotionState state) {
    uint16_t color = getStateColor(state);
    
    // Clear screen with black background
    tft.fillScreen(ST77XX_BLACK);
    
    // Draw 4x5 dot grid
    for (int row = 0; row < GRID_ROWS; row++) {
        for (int col = 0; col < GRID_COLS; col++) {
            // Calculate dot position with offset
            int16_t x = GRID_OFFSET_X + col * DOT_SPACING + (int16_t)offset;
            int16_t y = GRID_OFFSET_Y + row * DOT_SPACING;
            
            // Boundary check (skip dots outside screen)
            if (x < 0 || x + DOT_SIZE > SCREEN_W) continue;
            if (y < 0 || y + DOT_SIZE > SCREEN_H) continue;
            
            // Draw filled rectangle as dot
            tft.fillRect(x, y, DOT_SIZE, DOT_SIZE, color);
        }
    }
    
    // Optional: Draw status indicator at top
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    switch (state) {
        case ACCELERATING:
            tft.setTextColor(ST77XX_RED);
            tft.print("ACCEL");
            break;
        case BRAKING:
            tft.setTextColor(ST77XX_GREEN);
            tft.print("BRAKE");
            break;
        case CRUISING:
            tft.setTextColor(ST77XX_WHITE);
            tft.print("CRUISE");
            break;
    }
    
    // Optional: Draw offset value
    tft.setCursor(5, 20);
    tft.setTextColor(ST77XX_WHITE);
    tft.printf("Offset: %.1f", offset);
}

// ===================== DEBUG OUTPUT =====================
/**
 * @brief Print debug information to serial port
 * 
 * Output format:
 *   Frame=N, State=S, a_x=XX.XXXg, Offset=XX.Xpx, Pitch=XX.XXrad
 * 
 * @param a_x_world Longitudinal acceleration
 * @param visualOffset Current visual offset
 */
void printDebugInfo(float a_x_world, float visualOffset) {
    const char* stateStr;
    switch (motionState) {
        case ACCELERATING: stateStr = "ACCEL"; break;
        case BRAKING:      stateStr = "BRAKE"; break;
        case CRUISING:     stateStr = "CRUISE"; break;
        default:           stateStr = "UNKNOWN";
    }
    
    Serial.printf("Frame=%d, State=%s, a_x=%+.3fg, Offset=%+6.1fpx, Pitch=%+.3frad\n",
                  frameCount, stateStr, a_x_world, visualOffset, pitch);
}

// ===================== END OF FILE =====================
