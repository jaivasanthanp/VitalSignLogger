#pragma once
#include "stm32u5xx_hal.h"
#include <stdint.h>

#define SSD1306_ADDR    (0x3C << 1)   /* 0x78 — SA0 LOW (default); change to (0x3D<<1) if blank */
#define SSD1306_WIDTH   128
#define SSD1306_HEIGHT  64
#define SSD1306_PAGES   8             /* HEIGHT / 8 */

/* Call once before any other function. Assumes I2C mutex is held by caller. */
void SSD1306_Init(I2C_HandleTypeDef *hi2c);

/* Write zeros to framebuffer only (no I2C). */
void SSD1306_Clear(void);

/* Write ASCII string to framebuffer at page-row (0-7) and pixel-column (0-127).
 * Each char is 6 px wide (5 glyph + 1 spacing). No I2C — call Flush() to push. */
void SSD1306_DrawString(uint8_t col, uint8_t page, const char *str);

/* Push full 1024-byte framebuffer to display over I2C.
 * At 100 kHz this takes ~103 ms — acquire i2cMutexHandle before calling. */
void SSD1306_Flush(void);
