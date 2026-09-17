#ifndef APP_MIC_SCOPE_H
#define APP_MIC_SCOPE_H
#include <stdint.h>

/* Debug waveform hold; 0 disables automatic holding. Does not stop audio DMA. */
#define MIC_SCOPE_HOLD_MS 2000U
/* Minimum average absolute PCM amplitude inside a chirp (0..32767 units). */
#define MIC_SCOPE_TRIGGER_MIN_LEVEL 128U

typedef enum
{
  MIC_SCOPE_RANGE_WAITING = 0,
  MIC_SCOPE_RANGE_MEASURING,
  MIC_SCOPE_RANGE_VALID,
  MIC_SCOPE_RANGE_INVALID
} MicScope_RangeState;

void MicScope_Init(void);
void MicScope_Process(void);
/* Main-loop calls only. Input is board separation in millimeters, not
 * raw sound delay or local microphone spacing. Valid display: 0..999999 mm.
 * Results expire after 5 seconds; refresh only for a new measurement.
 * Call after Init. These functions do not implement ranging. */
void MicScope_SetDistanceMm(uint32_t millimeters);
/* WAITING, MEASURING or INVALID; use SetDistanceMm to publish VALID. */
void MicScope_SetRangeState(MicScope_RangeState state);
#endif
