#pragma once
#include "stm32u5xx_hal.h"
#include <stdint.h>

#define MPU6050_ADDR  (0x68 << 1)

typedef struct {
    I2C_HandleTypeDef *hi2c;
    // Scaled outputs
    float ax, ay, az;   // g
    float gx, gy, gz;   // deg/s (bias-corrected)
    float temp_c;

    // Calibration offsets (computed once at startup)
    float bias_gx, bias_gy, bias_gz;
    float bias_ax, bias_ay;  // az bias not removed (gravity)

    uint8_t init_ok;
} MPU6050_t;

void MPU6050_Init(MPU6050_t *dev, I2C_HandleTypeDef *hi2c);
void MPU6050_Calibrate(MPU6050_t *dev, uint16_t n_samples);
void MPU6050_Read(MPU6050_t *dev);
