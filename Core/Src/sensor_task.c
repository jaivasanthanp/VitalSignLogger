/* sensor_task.c — SensorTask & TxTask for VitalSignLogger
 *
 * SensorTask: reads MAX30102 at 100 Hz, filters PPG, detects BPM peaks,
 *             fills accel_buf for MLTask, pushes SensorFrame to queue.
 *
 * TxTask:     pops SensorFrame, formats CSV → UART, updates SSD1306
 *             every 200 frames (~2 s).
 *
 * I2C bus (I2C1) is shared between MAX30102, MPU6050, and SSD1306,
 * protected by i2cMutexHandle (priority-inheritance mutex, app_freertos.c).
 */

#include "sensor_task.h"
#include "shared.h"
#include "usb_tx.h"
#include "biquad.h"
#include "peak_detect.h"
#include "max30102.h"
#include "mpu6050.h"
#include "ssd1306.h"
#include "cmsis_os.h"
#include "stm32u5xx_hal.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

extern I2C_HandleTypeDef hi2c1;

#define FINGER_THRESH  3000U   /* IR counts below this = no finger */

/* ---------------------------------------------------------------
 * Module-level instances
 * --------------------------------------------------------------- */
static Biquad_t    hpf;
static Biquad_t    lpf;
static PeakDetect_t pd;
       MAX30102_t  ppg_dev;   /* non-static: initialised in main() before RTOS starts */
       MPU6050_t   imu_dev;   /* non-static: initialised in main() before RTOS starts */
static uint8_t     accel_idx = 0;

/* ---------------------------------------------------------------
 * SensorTask
 * Priority: ABOVE_NORMAL   Stack: 512 words
 * --------------------------------------------------------------- */
void SensorTask(void *argument)
{
    (void)argument;

    Biquad_Init(&hpf, BQ_HIGHPASS, 0.5f, 0.707f, 100.0f);
    Biquad_Init(&lpf, BQ_LOWPASS,  4.0f, 0.707f, 100.0f);
    PD_Init(&pd);
    /* ppg_dev already initialised by main() in bare-metal context before RTOS start */

    SensorFrame_t frame;
    memset(&frame, 0, sizeof(frame));

    uint32_t wake_tick = osKernelGetTickCount();

    for (;;)
    {
        wake_tick += 10;   /* 100 Hz deadline */

        /* ── READ SENSORS ──────────────────────────────────────
         * Non-blocking mutex (5 ms timeout): if SSD1306_Flush is
         * running (~103 ms), use the cached values from last read.
         * The MAX30102 FIFO buffers up to 32 samples, so no data
         * is lost — we drain the backlog on the next successful read.
         * Both MAX30102 and MPU6050 share I2C1, read in one lock.
         * ─────────────────────────────────────────────────────── */
        if (osMutexAcquire(i2cMutexHandle, 5) == osOK) {
            if (ppg_dev.init_ok)  MAX30102_ReadFIFO(&ppg_dev);
            if (imu_dev.init_ok)  MPU6050_Read(&imu_dev);
            osMutexRelease(i2cMutexHandle);
        }

        if (!ppg_dev.init_ok) {
            static uint32_t stk = 0; stk++;
            ppg_dev.ir_raw = (uint32_t)(100000.0f + 3000.0f * sinf(stk * 0.0628f));
        }

        /* ── SIGNAL PROCESSING ──────────────────────────────── */
        float hp    = Biquad_Process(&hpf, (float)ppg_dev.ir_raw);
        float clean = Biquad_Process(&lpf, hp);
        PD_Update(&pd, clean);

        /* ── FILL ACCEL BUFFER for MLTask ───────────────────── */
        accel_buf[accel_write_idx][accel_idx][0] = imu_dev.ax;
        accel_buf[accel_write_idx][accel_idx][1] = imu_dev.ay;
        accel_buf[accel_write_idx][accel_idx][2] = imu_dev.az;

        if (++accel_idx >= 100) {
            accel_idx       = 0;
            accel_write_idx = 1 - accel_write_idx;
            osSemaphoreRelease(accelBufSemHandle);
        }

        /* ── FINGER DETECTION ────────────────────────────────────
         * IR below threshold means no contact — reset peak detector
         * so stale state doesn't carry over. */
        if (ppg_dev.ir_raw < FINGER_THRESH) {
            PD_Init(&pd);
        }

        /* ── BUILD FRAME ──────────────────────────────────────── */
        frame.bpm          = (ppg_dev.ir_raw >= FINGER_THRESH) ? pd.bpm : 0;
        frame.ir_raw       = ppg_dev.ir_raw;
        frame.ax           = imu_dev.ax;
        frame.ay           = imu_dev.ay;
        frame.az           = imu_dev.az;
        frame.gx           = imu_dev.gx;
        frame.gy           = imu_dev.gy;
        frame.gz           = imu_dev.gz;
        frame.temp         = imu_dev.temp_c;
        frame.motion_class = current_motion_class;
        frame.timestamp_ms = HAL_GetTick();

        osMessageQueuePut(sensorQueueHandle, &frame, 0, 0);
        osDelayUntil(wake_tick);
    }
}

/* ---------------------------------------------------------------
 * Helper: fixed-width motion label for the OLED line
 * --------------------------------------------------------------- */
static const char *motion_label(uint8_t cls)
{
    switch (cls) {
        case MOTION_REST:       return "REST    ";
        case MOTION_WALKING:    return "WALKING ";
        case MOTION_ARM_RAISED: return "ARM UP  ";
        default:                return "---     ";
    }
}

/* ---------------------------------------------------------------
 * TxTask
 * Priority: NORMAL   Stack: 256 words
 *
 * CSV format:
 *   HR,<bpm>,<ax>,<ay>,<az>,<gx>,<gy>,<gz>,<temp>,<motion>,<ts_ms>\r\n
 * --------------------------------------------------------------- */
void TxTask(void *argument)
{
    (void)argument;

    /* SSD1306 already initialised by main(); splash already shown */

    SensorFrame_t frame;
    char buf[160];
    char row[22];
    uint32_t frame_count = 0;

    for (;;)
    {
        osMessageQueueGet(sensorQueueHandle, &frame, NULL, osWaitForever);

        /* ── CSV → UART ──────────────────────────────────────── */
        int n = snprintf(buf, sizeof(buf),
            "HR,%d,%.3f,%.3f,%.3f,%.1f,%.1f,%.1f,%.1f,%d,%lu\r\n",
            (int)frame.bpm,
            frame.ax, frame.ay, frame.az,
            frame.gx, frame.gy, frame.gz,
            frame.temp,
            (int)frame.motion_class,
            (unsigned long)frame.timestamp_ms);
        if (n > 0) USBTX_Send(buf, (uint16_t)n);

        /* ── DISPLAY UPDATE every 50 frames (~0.5 s) ─────────
         * Build framebuffer (no I2C), then flush (holds I2C ~103 ms).
         * ─────────────────────────────────────────────────────── */
        if (++frame_count >= 50) {
            frame_count = 0;

            SSD1306_Clear();
            SSD1306_DrawString(0, 0, "VITAL SIGN LOGGER");
            SSD1306_DrawString(0, 1, "-----------------");

            if (frame.ir_raw >= FINGER_THRESH) {
                if (frame.bpm > 0)
                    snprintf(row, sizeof(row), "HR:  %3d bpm", (int)frame.bpm);
                else
                    snprintf(row, sizeof(row), "HR:  --- bpm");
                SSD1306_DrawString(0, 2, row);
            } else {
                SSD1306_DrawString(0, 2, "HR:  --- bpm");
            }

            snprintf(row, sizeof(row), "MOT: %s", motion_label(frame.motion_class));
            SSD1306_DrawString(0, 3, row);

            snprintf(row, sizeof(row), "IR: %6lu", (unsigned long)frame.ir_raw);
            SSD1306_DrawString(0, 4, row);

            if (frame.ir_raw >= FINGER_THRESH)
                SSD1306_DrawString(0, 6, "Keep still...");
            else
                SSD1306_DrawString(0, 6, "Place finger");

            osMutexAcquire(i2cMutexHandle, osWaitForever);
            SSD1306_Flush();
            /* If display NACK'd, I2C peripheral may be in error —
             * recover so MAX30102 / MPU6050 reads still work */
            if (hi2c1.ErrorCode != HAL_I2C_ERROR_NONE) {
                HAL_I2C_DeInit(&hi2c1);
                HAL_I2C_Init(&hi2c1);
            }
            osMutexRelease(i2cMutexHandle);
        }
    }
}
