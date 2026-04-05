/* ml_task.h — ML inference task for VitalSignLogger
 * Guide reference: Chapter 9.5
 */

#pragma once

#include <stdint.h>

/* Initialise the AI network — call once before MLTask starts */
void    ML_Init(void);

/* Run one inference cycle — returns 0=Rest 1=Walking 2=Arm Raised */
uint8_t ML_Classify(void);

/* FreeRTOS task entry point — register this in CubeMX as MLTask */
void    MLTask(void *argument);
