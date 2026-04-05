/* ml_task.c — On-device motion classification using STM32Cube.AI
 * Guide reference: Chapter 9.5
 *
 * This task runs every 1 second (100 samples at 100 Hz).
 * It extracts the same 19 features as the Python training script,
 * runs the neural network, and writes the result to current_motion_class.
 *
 * Feature vector layout (must be IDENTICAL to train_classifier.py):
 *   [0..3]   ax: mean, std, min, max
 *   [4..7]   ay: mean, std, min, max
 *   [8..11]  az: mean, std, min, max
 *   [12..15] g:  mean, std, min, max
 *   [16]     g percentile 25
 *   [17]     g percentile 75
 *   [18]     mean absolute deviation of g
 */

#include "ml_task.h"
#include "shared.h"
#include "ai_platform.h"
#include "network.h"
#include "network_data.h"
#include "cmsis_os2.h"
#include <math.h>
#include <string.h>
#include <stdio.h>

/* ---------------------------------------------------------------
 * Shared buffer — filled by SensorTask at 100 Hz
 * --------------------------------------------------------------- */
volatile float   accel_buf[2][100][3] = {0};
volatile uint8_t accel_write_idx      = 0;
volatile uint8_t current_motion_class = MOTION_REST;

/* ---------------------------------------------------------------
 * Private: AI network handle + activation buffer
 * --------------------------------------------------------------- */
static ai_handle     network_hdl  = AI_HANDLE_NULL;
static ai_u8         activations[AI_NETWORK_DATA_ACTIVATIONS_SIZE];

/* ---------------------------------------------------------------
 * Private helper: simple statistics on a float array of length N
 * --------------------------------------------------------------- */
osSemaphoreId_t accelBufSemHandle;



static void array_stats(const float *arr, int n,
                         float *out_mean, float *out_std,
                         float *out_min,  float *out_max)
{
    float sum  = 0.0f, sum2 = 0.0f;
    float vmin = arr[0], vmax = arr[0];

    for (int i = 0; i < n; i++) {
        sum  += arr[i];
        sum2 += arr[i] * arr[i];
        if (arr[i] < vmin) vmin = arr[i];
        if (arr[i] > vmax) vmax = arr[i];
    }

    *out_mean = sum / n;
    /* population std dev: sqrt(E[x^2] - E[x]^2) */
    float var = (sum2 / n) - (*out_mean * *out_mean);
    *out_std  = (var > 0.0f) ? sqrtf(var) : 0.0f;
    *out_min  = vmin;
    *out_max  = vmax;
}

/* ---------------------------------------------------------------
 * Private helper: approximate percentile via sorting a copy
 * Uses insertion sort (fine for N=100)
 * --------------------------------------------------------------- */
static float array_percentile(const float *arr, int n, float pct)
{
    /* Copy to local scratch so we don't destroy original */
    float tmp[100];
    memcpy(tmp, arr, n * sizeof(float));

    /* Insertion sort */
    for (int i = 1; i < n; i++) {
        float key = tmp[i];
        int j = i - 1;
        while (j >= 0 && tmp[j] > key) {
            tmp[j + 1] = tmp[j];
            j--;
        }
        tmp[j + 1] = key;
    }

    /* Linear interpolation */
    float pos = pct / 100.0f * (n - 1);
    int   lo  = (int)pos;
    float frac = pos - lo;
    if (lo >= n - 1) return tmp[n - 1];
    return tmp[lo] + frac * (tmp[lo + 1] - tmp[lo]);
}

/* ---------------------------------------------------------------
 * Private helper: mean absolute deviation of consecutive diffs
 * Matches Python: np.mean(np.abs(np.diff(g)))
 * --------------------------------------------------------------- */
static float mean_abs_diff(const float *arr, int n)
{
    if (n < 2) return 0.0f;
    float sum = 0.0f;
    for (int i = 1; i < n; i++) {
        float d = arr[i] - arr[i - 1];
        sum += (d < 0.0f) ? -d : d;
    }
    return sum / (n - 1);
}

/* ---------------------------------------------------------------
 * extract_features()
 * Computes the same 19 features as train_classifier.py
 * from the global accel_buf[100][3].
 * --------------------------------------------------------------- */
static void extract_features(float feat[AI_NETWORK_IN_1_SIZE])
{
    float ax[100], ay[100], az[100], g[100];


    /* Read from the buffer SensorTask just finished —
     * that's the one opposite to what it's currently writing */
    uint8_t read_idx = 1 - accel_write_idx;
    /* Unpack the 2-D buffer */
    for (int i = 0; i < 100; i++) {
            ax[i] = accel_buf[read_idx][i][0];
            ay[i] = accel_buf[read_idx][i][1];
            az[i] = accel_buf[read_idx][i][2];
            g[i]  = sqrtf(ax[i]*ax[i] + ay[i]*ay[i] + az[i]*az[i]);
    }

    float mean, std, vmin, vmax;

    /* ax: [0..3] */
    array_stats(ax, 100, &mean, &std, &vmin, &vmax);
    feat[0] = mean; feat[1] = std; feat[2] = vmin; feat[3] = vmax;

    /* ay: [4..7] */
    array_stats(ay, 100, &mean, &std, &vmin, &vmax);
    feat[4] = mean; feat[5] = std; feat[6] = vmin; feat[7] = vmax;

    /* az: [8..11] */
    array_stats(az, 100, &mean, &std, &vmin, &vmax);
    feat[8] = mean; feat[9] = std; feat[10] = vmin; feat[11] = vmax;

    /* g: [12..15] */
    array_stats(g, 100, &mean, &std, &vmin, &vmax);
    feat[12] = mean; feat[13] = std; feat[14] = vmin; feat[15] = vmax;

    /* g percentiles: [16..17] */
    feat[16] = array_percentile(g, 100, 25.0f);
    feat[17] = array_percentile(g, 100, 75.0f);

    /* mean absolute deviation of g: [18] */
    feat[18] = mean_abs_diff(g, 100);
}

/* ---------------------------------------------------------------
 * ML_Init()
 * Call once from main() or MLTask before the loop starts.
 * --------------------------------------------------------------- */
void ML_Init(void)
{
    accelBufSemHandle = osSemaphoreNew(1, 0, NULL);
    if (accelBufSemHandle == NULL) {
        printf("ML_Init: semaphore creation FAILED\r\n");
        return;
    }
    const ai_handle acts[] = { activations };
	ai_error err = ai_network_create_and_init(&network_hdl, acts, NULL);
    if (err.type != AI_ERROR_NONE) {
        printf("ML_Init: ai_network_create_and_init FAILED (type=%d code=%d)\r\n",
               err.type, err.code);
    } else {
        printf("ML_Init: network ready. Input size=%d Output size=%d\r\n",
               AI_NETWORK_IN_1_SIZE, AI_NETWORK_OUT_1_SIZE);
    }
}

/* ---------------------------------------------------------------
 * ML_Classify()
 * Returns 0=Rest, 1=Walking, 2=Arm Raised.
 * Returns previous class if buffer not ready yet.
 * --------------------------------------------------------------- */
uint8_t ML_Classify(void)
{
    /* Get the network's own buffer descriptors — these point into
     * the activations buffer (AI_NETWORK_INPUTS_IN_ACTIVATIONS=4). */
    ai_buffer *ai_input  = ai_network_inputs_get(network_hdl, NULL);
    ai_buffer *ai_output = ai_network_outputs_get(network_hdl, NULL);
    if (!ai_input || !ai_output) return current_motion_class;

    /* Extract features directly into the network's input buffer */
    float *in_data = (float *)ai_input[0].data;
    extract_features(in_data);

    /* --- Run inference --- */
    ai_i32 ret = ai_network_run(network_hdl, ai_input, ai_output);
    if (ret != 1) {
        ai_error err = ai_network_get_error(network_hdl);
        printf("ML: run fail ret=%d type=%d code=%d\r\n",
               (int)ret, err.type, err.code);
        return current_motion_class;
    }

    /* --- Argmax: find highest-confidence class --- */
    float *out_data = (float *)ai_output[0].data;
    uint8_t cls = 0;
    for (int i = 1; i < AI_NETWORK_OUT_1_SIZE; i++) {
        if (out_data[i] > out_data[cls]) cls = (uint8_t)i;
    }

    printf("ML: R=%.2f W=%.2f A=%.2f -> %d\r\n",
           out_data[0], out_data[1], out_data[2], cls);

    return cls;
}

/* ---------------------------------------------------------------
 * MLTask()
 * FreeRTOS task entry point.
 * Priority: BELOW_NORMAL  Stack: 1024 words  (set in CubeMX)
 * Guide reference: Chapter 6.1
 * --------------------------------------------------------------- */
void MLTask(void *argument)
{
    (void)argument;

    ML_Init();

    for (;;) {
        /* Block until SensorTask fills a 100-sample window */
        osSemaphoreAcquire(accelBufSemHandle, osWaitForever);

        current_motion_class = ML_Classify();
    }
}
