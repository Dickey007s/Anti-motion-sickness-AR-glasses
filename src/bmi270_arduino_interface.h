#pragma once

#include <stdint.h>
#include <Wire.h>

#ifdef __cplusplus
extern "C" {
#endif

// 必须 extern "C" 包裹 Bosch C 库头文件
#include "bmi2.h"
#include "bmi270.h"

// 为 Bosch API 提供的 I2C 读/写/延时函数
BMI2_INTF_RETURN_TYPE bmi2_i2c_read(uint8_t reg_addr, uint8_t* reg_data,
                                    uint32_t len, void* intf_ptr);
BMI2_INTF_RETURN_TYPE bmi2_i2c_write(uint8_t reg_addr, const uint8_t* reg_data,
                                     uint32_t len, void* intf_ptr);
void                  bmi2_delay_us(uint32_t period, void* intf_ptr);

// Arduino 版接口初始化：绑定 Wire 实例和 I2C 地址
int8_t  bmi2_interface_init(struct bmi2_dev* bmi, uint8_t intf,
                            TwoWire* wire, uint8_t address);

// 错误码打印（映射到 Serial）
void bmi2_error_codes_print_result(int8_t rslt);

#ifdef __cplusplus
}
#endif
