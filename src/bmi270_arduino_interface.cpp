#include "bmi270_arduino_interface.h"
#include <Arduino.h>

static TwoWire* s_wire    = nullptr;
static uint8_t  s_address = 0;

BMI2_INTF_RETURN_TYPE bmi2_i2c_read(uint8_t reg_addr, uint8_t* reg_data,
                                    uint32_t len, void* intf_ptr)
{
    if (!s_wire) return BMI2_E_COM_FAIL;

    s_wire->beginTransmission(s_address);
    s_wire->write(reg_addr);
    if (s_wire->endTransmission(false) != 0) {
        return BMI2_E_COM_FAIL;
    }

    uint32_t rx = s_wire->requestFrom((int)s_address, (int)len);
    if (rx != len) {
        return BMI2_E_COM_FAIL;
    }

    for (uint32_t i = 0; i < len; i++) {
        reg_data[i] = s_wire->read();
    }
    return BMI2_OK;
}

BMI2_INTF_RETURN_TYPE bmi2_i2c_write(uint8_t reg_addr, const uint8_t* reg_data,
                                     uint32_t len, void* intf_ptr)
{
    if (!s_wire) return BMI2_E_COM_FAIL;

    s_wire->beginTransmission(s_address);
    s_wire->write(reg_addr);
    for (uint32_t i = 0; i < len; i++) {
        s_wire->write(reg_data[i]);
    }
    if (s_wire->endTransmission(true) != 0) {
        return BMI2_E_COM_FAIL;
    }
    return BMI2_OK;
}

void bmi2_delay_us(uint32_t period, void* intf_ptr)
{
    delayMicroseconds(period);
}

int8_t bmi2_interface_init(struct bmi2_dev* bmi, uint8_t intf,
                           TwoWire* wire, uint8_t address)
{
    if (!bmi || !wire) return BMI2_E_NULL_PTR;
    if (intf != BMI2_I2C_INTF) return BMI2_E_COM_FAIL;

    s_wire    = wire;
    s_address = address;

    bmi->intf     = BMI2_I2C_INTF;
    bmi->read     = bmi2_i2c_read;
    bmi->write    = bmi2_i2c_write;
    bmi->delay_us = bmi2_delay_us;
    bmi->intf_ptr = nullptr;
    bmi->chip_id  = BMI270_CHIP_ID;

    return BMI2_OK;
}

void bmi2_error_codes_print_result(int8_t rslt)
{
    if (rslt != BMI2_OK) {
        Serial.printf("[BMI270] Error code: %d\n", rslt);
    }
}
