#pragma once

typedef enum {
    BQ_LOWPASS,
    BQ_HIGHPASS
} BiquadType_t;

typedef struct {
    float a0, a1, a2, b1, b2;
    float z1, z2;
} Biquad_t;

void Biquad_Init(Biquad_t *bq, BiquadType_t type, float fc, float Q, float fs);
float Biquad_Process(Biquad_t *bq, float in);
