#include "mpu6050.h"
#include <string.h>

static void wr(MPU6050_t *d, uint8_t reg, uint8_t val) {
    HAL_I2C_Mem_Write(d->hi2c, MPU6050_ADDR, reg,
                      I2C_MEMADD_SIZE_8BIT, &val, 1, 50);
}

void MPU6050_Init(MPU6050_t *dev, I2C_HandleTypeDef *hi2c) {
    dev->hi2c = hi2c;
    memset(&dev->bias_gx, 0, sizeof(float)*5);
    dev->init_ok = 0;

    uint8_t who;
    HAL_I2C_Mem_Read(dev->hi2c, MPU6050_ADDR, 0x75,
                     I2C_MEMADD_SIZE_8BIT, &who, 1, 50);

    if (who != 0x68) return;

    wr(dev, 0x6B, 0x00);  // Wake up
    wr(dev, 0x1B, 0x00);  // Gyro FS = ±250 deg/s
    wr(dev, 0x1C, 0x00);  // Accel FS = ±2 g
    wr(dev, 0x1A, 0x03);  // DLPF ~44 Hz
    wr(dev, 0x19, 0x07);  // Sample rate 1 kHz

    dev->init_ok = 1;
}

void MPU6050_Calibrate(MPU6050_t *dev, uint16_t n) {
    double sgx=0,sgy=0,sgz=0,sax=0,say=0;

    for (uint16_t i=0; i<n; i++) {
        MPU6050_Read(dev);
        sgx += dev->gx; sgy += dev->gy; sgz += dev->gz;
        sax += dev->ax; say += dev->ay;
        HAL_Delay(2);
    }

    dev->bias_gx = (float)(sgx/n);
    dev->bias_gy = (float)(sgy/n);
    dev->bias_gz = (float)(sgz/n);
    dev->bias_ax = (float)(sax/n);
    dev->bias_ay = (float)(say/n);
}

void MPU6050_Read(MPU6050_t *dev) {
    uint8_t buf[14];
    HAL_StatusTypeDef st = HAL_I2C_Mem_Read(dev->hi2c, MPU6050_ADDR, 0x3B,
                     I2C_MEMADD_SIZE_8BIT, buf, 14, 50);
    if (st != HAL_OK) {
        /* I2C error — recover the peripheral so next call can succeed */
        HAL_I2C_DeInit(dev->hi2c);
        HAL_I2C_Init(dev->hi2c);
        return;
    }

    int16_t axr = (int16_t)(buf[0]<<8|buf[1]);
    int16_t ayr = (int16_t)(buf[2]<<8|buf[3]);
    int16_t azr = (int16_t)(buf[4]<<8|buf[5]);
    int16_t tr  = (int16_t)(buf[6]<<8|buf[7]);
    int16_t gxr = (int16_t)(buf[8]<<8|buf[9]);
    int16_t gyr = (int16_t)(buf[10]<<8|buf[11]);
    int16_t gzr = (int16_t)(buf[12]<<8|buf[13]);

    // Scale factors: accel FS=±2g → 16384 LSB/g
    //                gyro  FS=±250°/s → 131 LSB/°/s
    dev->ax = axr/16384.0f - dev->bias_ax;
    dev->ay = ayr/16384.0f - dev->bias_ay;
    dev->az = azr/16384.0f;  // gravity intentionally kept

    dev->gx = gxr/131.0f    - dev->bias_gx;
    dev->gy = gyr/131.0f    - dev->bias_gy;
    dev->gz = gzr/131.0f    - dev->bias_gz;

    dev->temp_c = tr/340.0f + 36.53f;
}
