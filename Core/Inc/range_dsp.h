#ifndef RANGE_DSP_H
#define RANGE_DSP_H
#include <stdint.h>
#include "app_board_config.h"
#if APP_RANGE_AUDIO_PROFILE == APP_RANGE_AUDIO_LEGACY
#define RANGE_SIGNATURE_SAMPLES (1792U * APP_AUDIO_SCALE)
#define RANGE_PULSE_STEP (640U * APP_AUDIO_SCALE)
#else
#define RANGE_SIGNATURE_SAMPLES (1664U * APP_AUDIO_SCALE)
#define RANGE_PULSE_STEP (576U * APP_AUDIO_SCALE)
#endif
#if APP_RANGE_JOINT_PEAKS
#define RANGE_WINDOW_SAMPLES (2304U * APP_AUDIO_SCALE) /* right margin for candidate extraction */
#else
#define RANGE_WINDOW_SAMPLES (2048U * APP_AUDIO_SCALE)
#endif
#define RANGE_PULSE_SAMPLES (512U * APP_AUDIO_SCALE)
#define RANGE_SCAN_ADVANCE APP_AUDIO_HALF_FRAMES
#define RANGE_SCAN_SLICE (64U * APP_AUDIO_SCALE)
#define RANGE_REFRACTORY_SAMPLES (APP_AUDIO_SAMPLE_RATE / 4U)
#define RANGE_MAX_PEAKS 3U
typedef struct {
  float position[3]; /* Window positions of the three pulse starts. */
  uint32_t quality;
} RangeDspPeak;
typedef struct {
  RangeDspPeak peak[RANGE_MAX_PEAKS];
  uint32_t count, overflow;
} RangeDspPeaks;
void RangeDsp_Candidates(const int16_t *x, float position, RangeDspPeaks *peaks);
/* Last accepted signature: local per-pulse peak spread, in sample units.
 * Diagnostic only; it does not establish a direct acoustic path. */
extern uint32_t rangeDspPeakSpreadSamples;
/* Search [first, end) template-start positions, at most RANGE_SCAN_SLICE starts per call.
 * Returns a fractional sample position; polarity invariant correlation. */
int RangeDsp_Find(const int16_t *x, unsigned first, unsigned end,
                  float *position, uint32_t *quality);
/* Delta is corrected B-A arrival time, ns; temperature in 0.1 deg C.
 * Returns 0 outside 0..5 m or invalid parameter range. */
int RangeDsp_Distance(int64_t deltaNs, int32_t temperatureDeciC,
                      uint32_t *mm, int32_t *direction);
#endif
