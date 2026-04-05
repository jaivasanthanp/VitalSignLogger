/* usb_tx.c — USART1 transmit wrapper (ST-Link Virtual COM Port)
 * Guide reference: Chapter 7.2
 *
 * Replaces CDC_Transmit_FS() with HAL_UART_Transmit() on huart1.
 * Uses a 20 ms timeout then drops the frame to avoid blocking tasks.
 */

#include "usb_tx.h"
#include "stm32u5xx_hal.h"
#include "cmsis_os.h"

/* huart1 is declared and initialised by CubeMX in main.c */
extern UART_HandleTypeDef huart1;

void USBTX_Send(const char *buf, uint16_t len)
{
    /* HAL_UART_Transmit blocks until done or timeout.
     * 20 ms is enough for 256 bytes at 115200 baud (~22 ms worst case).
     * Drop the frame silently if UART is still busy — never block tasks. */
	if (buf == NULL || len == 0) return;
    HAL_UART_Transmit(&huart1, (const uint8_t *)buf, len, 50);
}
