#pragma once
#include "stm32u5xx_hal.h"
#include <stdint.h>

#define MAX30102_ADDR    (0x57 << 1)

typedef struct {
    I2C_HandleTypeDef *hi2c;
    uint32_t red_raw;
    uint32_t ir_raw;
    uint8_t  init_ok;
} MAX30102_t;

void    MAX30102_Init(MAX30102_t *dev, I2C_HandleTypeDef *hi2c);
uint8_t MAX30102_ReadFIFO(MAX30102_t *dev);  // returns samples read
