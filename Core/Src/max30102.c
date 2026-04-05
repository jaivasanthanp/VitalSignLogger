#include "max30102.h"
#include <stdio.h>

static HAL_StatusTypeDef wr(MAX30102_t *d, uint8_t reg, uint8_t val) {
    return HAL_I2C_Mem_Write(d->hi2c, MAX30102_ADDR, reg,
                             I2C_MEMADD_SIZE_8BIT, &val, 1, 50);
}

static uint8_t rd(MAX30102_t *d, uint8_t reg) {
    uint8_t v = 0;
    HAL_I2C_Mem_Read(d->hi2c, MAX30102_ADDR, reg,
                     I2C_MEMADD_SIZE_8BIT, &v, 1, 50);
    return v;
}

void MAX30102_Init(MAX30102_t *dev, I2C_HandleTypeDef *hi2c) {
    dev->hi2c    = hi2c;
    dev->init_ok = 0;

    HAL_Delay(50);  /* power-on stabilisation */

    uint8_t id = rd(dev, 0xFF);
    if (id != 0x15) {
        printf("MAX30102: part ID 0x%02X (expected 0x15) — check wiring\r\n", id);
        return;
    }

    /* Soft-reset */
    wr(dev, 0x09, 0x40);
    HAL_Delay(10);

    /* The soft-reset may glitch SDA/SCL and leave the STM32 I2C peripheral
     * in an error state.  Re-init the peripheral so subsequent writes work. */
    HAL_I2C_DeInit(dev->hi2c);
    HAL_I2C_Init(dev->hi2c);

    /* Clear FIFO pointers */
    wr(dev, 0x04, 0x00);
    wr(dev, 0x05, 0x00);
    wr(dev, 0x06, 0x00);

    /* FIFO: no averaging (true 100 sps to match biquad fs=100 Hz) */
    wr(dev, 0x08, 0x1F);

    /* SpO2 config: ADC=4096nA | 100sps | 411µs pulse */
    wr(dev, 0x0A, 0x27);

    /* LED currents: 7.2 mA each */
    wr(dev, 0x0C, 0x24);  /* LED1_PA = Red (reg 0x0C) */
    wr(dev, 0x0D, 0x24);  /* LED2_PA = IR  (reg 0x0D) */

    /* Mode: SpO2 — set last after LED currents are configured */
    HAL_StatusTypeDef r = wr(dev, 0x09, 0x03);
    if (r != HAL_OK) {
        printf("MAX30102: mode write failed (HAL %d) — I2C error\r\n", r);
        return;
    }

    /* Verify the mode register was written */
    uint8_t mode = rd(dev, 0x09);
    if ((mode & 0x07) != 0x03) {
        printf("MAX30102: mode readback 0x%02X (expected 0x03) — init failed\r\n", mode);
        return;
    }

    dev->init_ok = 1;
    printf("MAX30102: OK (mode=0x%02X)\r\n", mode);
}

uint8_t MAX30102_ReadFIFO(MAX30102_t *dev) {
    uint8_t wr_ptr = rd(dev, 0x04);
    uint8_t rd_ptr = rd(dev, 0x06);

    uint8_t n = (wr_ptr - rd_ptr) & 0x1F;
    if (n == 0) return 0;

    uint8_t buf[6];
    HAL_I2C_Mem_Read(dev->hi2c, MAX30102_ADDR, 0x07,
                     I2C_MEMADD_SIZE_8BIT, buf, 6, 50);

    // 18-bit values, MSB first, upper 2 bits reserved
    dev->red_raw = ((uint32_t)(buf[0]&0x03)<<16) | ((uint32_t)buf[1]<<8) | buf[2];
    dev->ir_raw  = ((uint32_t)(buf[3]&0x03)<<16) | ((uint32_t)buf[4]<<8) | buf[5];

    return 1;
}
