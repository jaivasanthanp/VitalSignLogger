#include "peak_detect.h"
#include "stm32u5xx_hal.h"

void PD_Init(PeakDetect_t *pd) {
    pd->threshold = 200.0f;
    pd->peak_val = 0.0f;
    pd->prev = 0.0f;
    pd->last_tick = 0;
    pd->rr_ms = 0;
    pd->bpm = 0;
}

uint8_t PD_Update(PeakDetect_t *pd, float s) {
    uint32_t now = HAL_GetTick();
    uint8_t found = 0;

    // Peak = signal was above threshold and is now falling
    if (pd->prev > pd->threshold && s < pd->prev &&
        (now - pd->last_tick) > REFRACTORY_MS) {

        pd->rr_ms = now - pd->last_tick;
        pd->last_tick = now;

        if (pd->rr_ms > 0 && pd->rr_ms < 2000) {
            pd->bpm = (uint8_t)(60000 / pd->rr_ms);
        }

        pd->threshold = pd->peak_val * 0.4f;  // adaptive: 40% of last peak
        pd->peak_val = 0.0f;
        found = 1;
    }

    if (s > pd->peak_val) {
        pd->peak_val = s;
    }
    pd->prev = s;

    return found;
}
