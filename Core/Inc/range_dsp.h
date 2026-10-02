#ifndef RANGE_DSP_H
#define RANGE_DSP_H
#include <stdint.h>
#include "app_board_config.h"
#if APP_RANGE_AUDIO_PROFILE == APP_RANGE_AUDIO_LEGACY
#define RANGE_SIGNATURE_SAMPLES (5376U)
#define RANGE_PULSE_STEP (1920U)
#else
#define RANGE_SIGNATURE_SAMPLES (4992U)
#define RANGE_PULSE_STEP (1728U)
#endif
#if APP_RANGE_JOINT_PEAKS
#define RANGE_WINDOW_SAMPLES (6912U) /* right margin for candidate extraction */
#else
#define RANGE_WINDOW_SAMPLES (6144U)
#endif
#define RANGE_PULSE_SAMPLES (1536U)
#define RANGE_SCAN_ADVANCE APP_AUDIO_HALF_FRAMES
#define RANGE_SCAN_SLICE (192U)
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
/* Local LEGACY diagnostics, main loop only, retained until reboot. Counts are
 * search attempts, NOT transmitted signatures. Each coarse pass ends in one
 * of failPulse[0..2], gapRejected or signatures. Maxima are independent and
 * reuse evaluated correlations (later pulses are short-circuit gated). */
typedef struct {
  uint32_t coarsePassed, failPulse[3], gapRejected, signatures;
  uint32_t noCandidates, candidateOverflow;
  float coarseMax, pulseMax[3], gapMinRatio;
  uint8_t haveGap;
} RangeDspDiagnostics;
extern RangeDspDiagnostics rangeDspDiagnostics;
/* Search [first, end) template-start positions, at most RANGE_SCAN_SLICE starts per call.
 * Returns a fractional sample position; polarity invariant correlation. */
int RangeDsp_Find(const int16_t *x, unsigned first, unsigned end,
                  float *position, uint32_t *quality);
/* Delta is corrected B-A arrival time, ns; temperature in 0.1 deg C.
 * Returns 0 outside 0..5 m or invalid parameter range. */
int RangeDsp_Distance(int64_t deltaNs, int32_t temperatureDeciC,
                      uint32_t *mm, int32_t *direction);
#endif
