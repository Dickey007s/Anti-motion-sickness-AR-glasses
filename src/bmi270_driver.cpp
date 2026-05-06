#include "bmi270_driver.h"
#include <math.h>

#define GRAVITY_EARTH 9.80665f

BMI270Driver::BMI270Driver(TwoWire& wire, uint8_t address)
    : _wire(&wire), _address(address)
{
    memset(&_bmi, 0, sizeof(_bmi));
    memset(&_sensorData, 0, sizeof(_sensorData));
    _accOffset = {0, 0, 0};
    _gyrOffset = {0, 0, 0};
    _accel     = {0, 0, 0};
    _gyro      = {0, 0, 0};
}

bool BMI270Driver::begin(uint8_t sdaPin, uint8_t sclPin)
{
    _wire->setPins(sdaPin, sclPin);
    _wire->begin();
    _wire->setClock(400000);

    int8_t rslt = bmi2_interface_init(&_bmi, BMI2_I2C_INTF, _wire, _address);
    bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) {
        Serial.println("[BMI270] Interface init failed");
        return false;
    }

    rslt = bmi270_init(&_bmi);
    bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) {
        Serial.println("[BMI270] Chip init failed (config file load error?)");
        return false;
    }

    if (!setAccelConfig()) {
        Serial.println("[BMI270] Accel config failed");
        return false;
    }
    if (!setGyroConfig()) {
        Serial.println("[BMI270] Gyro config failed");
        return false;
    }

    uint8_t sensor_list[2] = {BMI2_ACCEL, BMI2_GYRO};
    rslt = bmi2_sensor_enable(sensor_list, 2, &_bmi);
    bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) {
        Serial.println("[BMI270] Sensor enable failed");
        return false;
    }

    Serial.println("[BMI270] Init OK");
    return true;
}

bool BMI270Driver::calibrate(uint16_t samples, uint16_t delayMs)
{
    Serial.printf("[BMI270] Calibrating... keep stationary (%d samples)\n", samples);

    Vec3 accSum = {0, 0, 0};
    Vec3 gyrSum = {0, 0, 0};
    uint16_t valid = 0;

    for (uint16_t i = 0; i < samples; i++) {
        if (update()) {
            accSum.x += _sensorData.acc.x;
            accSum.y += _sensorData.acc.y;
            accSum.z += _sensorData.acc.z;
            gyrSum.x += _sensorData.gyr.x;
            gyrSum.y += _sensorData.gyr.y;
            gyrSum.z += _sensorData.gyr.z;
            valid++;
        }
        delay(delayMs);
    }

    if (valid == 0) {
        Serial.println("[BMI270] Calibration failed: no valid samples");
        return false;
    }

    // 加速度计零偏：标定后静止时理论上 Z 轴为 1g，这里直接用采样均值作为零偏
    // 若传感器水平安装，后续可在算法层扣除重力分量
    _accOffset.x = accSum.x / valid;
    _accOffset.y = accSum.y / valid;
    _accOffset.z = accSum.z / valid;

    _gyrOffset.x = gyrSum.x / valid;
    _gyrOffset.y = gyrSum.y / valid;
    _gyrOffset.z = gyrSum.z / valid;

    Serial.printf("[BMI270] Calibrated: acc_off=(%.0f,%.0f,%.0f) gyr_off=(%.0f,%.0f,%.0f)\n",
                  _accOffset.x, _accOffset.y, _accOffset.z,
                  _gyrOffset.x, _gyrOffset.y, _gyrOffset.z);
    return true;
}

bool BMI270Driver::update()
{
    int8_t rslt = bmi2_get_sensor_data(&_sensorData, &_bmi);
    bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) return false;

    // 数据就绪检查
    if (!(_sensorData.status & BMI2_DRDY_ACC) || !(_sensorData.status & BMI2_DRDY_GYR)) {
        return false;
    }

    int16_t acc_x = _sensorData.acc.x - (int16_t)_accOffset.x;
    int16_t acc_y = _sensorData.acc.y - (int16_t)_accOffset.y;
    int16_t acc_z = _sensorData.acc.z - (int16_t)_accOffset.z;

    int16_t gyr_x = _sensorData.gyr.x - (int16_t)_gyrOffset.x;
    int16_t gyr_y = _sensorData.gyr.y - (int16_t)_gyrOffset.y;
    int16_t gyr_z = _sensorData.gyr.z - (int16_t)_gyrOffset.z;

    // 转换为物理单位
    // 量程：accel ±4g, gyro ±500°/s
    _accel.x = lsbToMps2(acc_x, 4.0f, _bmi.resolution);
    _accel.y = lsbToMps2(acc_y, 4.0f, _bmi.resolution);
    _accel.z = lsbToMps2(acc_z, 4.0f, _bmi.resolution);

    _gyro.x = lsbToDps(gyr_x, 500.0f, _bmi.resolution);
    _gyro.y = lsbToDps(gyr_y, 500.0f, _bmi.resolution);
    _gyro.z = lsbToDps(gyr_z, 500.0f, _bmi.resolution);

    return true;
}

BMI270Driver::Vec3 BMI270Driver::getAccel() const { return _accel; }
BMI270Driver::Vec3 BMI270Driver::getGyro()  const { return _gyro; }

void BMI270Driver::getRawAccel(int16_t& x, int16_t& y, int16_t& z) const
{
    x = _sensorData.acc.x;
    y = _sensorData.acc.y;
    z = _sensorData.acc.z;
}

void BMI270Driver::getRawGyro(int16_t& x, int16_t& y, int16_t& z) const
{
    x = _sensorData.gyr.x;
    y = _sensorData.gyr.y;
    z = _sensorData.gyr.z;
}

bool BMI270Driver::isConnected() const
{
    _wire->beginTransmission(_address);
    return (_wire->endTransmission() == 0);
}

float BMI270Driver::lsbToMps2(int16_t val, float gRange, uint8_t bitWidth) const
{
    float halfScale = (float)((1UL << bitWidth) / 2.0f);
    return (GRAVITY_EARTH * val * gRange) / halfScale;
}

float BMI270Driver::lsbToDps(int16_t val, float dpsRange, uint8_t bitWidth) const
{
    float halfScale = (float)((1UL << bitWidth) / 2.0f);
    return (dpsRange / halfScale) * val;
}

bool BMI270Driver::setAccelConfig()
{
    struct bmi2_sens_config config;
    config.type = BMI2_ACCEL;

    int8_t rslt = bmi2_get_sensor_config(&config, 1, &_bmi);
    bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) return false;

    config.cfg.acc.odr         = BMI2_ACC_ODR_100HZ;
    config.cfg.acc.range       = BMI2_ACC_RANGE_4G;
    config.cfg.acc.bwp         = BMI2_ACC_NORMAL_AVG4;
    config.cfg.acc.filter_perf = BMI2_PERF_OPT_MODE;

    rslt = bmi2_set_sensor_config(&config, 1, &_bmi);
    bmi2_error_codes_print_result(rslt);
    return (rslt == BMI2_OK);
}

bool BMI270Driver::setGyroConfig()
{
    struct bmi2_sens_config config;
    config.type = BMI2_GYRO;

    int8_t rslt = bmi2_get_sensor_config(&config, 1, &_bmi);
    bmi2_error_codes_print_result(rslt);
    if (rslt != BMI2_OK) return false;

    config.cfg.gyr.odr         = BMI2_GYR_ODR_100HZ;
    config.cfg.gyr.range       = BMI2_GYR_RANGE_500;
    config.cfg.gyr.bwp         = BMI2_GYR_NORMAL_MODE;
    config.cfg.gyr.noise_perf  = BMI2_POWER_OPT_MODE;
    config.cfg.gyr.filter_perf = BMI2_PERF_OPT_MODE;

    rslt = bmi2_set_sensor_config(&config, 1, &_bmi);
    bmi2_error_codes_print_result(rslt);
    return (rslt == BMI2_OK);
}
