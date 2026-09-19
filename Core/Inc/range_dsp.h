#ifndef RANGE_DSP_H
#define RANGE_DSP_H
#include <stdint.h>
#define RANGE_SIGNATURE_SAMPLES 1792U
#define RANGE_WINDOW_SAMPLES 2048U
/* Last accepted signature: local per-pulse peak spread, in sample units.
 * Diagnostic only; it does not establish a direct acoustic path. */
extern uint32_t rangeDspPeakSpreadSamples;
/* Search [first, end) template-start positions, <=32 candidates per call.
 * Returns a fractional sample position; polarity invariant correlation. */
int RangeDsp_Find(const int16_t *x, unsigned first, unsigned end,
                  float *position, uint32_t *quality);
/* Delta is corrected B-A arrival time, ns; temperature in 0.1 deg C.
 * Returns 0 outside 0..5 m or invalid parameter range. */
int RangeDsp_Distance(int64_t deltaNs, int32_t temperatureDeciC,
                      uint32_t *mm, int32_t *direction);
#endif
