/* usb_tx.h — USB CDC transmit wrapper
 * Guide reference: Chapter 7.2
 */

#pragma once

#include <stdint.h>

/* Send len bytes over USB CDC.
 * Retries up to 20 ms if CDC is busy, then drops the frame.
 * Safe to call from any FreeRTOS task. */
void USBTX_Send(const char *buf, uint16_t len);
