/* sensor_task.h — SensorTask & TxTask for VitalSignLogger
 * Guide reference: Chapter 6.2
 */

#pragma once

#include "max30102.h"
#include "mpu6050.h"

/* ppg_dev is defined in sensor_task.c; initialised from main() before RTOS start */
extern MAX30102_t ppg_dev;

/* imu_dev is defined in sensor_task.c; initialised from main() before RTOS start */
extern MPU6050_t imu_dev;

/* SensorTask: 100 Hz acquisition, filtering, peak detection
 * Register in CubeMX: Priority=osPriorityAboveNormal  Stack=512 words */
void SensorTask(void *argument);

/* TxTask: CSV → UART + SSD1306 display update
 * Register in CubeMX: Priority=osPriorityNormal  Stack=256 words */
void TxTask(void *argument);
