#include "biquad.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

void Biquad_Init(Biquad_t *bq, BiquadType_t type, float fc, float Q, float fs) {
    float K = tanf(M_PI * fc / fs);
    float norm;

    if (type == BQ_LOWPASS) {
        norm = 1.0f / (1.0f + K / Q + K * K);
        bq->a0 = K * K * norm;
        bq->a1 = 2.0f * bq->a0;
        bq->a2 = bq->a0;
        bq->b1 = 2.0f * (K * K - 1.0f) * norm;
        bq->b2 = (1.0f - K / Q + K * K) * norm;
    } else if (type == BQ_HIGHPASS) {
        norm = 1.0f / (1.0f + K / Q + K * K);
        bq->a0 = 1.0f * norm;
        bq->a1 = -2.0f * bq->a0;
        bq->a2 = bq->a0;
        bq->b1 = 2.0f * (K * K - 1.0f) * norm;
        bq->b2 = (1.0f - K / Q + K * K) * norm;
    }

    bq->z1 = 0.0f;
    bq->z2 = 0.0f;
}

float Biquad_Process(Biquad_t *bq, float in) {
    float out = in * bq->a0 + bq->z1;
    bq->z1 = in * bq->a1 + bq->z2 - bq->b1 * out;
    bq->z2 = in * bq->a2 - bq->b2 * out;
    return out;
}
