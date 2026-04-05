/* shared.h — Shared data structures for VitalSignLogger
 * Include this in every task file.
 * Guide reference: Chapter 6.2
 */

#pragma once

#include <stdint.h>
#include "cmsis_os2.h"/* FreeRTOS CMSIS-RTOS2 types */
extern osSemaphoreId_t accelBufSemHandle;
extern osMutexId_t     i2cMutexHandle;    /* guards shared I2C1 bus (MAX30102 + SSD1306) */

/* ---------------------------------------------------------------
 * Motion class labels (must match Python training labels exactly)
 *   0 = Rest
 *   1 = Walking
 *   2 = Arm Raised
 * --------------------------------------------------------------- */
#define MOTION_REST        0u
#define MOTION_WALKING     1u
#define MOTION_ARM_RAISED  2u

/* ---------------------------------------------------------------
 * SensorFrame_t
 * One frame of data flowing through sensorQueue from
 * SensorTask → TxTask.  MLTask writes motion_class in-place
 * via a separate shared variable (accel_buf / accel_buf_ready).
 * --------------------------------------------------------------- */
typedef struct {
    uint8_t  bpm;            /* Heart rate, 0 if no finger detected   */
    uint32_t ir_raw;         /* Raw IR ADC value — used for finger detection */
    float    ax, ay, az;     /* Acceleration [g]                      */
    float    gx, gy, gz;     /* Angular rate [deg/s]                  */
    float    temp;           /* Board temperature [°C]                */
    uint8_t  motion_class;   /* 0=Rest  1=Walking  2=Arm raised       */
    uint32_t timestamp_ms;   /* HAL_GetTick() at time of sample       */
} SensorFrame_t;

/* ---------------------------------------------------------------
 * Shared accelerometer ring-buffer filled by SensorTask,
 * consumed by MLTask every 100 samples (1 second at 100 Hz).
 * Declared extern here; defined once in ml_task.c
 * --------------------------------------------------------------- */
/* Double buffer — SensorTask writes to [write_idx], MLTask reads from [read_idx] */
extern volatile float   accel_buf[2][100][3];
extern volatile uint8_t accel_write_idx;   /* which buffer SensorTask is filling */    /* Set to 1 by SensorTask         */
extern volatile uint8_t current_motion_class;/* Written by MLTask, read by SensorTask */

/* ---------------------------------------------------------------
 * FreeRTOS queue handle — defined in main.c (CubeMX generates it)
 * --------------------------------------------------------------- */
extern osMessageQueueId_t sensorQueueHandle;
