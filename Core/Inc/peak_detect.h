#pragma once
#include <stdint.h>

#define REFRACTORY_MS  350   // min 350 ms between peaks = max 171 BPM

typedef struct {
    float    threshold;
    float    peak_val;
    float    prev;
    uint32_t last_tick;
    uint32_t rr_ms;
    uint8_t  bpm;
} PeakDetect_t;

void    PD_Init(PeakDetect_t *pd);
uint8_t PD_Update(PeakDetect_t *pd, float s);
