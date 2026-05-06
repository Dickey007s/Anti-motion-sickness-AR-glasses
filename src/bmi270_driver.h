#pragma once

#include <Arduino.h>
#include "bmi270_arduino_interface.h"

class BMI270Driver {
public:
    struct Vec3 {
        float x, y, z;
    };

    BMI270Driver(TwoWire& wire = Wire, uint8_t address = 0x68);

    bool begin(uint8_t sdaPin = 8, uint8_t sclPin = 9);
    bool calibrate(uint16_t samples = 200, uint16_t delayMs = 10);
    bool update();

    Vec3 getAccel() const;
    float getAccelX() const { return _accel.x; }
    float getAccelY() const { return _accel.y; }
    float getAccelZ() const { return _accel.z; }

    Vec3 getGyro() const;
    float getGyroX() const { return _gyro.x; }
    float getGyroY() const { return _gyro.y; }
    float getGyroZ() const { return _gyro.z; }

    void getRawAccel(int16_t& x, int16_t& y, int16_t& z) const;
    void getRawGyro(int16_t& x, int16_t& y, int16_t& z) const;

    bool isConnected() const;

private:
    TwoWire* _wire;
    uint8_t  _address;
    struct bmi2_dev _bmi;
    struct bmi2_sens_data _sensorData;

    Vec3 _accOffset;
    Vec3 _gyrOffset;
    Vec3 _accel;
    Vec3 _gyro;

    float lsbToMps2(int16_t val, float gRange, uint8_t bitWidth) const;
    float lsbToDps(int16_t val, float dpsRange, uint8_t bitWidth) const;

    bool setAccelConfig();
    bool setGyroConfig();
};
